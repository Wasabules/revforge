#!/usr/bin/env python3
"""Compare un rendu à de vrais enregistrements avec des descripteurs perceptifs.

But : prédire ce qu'une oreille reproche, là où le spectre moyen, le rythme et la hauteur des raies
disent que tout va bien. Tous les descripteurs sont invariants au niveau et ne demandent aucun
alignement temporel : ce sont des statistiques sur des fenêtres de même durée (FENETRE), pour qu'un
segment de 2 s et un de 0,5 s restent comparables.

    tools/perceptual_compare.py --ref a.wav:0.5:2.8 b.wav:0.3:1.2 ... --rendu r.wav:0.05:0.72 --png p.png
    tools/perceptual_compare.py --ref ... --valider --controles DOSSIER   # contrôles, LOO, sensibilité
    tools/perceptual_compare.py --etalonner                               # sonie, netteté, rugosité...

Décision (voir Referentiel) : pour chaque descripteur, écart z = (x - médiane des réfs) / σ, où
σ² = s²(1 + 1/n) + σ_capture². s est la dispersion des réfs (intervalle de prédiction de Student, car n
est petit) ; σ_capture mesure de combien le descripteur bouge si l'on change seulement la prise de son
(égalisation, réverbération, compression, codec), de sorte qu'un descripteur instable se discrédite tout
seul. Le seuil vaut le plus grand de t(n-1, 99 %) et du pire écart observé en leave-one-out parmi les
réfs : le bruit de fond. La sévérité est |z| / seuil : au-dessus de 1, hors plage. Les réfs jugées en
leave-one-out le sont avec un seuil calculé sans elles (LOO imbriqué), sinon elles passeraient par
construction. Un seul descripteur hors plage sur quatre-vingt-dix ne prouve rien : le verdict global est
la moyenne des trois familles les plus touchées, spectre moyen exclu (voir `score_global`). En dessous de
1, dans la plage ; de 1 à 1,5, limite ; au-dessus, hors plage. Ces deux seuils ont été réglés sur de vrais
démarreurs absents de ces références : cinq segments sono (0534, 0557, 1146, 1147, 1286), puis
trois autres à l'aveugle (s1_i4t, rcf_v8, gt500_v8 à 14,5 s) : tous sous 1,5, trois en « limite ».

Approximations assumées, à ne pas citer comme des mesures normalisées :
- sonie : formule de Zwicker sur 48 demi-Bark, seuil de Terhardt et étalement de Schroeder, et non
  ISO 532-1 ; étalonnée à 1 sone pour 1 kHz à 40 dB.
- netteté : formule DIN 45692 (g(z) incluse) appliquée à cette sonie approchée.
- rugosité : structure de Daniel & Weber 1997 (47 canaux, profondeur de modulation pondérée par canal,
  corrélations entre canaux voisins) ; les filtres de modulation H_i sont remplacés par des gaussiennes
  en log-fréquence. Étalonnée à 1 asper pour 1 kHz, 60 dB, modulé à 100 % à 70 Hz.
- force de fluctuation : ΔL par bande, pondéré par 1/(f/4 + 4/f), à la Zwicker & Fastl ; étalonnée à
  1 vacil pour 1 kHz, 60 dB, modulé à 100 % à 4 Hz. Sur 0,45 s, on ne voit rien en dessous de 4,4 Hz.
- le niveau d'écoute est fixé arbitrairement à 75 dB(A) : les réfs n'ont pas de calibration absolue.

Limites mesurées (`--valider`) :
- les plages décrivent de vrais démarreurs dans leur diversité (essence, diesel, intérieur, extérieur).
  Être dans la plage ne prouve pas qu'un son est bon, seulement que ces statistiques ne le trahissent pas.
  L'écoute tranche.
- la réverbération rapproche tout des références (elle étale les bouffées) : un défaut de fond peut se
  cacher sous une salle. Égalisation, codec (mp3, opus) et compression déplacent à peine le score.
- un défaut impulsif se voit dès -10 dB sous le son ; un défaut tonal (raie glissante, résonance) ou un
  souffle doit dominer le son pour faire sortir le score. Regarder alors la famille Tonalité, pas le score.
- un vrai bruit au spectre exact d'un démarreur (`phases_aleatoires`) reste en « limite », pas hors plage.
- un vrai moteur qui tourne (ralenti, montée) sort aussi : hors plage veut dire « ne ressemble pas à ces
  références », pas « sonne faux ». C'est le détail par famille qui dit quoi écouter.
- sous 0,71 s, on ne mesure rien sous 4 Hz ; sans références plus longues, ces bandes ne sont pas jugées.
"""
import argparse
import os
import warnings
from concurrent.futures import ProcessPoolExecutor

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import numpy as np
import scipy.fft as sfft
import scipy.io.wavfile as wavfile
from scipy import stats as spstats
from scipy.ndimage import median_filter
from scipy.signal import butter, resample_poly, sosfilt, sosfiltfilt, spectrogram

# Un descripteur non mesurable vaut NaN : c'est prévu, inutile de le signaler à chaque calcul.
warnings.filterwarnings("ignore", message="All-NaN")
warnings.filterwarnings("ignore", message="Mean of empty slice")
warnings.filterwarnings("ignore", message="Degrees of freedom")

FS = 44100
FENETRE = 0.45  # s : la durée du plus court segment utile. Tout est mesuré à cette échelle.
NIVEAU_DBA = 75.0  # niveau d'écoute conventionnel
P0 = 20e-6

# Étalonnages (voir --etalonner), mesurés sur les signaux de référence de chaque norme.
K_SONIE = 0.7706
K_RUGOSITE = 0.1262
K_FLUCTUATION = 0.00722  # la constante de Zwicker & Fastl vaut 0,008

MODS = [(4, 8), (8, 16), (16, 32), (32, 64), (64, 128), (128, 300)]  # Hz, sur une fenêtre
MODS_LENTES = [(0.5, 1), (1, 2), (2, 4)]  # sur tout le segment : seulement s'il est assez long
OCTAVES = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
GROUPES_OCT = {"grave": (0, 3), "medium": (3, 6), "aigu": (6, 8)}
RESOLUTIONS = (256, 512, 1024, 2048, 4096)


# ---------------------------------------------------------------- lecture et préparation

def lire_segment(spec):
    """Lit « chemin.wav:debut:fin » (ou « chemin.wav ») : mono, 44,1 kHz, flottant."""
    parts = spec.rsplit(":", 2)
    if len(parts) == 3:
        chemin, debut, fin = parts[0], float(parts[1]), float(parts[2])
    else:
        chemin, debut, fin = spec, 0.0, None
    rate, x = wavfile.read(chemin)
    if np.issubdtype(x.dtype, np.integer):
        x = x.astype(float) / float(np.iinfo(x.dtype).max)
    else:
        x = x.astype(float)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x = x[int(debut * rate):None if fin is None else int(fin * rate)]
    if rate != FS:
        pgcd = np.gcd(rate, FS)
        x = resample_poly(x, FS // pgcd, rate // pgcd)
    if len(x) < 0.2 * FS:
        raise SystemExit(f"{spec} : segment trop court ({len(x) / FS:.2f} s)")
    return x


def nom_court(spec):
    return os.path.basename(spec.rsplit(":", 2)[0]).removesuffix(".wav")


def ponderation_a(f):
    f2 = np.maximum(f, 1.0) ** 2
    ra = (12194.0**2 * f2**2) / ((f2 + 20.6**2) * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2)) * (f2 + 12194.0**2))
    return ra * 10 ** (2.0 / 20)


def normaliser(x):
    """Ramène à NIVEAU_DBA (en pascals) : c'est ce qui rend tous les descripteurs invariants au niveau."""
    spectre = np.abs(sfft.rfft(x)) * ponderation_a(sfft.rfftfreq(len(x), 1 / FS))
    rms_a = np.sqrt(2 * (spectre**2).sum()) / len(x)
    return x * (P0 * 10 ** (NIVEAU_DBA / 20)) / max(rms_a, 1e-12)


def fenetres(x):
    """Découpe en fenêtres de FENETRE s qui couvrent tout le segment, avec environ 50 % de recouvrement."""
    n = int(FENETRE * FS)
    if len(x) <= n:
        return [x]
    compte = int(np.ceil((len(x) - n) / (n / 2))) + 1
    return [x[int(d):int(d) + n] for d in np.linspace(0, len(x) - n, compte)]


def trames(x, n, pas):
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    return np.lib.stride_tricks.sliding_window_view(x, n)[::pas]


def spectre_puissance(x, n, pas):
    """Spectre en Pa²/raie, tel que la somme des raies redonne le carré moyen de la trame."""
    w = np.hanning(n)
    return np.abs(sfft.rfft(trames(x, n, pas) * w, axis=1)) ** 2 * 2 / (n * (w**2).sum())


# ---------------------------------------------------------------- Bark, sonie, netteté

def hz_vers_bark(f):
    return 13 * np.arctan(0.00076 * f) + 3.5 * np.arctan((f / 7500.0) ** 2)


def seuil_terhardt(f):
    fk = np.maximum(f, 20.0) / 1000
    return np.minimum(3.64 * fk**-0.8 - 6.5 * np.exp(-0.6 * (fk - 3.3) ** 2) + 1e-3 * fk**4, 90.0)


NB_BARK = 48
Z_BARK = (np.arange(NB_BARK) + 0.5) / 2


def _etalement():
    dz = Z_BARK[:, None] - Z_BARK[None, :]  # bande reçue - bande émettrice
    db = 15.81 + 7.5 * (dz + 0.474) - 17.5 * np.sqrt(1 + (dz + 0.474) ** 2)
    m = 10 ** (db / 10)
    return m / m.sum(axis=0, keepdims=True)


ETALEMENT = _etalement()
_CACHE_BARK = {}


def bandes_bark(n):
    if n not in _CACHE_BARK:
        f = sfft.rfftfreq(n, 1 / FS)
        idx = np.minimum((hz_vers_bark(f) * 2).astype(int), NB_BARK - 1)
        idx[f < 20] = -1
        matrice = np.zeros((NB_BARK, len(f)))
        matrice[idx[idx >= 0], np.nonzero(idx >= 0)[0]] = 1.0
        # Le seuil par bande, évalué au centre de chaque demi-Bark.
        grille = np.linspace(20, FS / 2, 4000)
        seuil = np.interp(Z_BARK, hz_vers_bark(grille), seuil_terhardt(grille))
        _CACHE_BARK[n] = (matrice, 10 ** (seuil / 10))
    return _CACHE_BARK[n]


def excitation_et_sonie(x, n=1024, pas=220):
    """Renvoie l'excitation (temps x bandes, relative à P0²) et la sonie spécifique (sones/Bark)."""
    matrice, e_tq = bandes_bark(n)
    p = spectre_puissance(x, n, pas) / P0**2
    # Densité par Bark : deux demi-bandes par Bark.
    e = (p @ matrice.T) @ ETALEMENT.T * 2
    n_spec = K_SONIE * 0.08 * e_tq**0.23 * (np.maximum(0.5 + 0.5 * e / e_tq, 1.0) ** 0.23 - 1)
    return e, n_spec


def nettete_din(n_spec):
    """DIN 45692 : pondération g(z) relevée au-dessus de 15,8 Bark, en acum."""
    z = Z_BARK
    g = np.where(z <= 15.8, 1.0, 0.15 * np.exp(0.42 * (z - 15.8)) + 0.85)
    return 0.11 * (n_spec * g * z).sum(axis=-1) / np.maximum(n_spec.sum(axis=-1), 1e-9)


def d_sonie_nettete(x, e, n_spec, sortie):
    profil = n_spec.mean(axis=0)
    total = profil.sum() + 1e-9
    # Par groupes de 3 Bark : assez fin pour le timbre, assez large pour résister à la prise de son.
    for i in range(0, 24, 3):
        part = profil[(Z_BARK >= i) & (Z_BARK < i + 3)].sum() / total
        sortie[f"bark_{i:02d}_{i + 3:02d}"] = 10 * np.log10(part + 1e-5)
    instant = n_spec.sum(axis=1) / 2  # sones
    n5, n50, n95 = np.percentile(instant, [95, 50, 5])
    sortie["sonie_n5_n50"] = 20 * np.log10((n5 + 1e-3) / (n50 + 1e-3))
    sortie["sonie_n50_n95"] = 20 * np.log10((n50 + 1e-3) / (n95 + 1e-3))
    s = nettete_din(n_spec)
    sortie["nettete_med"] = float(np.median(s))
    sortie["nettete_var"] = float(np.percentile(s, 90) - np.percentile(s, 10))


# ---------------------------------------------------------------- rugosité (Daniel & Weber, approchée)

_G_Z = np.array([[0, 1, 2.5, 4.9, 6.5, 8, 9, 10, 11, 11.5, 13, 17.5, 21, 24],
                 [0, 0.35, 0.7, 0.7, 1.1, 1.25, 1.26, 1.18, 1.08, 1, 0.66, 0.46, 0.38, 0.3]])
_Z_CANAUX = np.arange(1, 48) * 0.5


def rugosite(x, n=8820, pas=4410):
    """Rugosité en asper, et son profil par canal."""
    w = np.blackman(n)
    f = sfft.rfftfreq(n, 1 / FS)
    garde = (f >= 20) & (f <= 16000)
    fg = f[garde]
    zf = hz_vers_bark(fg)
    fk = fg / 1000
    oreille_db = -3.64 * fk**-0.8 + 6.5 * np.exp(-0.6 * (fk - 3.3) ** 2) - 1e-3 * fk**4
    seuil_db = seuil_terhardt(fg)
    fm = sfft.rfftfreq(n, 1 / FS)
    # Filtre de modulation : plus lent en très bas de l'échelle Bark, plus rapide au-dessus de 8 Bark.
    centres = np.interp(_Z_CANAUX, [2, 8], [40, 90])  # D'où un maximum à 70 Hz pour 1 kHz, une fois la bande critique passée.
    with np.errstate(divide="ignore"):
        h = np.exp(-0.5 * (np.log(np.maximum(fm, 1e-3)[None, :] / centres[:, None]) / 0.9) ** 2)
    h[:, fm > 350] = 0
    h[:, fm < 8] = 0
    g = np.interp(_Z_CANAUX, _G_Z[0], _G_Z[1])
    amp_vers_db = 2 / w.sum() / P0
    profils = []
    for trame in trames(x, n, pas):
        spectre = sfft.rfft(trame * w)[garde]
        niveau = 20 * np.log10(np.abs(spectre) * amp_vers_db + 1e-9)
        audible = niveau > seuil_db  # sous le seuil absolu : ignoré
        pente_haut = 24 + 0.23 / fk - 0.2 * (niveau + oreille_db)
        dz = _Z_CANAUX[:, None] - zf[None, :]
        attenue = np.where(dz > 0.5, -(dz - 0.5) * pente_haut, np.where(dz < -0.5, (dz + 0.5) * 27, 0.0))
        gain = 10 ** ((attenue + oreille_db) / 20) * audible
        comp = np.zeros((47, n), dtype=complex)
        comp[:, np.nonzero(garde)[0]] = spectre[None, :] * gain * 2
        env = np.abs(sfft.ifft(comp, axis=1))
        moyenne = env.mean(axis=1)
        filtre = sfft.irfft(sfft.rfft(env, axis=1) * h, n=n, axis=1)
        m = np.minimum(np.sqrt((filtre**2).mean(axis=1)) / np.maximum(moyenne, 1e-12), 1.0)
        centre = filtre - filtre.mean(axis=1, keepdims=True)
        norme = np.sqrt((centre**2).sum(axis=1)) + 1e-20
        k = np.zeros(47)
        k[2:] = (centre[2:] * centre[:-2]).sum(axis=1) / (norme[2:] * norme[:-2])
        k = np.maximum(k, 0)
        k_haut = np.zeros(47)
        k_haut[:-2] = k[2:]
        profils.append((g * m * k * k_haut) ** 2)
    profil = K_RUGOSITE * np.median(np.array(profils), axis=0)
    return profil.sum(), profil


def d_rugosite(x, sortie):
    total, profil = rugosite(x)
    sortie["rug_total"] = 10 * np.log10(total + 1e-4)
    sortie["rug_grave"] = 10 * np.log10(profil[_Z_CANAUX < 6].sum() + 1e-4)
    sortie["rug_medium"] = 10 * np.log10(profil[(_Z_CANAUX >= 6) & (_Z_CANAUX < 14)].sum() + 1e-4)
    sortie["rug_aigu"] = 10 * np.log10(profil[_Z_CANAUX >= 14].sum() + 1e-4)


# ---------------------------------------------------------------- force de fluctuation (Zwicker)

def fluctuation(e, pas=220):
    """Force de fluctuation en vacil, à partir de l'excitation par bande."""
    _, e_tq = bandes_bark(1024)
    niveau = 10 * np.log10(np.maximum(e, e_tq))  # sous le seuil, plus aucune modulation audible
    n = niveau.shape[0]
    w = np.hanning(n)
    f = sfft.rfftfreq(n, pas / FS)
    with np.errstate(divide="ignore"):
        poids = 1 / (f / 4 + 4 / f)
    poids[(f < 0.5) | (f > 40)] = 0
    spectre = sfft.rfft((niveau - niveau.mean(axis=0)) * w[:, None], axis=0)
    # Valeur efficace pondérée, puis ΔL crête à crête d'une sinusoïde équivalente.
    variance = 2 * (np.abs(spectre * poids[:, None]) ** 2).sum(axis=0) / (n * (w**2).sum())
    delta = 2 * np.sqrt(2) * np.sqrt(variance)
    return K_FLUCTUATION * delta.sum() * 0.5  # 0,5 : largeur d'une bande en Bark


# ---------------------------------------------------------------- spectre de modulation

def enveloppes_octaves(x):
    n = len(x)
    spectre = sfft.fft(x)
    f = sfft.fftfreq(n, 1 / FS)
    positif = f > 0
    sortie = []
    for fc in OCTAVES:
        h = np.zeros(n)
        d = np.log2(f[positif] / fc)
        h[positif] = np.where(np.abs(d) < 1, np.cos(np.pi * d / 2), 0.0)
        sortie.append(np.abs(sfft.ifft(2 * spectre * h)))
    return np.array(sortie)


def matrice_modulation(env, bandes):
    """Indice de modulation (dB) par octave acoustique et bande de modulation. NaN si non mesurable."""
    n = env.shape[1]
    t = n / FS
    w = np.hanning(n)
    moyenne = env.mean(axis=1, keepdims=True)
    spectre = sfft.rfft((env - moyenne) * w, axis=1)
    fm = sfft.rfftfreq(n, 1 / FS)
    resultat = np.full((len(OCTAVES), len(bandes)), np.nan)
    for j, (bas, haut) in enumerate(bandes):
        # Sous 2 raies, la fenêtre de Hann fuit : on préfère ne rien dire.
        limite = 2 / t
        garde = (fm >= max(bas, limite)) & (fm < haut)
        if limite >= (bas * haut) ** 0.5 or not garde.any():
            continue
        variance = 2 * (np.abs(spectre[:, garde]) ** 2).sum(axis=1) / (n * (w**2).sum())
        m = 20 * np.log10(np.sqrt(variance) / np.maximum(moyenne[:, 0], 1e-12) + 1e-3)
        # Une enveloppe ne module pas plus vite que la largeur de sa bande.
        for i, fc in enumerate(OCTAVES):
            if haut <= 0.7 * fc or j == 0:
                resultat[i, j] = m[i]
    return resultat


def periodicite(x):
    """Saillance du rythme : pic d'autocorrélation de l'enveloppe entre 8 et 125 Hz."""
    sos = butter(4, [150, 6000], btype="band", fs=FS, output="sos")
    env = np.abs(sosfiltfilt(sos, x))
    env = sosfiltfilt(butter(2, 200, fs=FS, output="sos"), env)[::44]
    env = env - env.mean()
    n = len(env)
    corr = sfft.irfft(np.abs(sfft.rfft(env, 2 * n)) ** 2)[:n]
    corr = corr / (corr[0] + 1e-20) * n / np.maximum(n - np.arange(n), n / 3)  # corrigé du biais de longueur
    bas, haut = 8, min(125, n - 1)
    return float(corr[bas:haut].max())


def d_modulation(x, sortie):
    env = enveloppes_octaves(x)
    matrice = matrice_modulation(env, MODS)
    for groupe, (i, j) in GROUPES_OCT.items():
        for k, (bas, haut) in enumerate(MODS):
            col = matrice[i:j, k]
            sortie[f"mod_{groupe}_{bas}_{haut}"] = float(np.nanmean(col)) if np.isfinite(col).any() else np.nan
    sortie["mod_periodicite"] = periodicite(x)
    return matrice


# ---------------------------------------------------------------- tonalité (fenêtres courtes)

BANDES_TON = {"grave": (150, 1000), "medium": (1000, 4000), "aigu": (4000, 10000)}


def d_tonalite(x, sortie):
    n, pas = 2048, 512
    p = spectre_puissance(x, n, pas) + 1e-16
    f = sfft.rfftfreq(n, 1 / FS)
    db = 10 * np.log10(p)
    fond = median_filter(db, size=(1, 41), mode="nearest")
    exces = db - fond
    for nom, (bas, haut) in BANDES_TON.items():
        garde = (f >= bas) & (f < haut)
        raie = exces[:, garde] > 10
        e_raie = (p[:, garde] * raie).sum(axis=1)
        e_total = p[:, garde].sum(axis=1)
        # Plancher à -30 dB : en dessous, il n'y a plus de raie à mesurer, seulement du hasard. Sans lui,
        # une trame sans aucune raie donnerait un écart gigantesque et dénué de sens.
        sortie[f"ton_rapport_{nom}"] = float(10 * np.log10(np.mean(e_raie / e_total) + 1e-3))
    garde = (f >= 150) & (f < 8000)
    sortie["ton_crete"] = float(np.median(exces[:, garde].max(axis=1)))
    # Fenêtre deux fois plus courte : une raie qui glisse vite s'y étale moins, elle reste une raie.
    p_court = spectre_puissance(x, 1024, 256) + 1e-16
    f_court = sfft.rfftfreq(1024, 1 / FS)
    db_court = 10 * np.log10(p_court)
    exces_court = db_court - median_filter(db_court, size=(1, 21), mode="nearest")
    garde_court = (f_court >= 150) & (f_court < 8000)
    sortie["ton_crete_court"] = float(np.median(exces_court[:, garde_court].max(axis=1)))
    raie_court = exces_court[:, garde_court] > 10
    part = (p_court[:, garde_court] * raie_court).sum(axis=1) / p_court[:, garde_court].sum(axis=1)
    sortie["ton_rapport_court"] = float(10 * np.log10(np.mean(part) + 1e-3))
    # Les raies tenues (résonance, « ballon ») survivent à la moyenne ; les raies mobiles (« CD ») s'y étalent.
    long_db = 10 * np.log10(p.mean(axis=0))
    long_exces = long_db - median_filter(long_db, size=41, mode="nearest")
    sortie["ton_crete_longue"] = float(long_exces[garde].max())
    sortie["ton_tenue"] = float(np.mean(np.sort(long_exces[garde])[-8:]))
    # Dérive : vitesse relative des raies fortes suivies d'une trame à l'autre, et part de raies suivies.
    indices = np.nonzero(garde)[0]
    vitesses, suivies, poids = [], 0, 0
    for t in range(exces.shape[0] - 1):
        ligne = exces[t, indices]
        pics = indices[1:-1][(ligne[1:-1] > 15) & (ligne[1:-1] >= ligne[:-2]) & (ligne[1:-1] >= ligne[2:])]
        suivante = exces[t + 1]
        for k in pics[:6]:
            voisins = np.arange(max(k - 3, 0), min(k + 4, len(f)))
            j = voisins[np.argmax(suivante[voisins])]
            poids += 1
            if suivante[j] > 12:
                suivies += 1
                vitesses.append(abs(j - k) * (FS / n) / max(f[k], 1.0) / (pas / FS))
    sortie["ton_suivi"] = suivies / poids if poids else 0.0
    sortie["ton_derive"] = 10 * np.log10(np.mean(vitesses) + 0.1) if vitesses else np.nan


# ---------------------------------------------------------------- flux spectral

def d_flux(x, e, sortie):
    niveau = 10 * np.log10(e + 1.0)
    total = niveau.mean(axis=1, keepdims=True)
    forme = niveau - total
    d = np.sqrt((np.diff(forme, axis=0) ** 2).mean(axis=1)) + 1e-3
    sortie["flux_forme"] = 20 * np.log10(np.median(d))
    sortie["flux_rafale"] = 20 * np.log10(np.percentile(d, 95) / np.median(d))
    dn = np.abs(np.diff(total[:, 0])) + 1e-3
    sortie["flux_niveau"] = 20 * np.log10(np.median(dn))
    sortie["flux_niveau_rafale"] = 20 * np.log10(np.percentile(dn, 95) / np.median(dn))
    # Écart de forme entre trames distantes de 50 ms : évolution lente du timbre.
    if forme.shape[0] > 12:
        lent = np.sqrt((((forme[10:] - forme[:-10]) ** 2).mean(axis=1))) + 1e-3
        sortie["flux_lent"] = 20 * np.log10(np.median(lent))


# ---------------------------------------------------------------- statistiques de texture

def centres_erb(nb=30, bas=50.0, haut=10000.0):
    return np.linspace(21.4 * np.log10(1 + 0.00437 * bas), 21.4 * np.log10(1 + 0.00437 * haut), nb + 2)


def enveloppes_cochlee(x, nb=30):
    """Banc en demi-cosinus, espacé en ERB, comme McDermott & Simoncelli ; enveloppes à 400 Hz."""
    n = len(x)
    spectre = sfft.fft(x)
    f = sfft.fftfreq(n, 1 / FS)
    positif = f > 0
    erb_f = 21.4 * np.log10(1 + 0.00437 * f[positif])
    grille = centres_erb(nb)
    pas = grille[1] - grille[0]
    env = []
    garde = max(int(n * 400 / FS), 8)
    for c in grille[1:-1]:
        h = np.zeros(n)
        d = (erb_f - c) / pas
        h[positif] = np.where(np.abs(d) < 1, np.cos(np.pi * d / 2), 0.0)
        e = np.abs(sfft.ifft(2 * spectre * h))
        # Décimation par troncature spectrale, adoucie pour ne pas sonner sur les bords.
        s = sfft.rfft(e)[: garde // 2 + 1]
        s[-max(len(s) // 8, 1):] *= 0.5
        env.append(sfft.irfft(s, n=garde) * garde / n)
    env = np.maximum(np.array(env), 1e-12)
    return env


def pulsation_commune(relatif, cadence):
    """Moyenne des enveloppes, restreinte aux rythmes de 4 à 64 Hz (une compression, pas le grain)."""
    c = relatif.mean(axis=0)
    spectre = sfft.rfft(c - c.mean())
    f = sfft.rfftfreq(len(c), 1 / cadence)
    spectre[(f < 4) | (f > 64)] = 0
    return sfft.irfft(spectre, n=len(c))


def rythme_commun(c, cadence):
    """Saillance d'une période entre 8 et 64 Hz dans la pulsation commune, corrigée du biais de longueur."""
    n = len(c)
    corr = sfft.irfft(np.abs(sfft.rfft(c - c.mean(), 2 * n)) ** 2)[:n]
    corr = corr / (corr[0] + 1e-20) * n / np.maximum(n - np.arange(n), n / 3)
    bas = max(int(cadence / 64), 1)
    haut = min(int(cadence / 8) + 1, n - 1)
    return float(corr[bas:haut].max())


def d_texture(x, sortie):
    env = enveloppes_cochlee(x) ** 0.3  # compression de la cochlée
    moyenne = env.mean(axis=1)
    centre = env - moyenne[:, None]
    ecart = centre.std(axis=1) + 1e-12
    cv = ecart / moyenne
    asym = (centre**3).mean(axis=1) / ecart**3
    kurt = (centre**4).mean(axis=1) / ecart**4
    tiers = {"grave": (0, 10), "medium": (10, 20), "aigu": (20, 30)}
    for nom, (i, j) in tiers.items():
        sortie[f"tex_var_{nom}"] = 20 * np.log10(np.mean(cv[i:j]))
        sortie[f"tex_asym_{nom}"] = float(np.mean(asym[i:j]))
        sortie[f"tex_kurt_{nom}"] = 10 * np.log10(np.mean(kurt[i:j]))
    corr = np.corrcoef(env)
    corr = np.nan_to_num(corr)
    for d in (1, 2, 4, 8):
        sortie[f"tex_corr_{d}"] = float(np.mean(np.diag(corr, d)))
    sortie["tex_corr_gr_ai"] = float(corr[:10, 20:].mean())
    valeurs = np.maximum(np.linalg.eigvalsh(corr), 0)
    sortie["tex_commun"] = float(valeurs[-1] / (valeurs.sum() + 1e-12))
    # Pulsation commune : on moyenne les enveloppes normalisées de toutes les bandes. Les fluctuations
    # propres à chaque bande (celles de n'importe quel bruit) s'annulent ; seule reste la bouffée que
    # toutes partagent, celle d'une compression. C'est ce qu'un bruit au bon spectre ne sait pas imiter.
    relatif = env / moyenne[:, None]
    for nom, (i, j) in {"": (0, 30), "_grave": (0, 15), "_aigu": (15, 30)}.items():
        commun = pulsation_commune(relatif[i:j], env.shape[1] / (len(x) / FS))
        sortie[f"tex_puls{nom}"] = 20 * np.log10(commun.std() + 1e-4)
        if not nom:
            c = commun - commun.mean()
            sortie["tex_puls_forme"] = float((c**3).mean() / (c.std() ** 3 + 1e-12))
            sortie["tex_puls_rythme"] = rythme_commun(commun, env.shape[1] / (len(x) / FS))
    # Sans compression ni décimation : ce qui claque (cailloux, métal) se voit dans la crête à 1 ms.
    sos = butter(4, 2000, btype="high", fs=FS, output="sos")
    aigu = sosfilt(sos, x)
    centre_a = aigu - aigu.mean()
    sortie["tex_impulsif"] = 10 * np.log10((centre_a**4).mean() / ((centre_a**2).mean() ** 2 + 1e-30) + 1e-6)
    n = 44
    rms = np.sqrt(trames(x**2, n, n).mean(axis=1)) + 1e-9
    sortie["tex_crete_1ms"] = 20 * np.log10(rms.max() / np.sqrt((rms**2).mean()))


# ---------------------------------------------------------------- MFCC

_CACHE_MEL = {}


def banc_mel(n, nb=40, bas=60.0, haut=12000.0):
    if n not in _CACHE_MEL:
        mel = lambda h: 2595 * np.log10(1 + h / 700)
        inv = lambda m: 700 * (10 ** (m / 2595) - 1)
        bords = inv(np.linspace(mel(bas), mel(haut), nb + 2))
        f = sfft.rfftfreq(n, 1 / FS)
        m = np.zeros((nb, len(f)))
        for i in range(nb):
            monte = (f - bords[i]) / (bords[i + 1] - bords[i])
            descend = (bords[i + 2] - f) / (bords[i + 2] - bords[i + 1])
            m[i] = np.maximum(0, np.minimum(monte, descend))
        _CACHE_MEL[n] = m
    return _CACHE_MEL[n]


def d_mfcc(x, sortie):
    n = 1024
    p = spectre_puissance(x, n, 220) / P0**2
    log_mel = 10 * np.log10(p @ banc_mel(n).T + 1.0)
    c = sfft.dct(log_mel, type=2, norm="ortho", axis=1)
    for i in range(1, 7):
        sortie[f"mfcc_moy{i}"] = float(c[:, i].mean())
    for i in range(1, 7):
        sortie[f"mfcc_var{i}"] = float(c[:, i].std())


# ---------------------------------------------------------------- log-STFT multi-résolution

def stats_stft(x):
    """Statistiques de log-magnitude par résolution : variation dans le temps et grain en fréquence.

    On ne compare pas les spectrogrammes eux-mêmes (ils ne s'alignent pas) mais ce qu'ils ont de
    stationnaire. Le spectre moyen (« forme ») est gardé à part : c'est lui que la prise de son déplace.
    """
    grain, forme = {}, []
    bords = np.geomspace(60, 12000, 13)
    for n in RESOLUTIONS:
        db = 10 * np.log10(spectre_puissance(x, n, n // 4) + 1e-14)
        f = sfft.rfftfreq(n, 1 / FS)
        temps, finesse, moyenne = [], [], []
        for bas, haut in zip(bords[:-1], bords[1:]):
            garde = (f >= bas) & (f < haut)
            if garde.sum() < 3:
                temps.append(np.nan)
                finesse.append(np.nan)
                moyenne.append(np.nan)
                continue
            bande = db[:, garde]
            moyenne.append(bande.mean())
            temps.append(bande.mean(axis=1).std())
            finesse.append(bande.std(axis=1).mean())
        grain[n] = np.array(temps + finesse)
        m = np.array(moyenne)
        forme.append(m - np.nanmean(m))
    return {"grain": grain, "forme": np.concatenate(forme)}


def distance_vecteur(a, b):
    valide = np.isfinite(a) & np.isfinite(b)
    return float(np.mean(np.abs(a[valide] - b[valide]))) if valide.any() else np.nan


# ---------------------------------------------------------------- assemblage

def descripteurs(x):
    """Mesure un segment : valeurs scalaires, vecteurs pour la distance STFT et matrice de modulation."""
    x = sosfiltfilt(butter(2, 20, btype="high", fs=FS, output="sos"), x)
    par_fenetre, vecteurs, matrices = [], [], []
    for morceau in fenetres(x):
        morceau = normaliser(morceau)
        sortie = {}
        e, n_spec = excitation_et_sonie(morceau)
        d_sonie_nettete(morceau, e, n_spec, sortie)
        d_rugosite(morceau, sortie)
        sortie["fluct_total"] = 10 * np.log10(fluctuation(e) + 1e-3)
        matrices.append(d_modulation(morceau, sortie))
        d_tonalite(morceau, sortie)
        d_flux(morceau, e, sortie)
        d_texture(morceau, sortie)
        d_mfcc(morceau, sortie)
        par_fenetre.append(sortie)
        vecteurs.append(stats_stft(morceau))
    cles = list(par_fenetre[0])
    valeurs = {c: float(np.nanmedian([p.get(c, np.nan) for p in par_fenetre])) if any(
        np.isfinite(p.get(c, np.nan)) for p in par_fenetre) else np.nan for c in cles}
    # Modulations lentes : sur tout le segment, seulement s'il dure assez longtemps.
    entier = normaliser(x)
    env = enveloppes_octaves(entier)
    lentes = matrice_modulation(env, MODS_LENTES)
    for k, (bas, haut) in enumerate(MODS_LENTES):
        col = lentes[:, k]
        valeurs[f"mod_lente_{bas}_{haut}"] = float(np.nanmean(col)) if np.isfinite(col).any() else np.nan
    matrice = np.nanmedian(np.array(matrices), axis=0)
    grain = {n: np.nanmedian([v["grain"][n] for v in vecteurs], axis=0) for n in RESOLUTIONS}
    forme = np.nanmedian([v["forme"] for v in vecteurs], axis=0)
    return {
        "valeurs": valeurs,
        "grain": grain,
        "forme": forme,
        "matrice": np.hstack([lentes, matrice]),
        "duree": len(x) / FS,
    }


# Le spectre moyen est à part : il n'entre pas dans le score. C'est ce que déplacent le plus le micro,
# la distance et le type de moteur (une AC Cobra ou une prise en habitacle en sortent sans avoir rien de
# faux), et c'est aussi ce que l'on mesurait déjà sans rien entendre.
FAMILLE_SPECTRE = "Spectre moyen"
FAMILLES = [
    (FAMILLE_SPECTRE, "bark_", "mfcc_moy", "nettete_med", "stft_forme"),
    ("Modulation", "mod_"),
    ("Fluctuation, dynamique", "fluct_", "sonie_", "nettete_var"),
    ("Rugosité", "rug_"),
    ("Tonalité", "ton_"),
    ("Flux spectral", "flux_"),
    ("Texture", "tex_"),
    ("MFCC dynamiques", "mfcc_var"),
    ("Log-STFT", "stft_grain"),
]


def famille_de(nom):
    for ligne in FAMILLES:
        if nom.startswith(tuple(ligne[1:])):
            return ligne[0]
    return "Autre"


# Ce que l'oreille risque de dire quand le descripteur sort, dans un sens ou dans l'autre. Ce sont des
# pistes à vérifier à l'écoute, pas des diagnostics.
PISTES = [
    ("ton_rapport", "raies trop pures : sifflement, « vaisseau »", "trop peu de raies : bruit sans corps, « aspirateur »"),
    ("ton_crete_longue", "résonance figée : « ballon », « vaisseau »", "aucune résonance tenue"),
    ("ton_tenue", "résonances figées : « ballon »", "aucune résonance tenue"),
    ("ton_crete", "raie isolée trop pure : « CD », sifflement", "pas de raie émergente : « aspirateur »"),
    ("ton_suivi", "raies trop continues : « CD », « vaisseau »", "raies éparses, sans continuité"),
    ("ton_derive", "raies qui glissent : « CD »", "raies trop fixes : « vaisseau »"),
    ("tex_puls_rythme", "bouffées d'une régularité de métronome", "pas de rythme commun : « aspirateur », « cailloux »"),
    ("tex_puls_forme", "bouffées brèves et pointues", "bouffées qui s'écrasent, creux brefs"),
    ("tex_puls", "bouffées trop marquées, le son est haché d'un bloc", "pas de bouffée commune : « aspirateur », « ballon », « CD »"),
    ("tex_impulsif", "chocs isolés dans l'aigu : « cailloux », « métal qui claque »", "aigu sans chocs : souffle lisse"),
    ("tex_crete", "chocs isolés : « cailloux », « claque »", "volume trop égal : « aspirateur »"),
    ("tex_kurt", "enveloppe à pointes : « cailloux », « fermeture éclair »", "enveloppe trop égale"),
    ("tex_asym", "enveloppe à pointes brèves", "enveloppe à creux brefs"),
    ("tex_var", "bandes trop contrastées, hachées", "bandes trop lisses : « aspirateur », « CD »"),
    ("tex_corr", "bandes qui bougent toutes ensemble : source unique", "bandes indépendantes : pas de bouffée commune, « ballon »"),
    ("tex_commun", "tout bouge d'un bloc", "aucune bouffée commune : « ballon », « aspirateur »"),
    ("mod_periodicite", "rythme trop mécanique", "pas de rythme régulier : « cailloux »"),
    ("mod_", "modulation trop marquée à cette vitesse : « fermeture éclair » si rapide", "enveloppe trop lisse à cette vitesse : « aspirateur », « CD »"),
    ("rug_", "grain trop rêche : « fermeture éclair »", "pas assez de grain : son trop lisse"),
    ("fluct", "pompage trop marqué", "pas de pulsation lente"),
    ("flux_niveau", "volume qui saute d'une trame à l'autre", "volume trop constant : « CD »"),
    ("flux_", "timbre instable, qui change à chaque trame", "timbre figé : « aspirateur », « ballon »"),
    ("nettete", "trop aigu, métallique", "trop sourd"),
    ("bark_", "trop d'énergie dans cette bande (spectre)", "trop peu d'énergie dans cette bande (spectre)"),
    ("sonie_", "volume en dents de scie", "volume trop constant"),
    ("mfcc_", "timbre global décalé", "timbre global décalé"),
    ("stft_", "structure temps-fréquence étrangère aux références", "structure temps-fréquence étrangère aux références"),
]


def piste(nom, z):
    for debut, haut, bas in PISTES:
        if nom.startswith(debut):
            return haut if z > 0 else bas
    return ""


# ---------------------------------------------------------------- sensibilité à la prise de son

def _biquad_plateau(x, fc, gain_db, aigu):
    """Plateau du premier ordre, stable et simple : suffit pour une correction douce."""
    sos = butter(1, fc, btype="high" if aigu else "low", fs=FS, output="sos")
    return x + (10 ** (gain_db / 20) - 1) * sosfilt(sos, x)


def _reverberer(x, rt60, humide_db, graine=7):
    rng = np.random.default_rng(graine)
    n = int(rt60 * FS)
    t = np.arange(n) / FS
    ir = rng.standard_normal(n) * 10 ** (-3 * t / rt60)
    ir = sosfilt(butter(1, 6000, fs=FS, output="sos"), ir)
    ir /= np.sqrt((ir**2).sum())
    humide = sfft.irfft(sfft.rfft(x, len(x) + n) * sfft.rfft(ir, len(x) + n))[: len(x)]
    return x + 10 ** (humide_db / 20) * humide


def _compresser(x, ratio=3.0, attaque=0.005, relache=0.08):
    a = np.exp(-1 / (attaque * FS))
    r = np.exp(-1 / (relache * FS))
    niveau = np.abs(x)
    env = np.zeros(len(x))
    e = float(niveau[:200].mean())
    for i, v in enumerate(niveau):
        e = (a if v > e else r) * e + (1 - (a if v > e else r)) * v
        env[i] = e
    reference = np.sqrt((x**2).mean()) * 1.5
    gain = np.minimum((np.maximum(env, 1e-9) / reference) ** (1 / ratio - 1), 1.0)
    return x * gain


def _bruit_rose(n, graine=11):
    rng = np.random.default_rng(graine)
    s = sfft.rfft(rng.standard_normal(n))
    s /= np.sqrt(np.maximum(sfft.rfftfreq(n, 1 / FS), 20.0))
    b = sfft.irfft(s, n=n)
    return b / np.sqrt((b**2).mean())


PERTURBATIONS = {
    "grave +4 dB": lambda x: _biquad_plateau(x, 200, 4, False),
    "grave -4 dB": lambda x: _biquad_plateau(x, 200, -4, False),
    "aigu +4 dB": lambda x: _biquad_plateau(x, 4000, 4, True),
    "aigu -4 dB": lambda x: _biquad_plateau(x, 4000, -4, True),
    "passe-haut 80 Hz": lambda x: sosfilt(butter(2, 80, btype="high", fs=FS, output="sos"), x),
    "passe-bas 10 kHz": lambda x: sosfilt(butter(4, 10000, fs=FS, output="sos"), x),
    "réverbération légère": lambda x: _reverberer(x, 0.25, -10),
    "réverbération marquée": lambda x: _reverberer(x, 0.6, -6),
    "compression 3:1": _compresser,
    "saturation douce": lambda x: np.tanh(2 * x / (np.abs(x).max() + 1e-9)) * np.abs(x).max() / 2,
    "bruit de fond -40 dB": lambda x: x + _bruit_rose(len(x)) * np.sqrt((x**2).mean()) * 0.01,
}


def _mesurer_perturbe(args):
    x, nom = args
    return descripteurs(PERTURBATIONS[nom](x))


def en_parallele(fn, travaux):
    travaux = list(travaux)
    with ProcessPoolExecutor(max_workers=max((os.cpu_count() or 2) - 1, 1)) as pool:
        return list(pool.map(fn, travaux, chunksize=max(len(travaux) // 32, 1)))


# ---------------------------------------------------------------- référentiel et décision

N_MIN_VALIDES = 4  # sous ce nombre de références valides, un descripteur n'est pas jugé


def t_seuil(n):
    return float(spstats.t.ppf(0.995, max(n - 1, 1)))


class Referentiel:
    """Un ensemble de références mesurées, et comment y situer un nouveau segment.

    Les distances Log-STFT dépendent de l'ensemble de comparaison : elles sont donc recalculées pour
    chaque sous-ensemble, et jugées seulement du côté « trop loin ».
    """

    def __init__(self, noms, mesures, sigma_capture=None):
        self.noms = noms
        self.mesures = mesures
        self.cles = sorted({c for m in mesures for c in m["valeurs"]})
        self.dist = [f"stft_grain_{n}" for n in RESOLUTIONS] + ["stft_forme"]
        self.F = np.array([[m["valeurs"].get(c, np.nan) for c in self.cles] for m in mesures])
        self.colonnes = self.cles + self.dist
        self.sigma = np.zeros(len(self.colonnes)) if sigma_capture is None else sigma_capture

    # --- distances
    def _centres_vecteurs(self, idx):
        grain = {n: np.nanmedian([self.mesures[i]["grain"][n] for i in idx], axis=0) for n in RESOLUTIONS}
        forme = np.nanmedian([self.mesures[i]["forme"] for i in idx], axis=0)
        return grain, forme

    def _distances(self, m, centres):
        grain, forme = centres
        d = [distance_vecteur(m["grain"][n], grain[n]) for n in RESOLUTIONS]
        d.append(distance_vecteur(m["forme"], forme))
        return np.log(np.maximum(np.array(d), 1e-3))

    def _vecteur(self, m, idx, memo=None):
        """Valeurs d'un segment face au sous-ensemble idx."""
        base = np.array([m["valeurs"].get(c, np.nan) for c in self.cles])
        cle = tuple(idx)
        if memo is not None and cle in memo:
            centres = memo[cle]
        else:
            centres = self._centres_vecteurs(idx)
            if memo is not None:
                memo[cle] = centres
        return np.concatenate([base, self._distances(m, centres)])

    def _table(self, idx, memo):
        """Valeurs de chaque réf de idx, vues depuis les autres réfs de idx."""
        lignes = []
        for i in idx:
            autres = [j for j in idx if j != i]
            lignes.append(self._vecteur(self.mesures[i], autres, memo))
        return np.array(lignes)

    def dispersion(self):
        """Écart-type de chaque descripteur entre les références, chacune vue depuis les autres."""
        tous = list(range(len(self.mesures)))
        table = np.array([self._vecteur(self.mesures[i], [j for j in tous if j != i]) for i in tous])
        return np.nanstd(table, axis=0, ddof=1)

    # --- écarts
    def _centre_echelle(self, table):
        n_val = np.sum(np.isfinite(table), axis=0)
        centre = np.nanmedian(table, axis=0)
        ecart = np.sqrt(np.nanmean((table - centre) ** 2, axis=0) * n_val / np.maximum(n_val - 1, 1))
        mad = 1.4826 * np.nanmedian(np.abs(table - centre), axis=0)
        s = np.maximum(ecart, mad)
        predit = np.sqrt(s**2 * (1 + 1 / np.maximum(n_val, 1)) + self.sigma**2 + 1e-9)
        return centre, predit, n_val

    def _ecarts(self, x, centre, echelle, n_val):
        z = (x - centre) / echelle
        z[n_val < N_MIN_VALIDES] = np.nan
        # Distances : seul « trop loin » a un sens.
        k = len(self.cles)
        z[k:] = np.maximum(z[k:], 0)
        return z

    def evaluer(self, m, idx=None, memo=None):
        """Écarts, seuils et sévérités du segment m face aux références idx (toutes par défaut)."""
        if idx is None:
            idx = list(range(len(self.mesures)))
        memo = {} if memo is None else memo
        table = self._table(idx, memo)
        centre, echelle, n_val = self._centre_echelle(table)
        z = self._ecarts(self._vecteur(m, idx, memo), centre, echelle, n_val)
        # Bruit de fond : écart de chaque réf de idx face aux autres, avec une échelle calculée sans elle.
        fond = np.zeros((len(idx), len(z)))
        for a, i in enumerate(idx):
            autres = [j for j in idx if j != i]
            sous_table = self._table(autres, memo)
            c, e, nv = self._centre_echelle(sous_table)
            fond[a] = np.abs(self._ecarts(self._vecteur(self.mesures[i], autres, memo), c, e, nv))
        n_ref = np.maximum(n_val, 2)
        seuil_t = np.array([t_seuil(v) for v in n_ref])
        pire = np.nan_to_num(np.nanmax(fond, axis=0))
        seuil = np.maximum(seuil_t, pire)
        return {
            "z": z,
            "severite": np.abs(z) / seuil,
            "seuil": seuil,
            "fond": pire,
            "centre": centre,
            "echelle": echelle,
            "table": table,
            "x": self._vecteur(m, idx, memo),
        }

    def loo(self, memo=None):
        """Chaque réf jugée par les autres, seuils compris."""
        memo = {} if memo is None else memo
        tous = list(range(len(self.mesures)))
        return [self.evaluer(self.mesures[i], [j for j in tous if j != i], memo) for i in tous]


def scores_familles(colonnes, severite):
    """Par famille : moyenne des deux pires descripteurs. Un seul écart isolé est moins parlant."""
    sortie = {}
    for ligne in FAMILLES:
        v = np.array([s for c, s in zip(colonnes, severite) if famille_de(c) == ligne[0] and np.isfinite(s)])
        if len(v):
            haut = np.sort(v)[::-1]
            sortie[ligne[0]] = float(haut[:2].mean())
    return sortie


def score_global(familles):
    """Moyenne des trois familles les plus touchées, hors spectre moyen : il faut plusieurs points de vue."""
    v = np.sort([s for nom, s in familles.items() if nom != FAMILLE_SPECTRE])[::-1]
    return float(v[:3].mean())


SEUIL_GLOBAL = 1.0  # au-dessus : au moins une famille sort franchement, ou plusieurs de peu
SEUIL_NET = 1.5  # calé sur de vrais démarreurs absents des références (voir l'en-tête)


def verdict_global(score):
    if score >= SEUIL_NET:
        return "HORS PLAGE"
    return "limite" if score >= SEUIL_GLOBAL else "dans la plage"


# ---------------------------------------------------------------- étalonnage

def _signal_module(f, fm, db, duree=2.0):
    t = np.arange(int(duree * FS)) / FS
    x = np.sin(2 * np.pi * f * t) * (1 + np.sin(2 * np.pi * fm * t))
    x /= np.sqrt((x**2).mean())
    return x * P0 * 10 ** (db / 20)


def etalonner():
    t = np.arange(int(1.0 * FS)) / FS
    ton = np.sin(2 * np.pi * 1000 * t)
    ton *= P0 * 10 ** (40 / 20) / np.sqrt((ton**2).mean())
    _, n_spec = excitation_et_sonie(ton)
    print(f"Sonie 1 kHz 40 dB : {n_spec.sum(axis=1).mean() / 2:.3f} sone (attendu 1)")
    rng = np.random.default_rng(1)
    bruit = rng.standard_normal(FS)
    sp = sfft.rfft(bruit)
    f = sfft.rfftfreq(FS, 1 / FS)
    sp[(f < 920) | (f > 1080)] = 0
    bruit = sfft.irfft(sp, n=FS)
    bruit *= P0 * 10 ** (60 / 20) / np.sqrt((bruit**2).mean())
    _, n_spec = excitation_et_sonie(bruit)
    print(f"Netteté, bruit 1 kHz à bande étroite, 60 dB : {nettete_din(n_spec).mean():.3f} acum (attendu 1)")
    print("Rugosité, 1 kHz 60 dB, en fonction de la modulation (attendu : maximum à 70 Hz, 1 asper) :")
    for fm in (10, 30, 50, 70, 100, 150, 250, 400):
        print(f"   {fm:4d} Hz : {rugosite(_signal_module(1000, fm, 60))[0]:.3f} asper")
    print("Fluctuation, 1 kHz 60 dB, en fonction de la modulation (attendu : maximum à 4 Hz, 1 vacil) :")
    for fm in (1, 2, 4, 8, 16, 32):
        e, _ = excitation_et_sonie(_signal_module(1000, fm, 60, 4.0))
        print(f"   {fm:4d} Hz : {fluctuation(e):.3f} vacil")


# ---------------------------------------------------------------- contrôles synthétiques

def _passe_bande(x, bas, haut):
    return sosfilt(butter(2, [bas, haut], btype="band", fs=FS, output="sos"), x)


def _resonateurs(x, frequences, q):
    """Banc de résonateurs parallèles à Q fixe."""
    sortie = np.zeros_like(x)
    for f in frequences:
        w0 = 2 * np.pi * f / FS
        r = np.exp(-w0 / (2 * q))
        a1, a2 = -2 * r * np.cos(w0), r * r
        sos = np.array([[1 - r, 0, 0, 1, a1, a2]])
        y = sosfilt(sos, x)
        sortie += y / (np.sqrt((y**2).mean()) + 1e-12)
    return sortie


def controles_synthetiques(duree=0.8, graine=2026):
    """Les défauts entendus par le passé, sous forme caricaturale. Chaque nom dit ce qu'il imite."""
    rng = np.random.default_rng(graine)
    n = int(duree * FS)
    t = np.arange(n) / FS
    sortie = {}
    sortie["aspirateur_bruit_module"] = rng.standard_normal(n) * (1 + 0.8 * np.sin(2 * np.pi * 12 * t))
    # Sinusoïde dont seule la hauteur balance : le volume ne bouge pas du tout.
    inst = 620 * (1 + 0.15 * np.sin(2 * np.pi * 9 * t))
    sortie["cd_sinus_glissant"] = np.sin(2 * np.pi * np.cumsum(inst) / FS)
    # Version plus riche : le spectre est large, mais les raies glissent ensemble sans battre.
    riche = sum(np.sin(2 * np.pi * k * np.cumsum(inst) / FS) / k for k in range(1, 9))
    sortie["cd_harmoniques"] = riche + 0.03 * rng.standard_normal(n)
    # Impulsions aléatoires filtrées.
    chocs = np.zeros(n)
    for i in rng.integers(0, n, int(duree * 35)):
        chocs[i] = rng.uniform(0.2, 1) * rng.choice([-1, 1])
    decroit = np.exp(-np.arange(int(0.006 * FS)) / (0.0015 * FS))
    sortie["cailloux_impulsions"] = _passe_bande(np.convolve(chocs, decroit)[:n], 1500, 7000)
    # Bruit dans des résonances très étroites, à peine vivant.
    entree = rng.standard_normal(n + FS // 4)
    sortie["ballon_resonances"] = _resonateurs(entree, [430, 690, 1130, 1870, 2610], 180)[FS // 4:]
    # Clics réguliers et rapides : fermeture éclair.
    clics = np.zeros(n)
    clics[np.arange(0, n, int(FS / 95))] = 1
    clics *= rng.uniform(0.5, 1.0, n)
    sortie["eclair_clics"] = _passe_bande(np.convolve(clics, decroit)[:n], 800, 9000)
    # Modes inharmoniques amortis, frappés à 10 Hz : métal.
    frappes = np.zeros(n)
    frappes[np.arange(int(0.01 * FS), n, int(FS / 10))] = 1
    modes = sum(np.sin(2 * np.pi * f * np.arange(int(0.12 * FS)) / FS) * np.exp(-np.arange(int(0.12 * FS)) / (0.025 * FS))
                for f in (1830, 2970, 4230, 5610, 7090))
    sortie["metal_frappes"] = np.convolve(frappes, modes)[:n]
    # Balayage lent de raies pures, très vibrées : vaisseau.
    vol = 300 * 2 ** (t / duree) * (1 + 0.02 * np.sin(2 * np.pi * 35 * t))
    sortie["vaisseau_raies"] = sum(np.sin(2 * np.pi * r * np.cumsum(vol) / FS) for r in (1, 1.51, 2.37, 3.11))
    return {k: v / (np.abs(v).max() + 1e-12) * 0.5 for k, v in sortie.items()}


def apparier_spectre(x, cible_db, f_cible):
    """Donne à x le spectre moyen voulu (lissé) : un défaut que le spectre moyen ne peut plus trahir."""
    n = len(x)
    s = sfft.rfft(x)
    f = sfft.rfftfreq(n, 1 / FS)
    p = np.abs(s) ** 2
    bords = np.geomspace(40, 16000, 37)
    mesure = np.array([p[(f >= a) & (f < b)].mean() if ((f >= a) & (f < b)).any() else np.nan
                       for a, b in zip(bords[:-1], bords[1:])])
    centres = np.sqrt(bords[:-1] * bords[1:])
    actuel = np.interp(np.log(np.maximum(f, 1)), np.log(centres), 10 * np.log10(np.nan_to_num(mesure) + 1e-12))
    voulu = np.interp(np.log(np.maximum(f, 1)), np.log(f_cible), cible_db)
    # Limité : on ne fabrique pas d'énergie là où le signal n'en a aucune.
    gain = np.clip(voulu - actuel, -60, 40)
    gain = gain - np.median(gain)
    y = sfft.irfft(s * 10 ** (gain / 20), n=n)
    return y / (np.abs(y).max() + 1e-12) * 0.5


def spectre_lisse_moyen(segments):
    """Spectre moyen des références en dB relatif, en tiers d'octave."""
    bords = np.geomspace(40, 16000, 37)
    centres = np.sqrt(bords[:-1] * bords[1:])
    profils = []
    for x in segments:
        p = np.abs(sfft.rfft(x)) ** 2 / len(x)
        f = sfft.rfftfreq(len(x), 1 / FS)
        v = np.array([p[(f >= a) & (f < b)].mean() + 1e-16 for a, b in zip(bords[:-1], bords[1:])])
        profils.append(10 * np.log10(v / v.sum()))
    return np.median(profils, axis=0), centres


def phases_aleatoires(x, graine=3):
    """Même spectre à long terme, plus aucune structure temporelle : le pire cas pour un spectre moyen."""
    rng = np.random.default_rng(graine)
    s = sfft.rfft(x)
    return sfft.irfft(np.abs(s) * np.exp(2j * np.pi * rng.random(len(s))), n=len(x))


# ---------------------------------------------------------------- sorties texte

def fmt(v):
    if not np.isfinite(v):
        return "   n/a"
    return f"{v:6.3f}" if abs(v) < 10 else f"{v:6.1f}"


def rapport_segment(nom, ref, ev, reference_globale, tout=False, nb=14):
    colonnes = ref.colonnes
    familles = scores_familles(colonnes, ev["severite"])
    score = score_global(familles)
    bas, haut = reference_globale
    print(f"\n=== {nom} : {verdict_global(score)}  (score {score:.2f} ; limite à {SEUIL_GLOBAL:.1f}, hors plage à "
          f"{SEUIL_NET:.1f} ; références en leave-one-out : {bas:.2f} à {haut:.2f})")
    print("    Familles : " + ", ".join(f"{k} {v:.2f}" for k, v in sorted(familles.items(), key=lambda kv: -kv[1])))
    ordre = np.argsort(-np.nan_to_num(ev["severite"]))
    hors = int(np.nansum(ev["severite"] >= 1))
    print(f"    {hors} descripteurs hors plage sur {int(np.isfinite(ev['severite']).sum())}"
          f" ({int(np.isnan(ev['severite']).sum())} non jugés, segments trop courts)")
    print(f"    {'descripteur':26s}{'valeur':>8s}  {'plage des réf.':>17s}{'écart':>8s}{'fond':>6s}{'sévér.':>7s}  verdict")
    for k in ordre:
        s = ev["severite"][k]
        if not np.isfinite(s) or (not tout and (k not in ordre[:nb])):
            continue
        t = ev["table"][:, k]
        if np.isfinite(t).sum() == 0:
            continue
        valeur = ev["x"][k]
        verdict = "hors plage" if s >= 1 else "dans la plage"
        ligne = (f"    {colonnes[k]:26s}{fmt(valeur):>8s}  [{fmt(np.nanmin(t))} ; {fmt(np.nanmax(t))}]"
                 f"{ev['z'][k]:+8.1f}{ev['fond'][k]:6.1f}{s:7.2f}  {verdict}")
        if s >= 1:
            ligne += " : " + piste(colonnes[k], ev["z"][k])
        print(ligne)
    return familles, score


# ---------------------------------------------------------------- planche visuelle

def planche(chemin, lignes, ref, evaluations):
    """lignes : (nom, signal, mesure, type) ; côte à côte : spectrogramme, modulation, familles."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    n = len(lignes)
    fig, axes = plt.subplots(n, 3, figsize=(17, 1.75 * n + 0.8), squeeze=False, width_ratios=[5, 3, 2.4])
    duree_max = max(len(s) / FS for _, s, _, _ in lignes)
    duree_vue = min(duree_max, 1.0)
    bandes = MODS_LENTES + MODS
    for ligne, (nom, signal, mesure, genre) in zip(axes, lignes):
        ax = ligne[0]
        sig = signal[: int(duree_vue * FS)]
        sig = sig / (np.sqrt((sig**2).mean()) + 1e-12)
        f, t, s = spectrogram(sig, FS, nperseg=512, noverlap=512 - 64, window="hann")
        s_db = 10 * np.log10(s + 1e-14)
        ax.pcolormesh(t, f, s_db, vmin=-85, vmax=-30, shading="auto", cmap="Greys", rasterized=True)
        ax.set_ylim(0, 8000)
        ax.set_xlim(0, duree_vue)
        ax.set_ylabel("Hz", fontsize=7)
        ax.tick_params(labelsize=7)
        couleur = {"ref": "#333333", "faible": "#7a6a00", "rendu": "#b5121b"}[genre]
        ax.set_title(f"{nom}  ({genre})", fontsize=8, loc="left", color=couleur, fontweight="bold" if genre == "rendu" else None)
        ax = ligne[1]
        im = ax.imshow(mesure["matrice"], origin="lower", aspect="auto", vmin=-40, vmax=0, cmap="Blues", interpolation="nearest")
        ax.set_xticks(range(len(bandes)))
        ax.set_xticklabels([f"{a:g}-{b:g}" for a, b in bandes], fontsize=6, rotation=45)
        ax.set_yticks(range(len(OCTAVES)))
        ax.set_yticklabels([f"{o}" for o in OCTAVES], fontsize=6)
        ax = ligne[2]
        familles = scores_familles(ref.colonnes, evaluations[nom]["severite"])
        noms = [c[0] for c in FAMILLES if c[0] in familles]
        valeurs = [familles[c] for c in noms]
        y = np.arange(len(noms))
        couleurs = ["#b5121b" if v >= 1 else "#8a8f99" for v in valeurs]
        ax.barh(y, np.minimum(valeurs, 5), color=couleurs, height=0.6)
        ax.axvline(1, color="#222222", lw=0.8)
        ax.set_yticks(y)
        ax.set_yticklabels(noms, fontsize=6)
        ax.invert_yaxis()
        ax.set_xlim(0, 5)
        ax.tick_params(axis="x", labelsize=6)
        for cote in ("top", "right"):
            ax.spines[cote].set_visible(False)
    axes[-1][0].set_xlabel("temps (s), fenêtres de 12 ms, niveau normalisé", fontsize=7)
    axes[-1][1].set_xlabel("modulation (Hz) ; en ordonnée, octave acoustique (Hz) ; indice en dB, de -40 à 0", fontsize=7)
    axes[-1][2].set_xlabel("sévérité par famille (trait : limite de la plage ; coupé à 5)", fontsize=7)
    fig.tight_layout()
    fig.savefig(chemin, dpi=110)
    plt.close(fig)
    print(f"\nPlanche écrite : {chemin}")


# ---------------------------------------------------------------- orchestration

def sigma_capture(ref, signaux, colonnes):
    """σ_capture de chaque descripteur : déplacement typique quand seule la prise de son change."""
    travaux = [(x, nom) for x in signaux for nom in PERTURBATIONS]
    mesures = en_parallele(_mesurer_perturbe, travaux)
    k = len(PERTURBATIONS)
    deplacements = np.full((len(PERTURBATIONS), len(signaux), len(colonnes)), np.nan)
    memo = {}
    tous = list(range(len(signaux)))
    for i in range(len(signaux)):
        autres = [j for j in tous if j != i]
        base = ref._vecteur(ref.mesures[i], autres, memo)
        for p in range(k):
            deplacements[p, i] = ref._vecteur(mesures[i * k + p], autres, memo) - base
    typique = np.nanmedian(np.abs(deplacements), axis=1)  # perturbation x descripteur
    sigma = np.sqrt(np.nanmean(typique**2, axis=0))
    return np.nan_to_num(sigma), mesures, deplacements


def tableau_validation(ref, loo, fond_scores, signaux, mesures_pert, deplacements, controles):
    print("\n--- Leave-one-out : chaque vraie référence jugée par les autres ---")
    for nom, ev, score in zip(ref.noms, loo, fond_scores):
        familles = scores_familles(ref.colonnes, ev["severite"])
        pire = max(familles, key=familles.get)
        ordre = np.argsort(-np.nan_to_num(ev["severite"]))[:3]
        pires = ", ".join(f"{ref.colonnes[k]} {ev['severite'][k]:.2f}" for k in ordre)
        print(f"  {nom:20s} {score:5.2f}  {verdict_global(score):14s} pire famille {pire} {familles[pire]:.2f} ; {pires}")

    print("\n--- Sensibilité : réf. légèrement dégradée, jugée par les autres ---")
    k = len(PERTURBATIONS)
    memo = {}
    tous = list(range(len(signaux)))
    for p, nom in enumerate(PERTURBATIONS):
        scores = []
        for i in tous:
            ev = ref.evaluer(mesures_pert[i * k + p], [j for j in tous if j != i], memo)
            scores.append(score_global(scores_familles(ref.colonnes, ev["severite"])))
        bouge = np.nanmedian(np.abs(deplacements[p]), axis=0) / (ref.dispersion() + 1e-9)
        pires = ", ".join(ref.colonnes[j] for j in np.argsort(-np.nan_to_num(bouge))[:3])
        print(f"  {nom:24s} score médian {np.median(scores):.2f}, pire {np.max(scores):.2f}, "
              f"{sum(s >= SEUIL_GLOBAL for s in scores)}/{len(scores)} au-dessus de la limite ; bouge : {pires}")

    print("\n--- Stabilité et pouvoir de discrimination de chaque descripteur ---")
    print("    instab. = σ_capture / dispersion des réfs (au-dessus de 1 : la prise de son pèse plus que")
    print("    la différence entre deux voitures). contrôles = sévérité médiane et minimale sur les contrôles.")
    dispersion = ref.dispersion()
    sev_ctrl = np.array([c["severite"] for c in controles.values()])
    mediane = np.nanmedian(sev_ctrl, axis=0)
    minimum = np.nanmin(sev_ctrl, axis=0)
    part = np.nanmean(sev_ctrl >= 1, axis=0)
    ordre = np.argsort(-np.nan_to_num(mediane))
    print(f"    {'descripteur':26s}{'instab.':>8s}{'fond':>6s}{'contrôles méd.':>16s}{'min':>6s}{'% hors':>8s}")
    for k2 in ordre:
        print(f"    {ref.colonnes[k2]:26s}{ref.sigma[k2] / (dispersion[k2] + 1e-9):8.2f}"
              f"{np.nanmax(np.abs(np.array([e['z'][k2] for e in loo]))):6.1f}"
              f"{mediane[k2]:16.2f}{minimum[k2]:6.2f}{100 * part[k2]:8.0f}")


def main(argv=None):
    p = argparse.ArgumentParser(description="Compare un rendu à des références, avec des descripteurs perceptifs.")
    p.add_argument("--ref", nargs="+", default=[], help="vrais enregistrements, chemin.wav:debut:fin")
    p.add_argument("--ref-faible", nargs="*", default=[], help="références moins fiables (sons générés) : jugées, jamais utilisées pour la plage")
    p.add_argument("--rendu", nargs="*", default=[], help="segments à juger, chemin.wav:debut:fin")
    p.add_argument("--png", help="planche visuelle côte à côte")
    p.add_argument("--tout", action="store_true", help="afficher tous les descripteurs")
    p.add_argument("--nb", type=int, default=14, help="descripteurs affichés par segment")
    p.add_argument("--sans-capture", action="store_true", help="ne pas mesurer la sensibilité à la prise de son (plus rapide, plages trop étroites)")
    p.add_argument("--valider", action="store_true", help="contrôles synthétiques, leave-one-out et sensibilité")
    p.add_argument("--controles", help="dossier où écrire et lire les signaux synthétiques de contrôle")
    p.add_argument("--etalonner", action="store_true", help="vérifie sonie, netteté, rugosité et fluctuation sur leurs signaux de référence")
    args = p.parse_args(argv)

    if args.etalonner:
        etalonner()
        if not args.ref:
            return
    if len(args.ref) < 4:
        p.error("il faut au moins 4 références (--ref)")

    signaux = [lire_segment(s) for s in args.ref]
    noms = [nom_court(s) for s in args.ref]
    print(f"{len(signaux)} références, de {min(len(x) for x in signaux) / FS:.2f} à {max(len(x) for x in signaux) / FS:.2f} s.")
    mesures = en_parallele(descripteurs, signaux)
    ref = Referentiel(noms, mesures)

    mesures_pert = deplacements = None
    if args.valider or not args.sans_capture:
        sigma, mesures_pert, deplacements = sigma_capture(ref, signaux, ref.colonnes)
        ref.sigma = sigma

    memo = {}
    loo = ref.loo(memo)
    fond_scores = [score_global(scores_familles(ref.colonnes, ev["severite"])) for ev in loo]
    bornes = (min(fond_scores), max(fond_scores))
    print(f"Bruit de fond : références en leave-one-out, score de {bornes[0]:.2f} à {bornes[1]:.2f} (limite {SEUIL_GLOBAL:.1f}, hors plage {SEUIL_NET:.1f}).")

    a_juger = [(s, "rendu") for s in args.rendu] + [(s, "faible") for s in args.ref_faible]
    sig_j = [lire_segment(s) for s, _ in a_juger]
    mes_j = en_parallele(descripteurs, sig_j) if sig_j else []
    evaluations = {}
    for (s, genre), m in zip(a_juger, mes_j):
        nom = nom_court(s)
        evaluations[nom] = ref.evaluer(m, memo=memo)
        rapport_segment(f"{nom} ({genre})", ref, evaluations[nom], bornes, args.tout, args.nb)

    if args.valider:
        synth = controles_synthetiques()
        cible, f_cible = spectre_lisse_moyen(signaux)
        for nom in list(synth):
            synth[nom + "__apparie"] = apparier_spectre(synth[nom], cible, f_cible)
        for nom, x in zip(noms, signaux):
            synth[f"phases_aleatoires_{nom}"] = phases_aleatoires(x)
        if args.controles:
            os.makedirs(args.controles, exist_ok=True)
            for nom, x in synth.items():
                wavfile.write(os.path.join(args.controles, nom + ".wav"), FS, (np.clip(x, -1, 1) * 32767).astype(np.int16))
        mes_c = en_parallele(descripteurs, list(synth.values()))
        evals_c = {nom: ref.evaluer(m, memo=memo) for nom, m in zip(synth, mes_c)}
        print("\n--- Contrôles synthétiques (doivent sortir nettement) ---")
        print("    « __apparie » : même défaut, mais avec le spectre moyen des références. ")
        print("    « phases_aleatoires » : une vraie référence, même spectre exact, temporalité détruite.")
        for nom, ev in evals_c.items():
            familles = scores_familles(ref.colonnes, ev["severite"])
            score = score_global(familles)
            tri = sorted(familles.items(), key=lambda kv: -kv[1])[:4]
            print(f"  {nom:36s} {score:6.2f}  {verdict_global(score):14s} " + ", ".join(f"{k} {v:.1f}" for k, v in tri))
        sous_reverb = en_parallele(_mesurer_perturbe, [(x, "réverbération marquée") for x in synth.values()])
        print("\n--- Les mêmes contrôles sous réverbération marquée : un défaut noyé reste-t-il visible ? ---")
        for nom, m in zip(synth, sous_reverb):
            if not nom.startswith("phases"):
                score = score_global(scores_familles(ref.colonnes, ref.evaluer(m, memo=memo)["severite"]))
                print(f"  {nom:36s} {score:6.2f}  {verdict_global(score)}")
        if sig_j:
            print("\n--- Segments jugés, passés dans la même prise de son : leur verdict tient-il ? ---")
            travaux = [(x, nom) for x, (s, g) in zip(sig_j, a_juger) if g == "rendu" for nom in PERTURBATIONS]
            mes_p = en_parallele(_mesurer_perturbe, travaux)
            for (x, nom), m in zip(travaux, mes_p):
                score = score_global(scores_familles(ref.colonnes, ref.evaluer(m, memo=memo)["severite"]))
                indice = next(i for i, y in enumerate(sig_j) if y is x)
                print(f"  {nom_court(a_juger[indice][0]):28s} {nom:24s} {score:5.2f}  {verdict_global(score)}")
        tableau_validation(ref, loo, fond_scores, signaux, mesures_pert, deplacements, evals_c)

    if args.png:
        lignes = []
        evaluations_png = dict(evaluations)
        for (s, genre), x, m in zip(a_juger, sig_j, mes_j):
            lignes.append((nom_court(s), x, m, genre))
        for nom, x, m, ev in zip(noms, signaux, mesures, loo):
            evaluations_png[nom] = ev
            lignes.append((nom, x, m, "ref"))
        planche(args.png, lignes, ref, evaluations_png)


if __name__ == "__main__":
    main()
