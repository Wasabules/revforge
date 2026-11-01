#!/usr/bin/env python3
"""Mesure ce qu'une oreille reproche à un rendu, pour le comparer à de vrais moteurs.

Usage : analyze_sound.py [--png sortie.png] fichier.wav [...]

« Fermeture éclair » : des raies parallèles aussi fortes jusqu'à plusieurs kHz, là où, sur un
vrai moteur, elles s'effacent peu à peu vers les aigus. `peigne` mesure ces raies, en dB au-dessus
du fond entre 1 et 3 kHz. `relief` mesure les bosses fixes de la ligne sous 1 kHz.
"""
import argparse
import sys

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import butter, hilbert, sosfiltfilt, spectrogram, welch

EDGES = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
WINDOW_S = 0.25


def read(path):
    rate, x = wavfile.read(path)
    x = x.astype(float)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return rate, x / (np.abs(x).max() + 1e-9)


def loudest_windows(x, rate, share=0.3):
    """Les fenêtres les plus fortes : le moteur sous charge, pas le silence autour."""
    n = int(WINDOW_S * rate)
    starts = range(0, len(x) - n, n)
    levels = [(np.sqrt((x[s : s + n] ** 2).mean()), s) for s in starts]
    levels.sort(reverse=True)
    # Répartir sur toute la durée : au plus 40 fenêtres, pour rester rapide sur les longs fichiers.
    chosen = levels[: max(1, int(len(levels) * share))]
    step = max(1, len(chosen) // 40)
    return [x[s : s + n] for _, s in chosen[::step]]


def band(x, rate, low, high):
    sos = butter(4, [low, min(high, rate * 0.45)], btype="band", fs=rate, output="sos")
    return sosfiltfilt(sos, x)


def envelope_beat(w, rate):
    """Battement de l'enveloppe des aigus : 1 = clics parfaitement réguliers, 0 = souffle."""
    env = np.abs(hilbert(band(w, rate, 2000, 7000)))
    sos = butter(2, 1500, fs=rate, output="sos")
    env = sosfiltfilt(sos, env)
    depth = env.std() / (env.mean() + 1e-12)
    env = env - env.mean()
    spectrum = np.fft.rfft(env, 2 * len(env))
    corr = np.fft.irfft(np.abs(spectrum) ** 2)[: len(env)]
    corr /= corr[0] + 1e-12
    low, high = int(rate * 0.0015), int(rate * 0.03)
    return corr[low:high].max(), depth


def comb_db(w, rate, low=1000, high=3000):
    """Hauteur des raies au-dessus du fond : un bruit donne environ 8 dB."""
    spectrum = np.abs(np.fft.rfft(w * np.hanning(len(w)))) ** 2
    freq = np.fft.rfftfreq(len(w), 1 / rate)
    part = spectrum[(freq >= low) & (freq < high)]
    return 10 * np.log10(np.percentile(part, 98) / (np.median(part) + 1e-20) + 1e-12)


def relief_db(w, rate):
    """Écart-type du spectre lissé sous 1 kHz, une fois retirée la pente : les bosses de la ligne."""
    freq, power = welch(w, rate, nperseg=1024)
    keep = (freq >= 60) & (freq < 1000)
    db = 10 * np.log10(power[keep] + 1e-20)
    slope = np.polyval(np.polyfit(np.log(freq[keep]), db, 1), np.log(freq[keep]))
    return (db - slope).std()


def crest(w):
    return np.abs(w).max() / (np.sqrt((w**2).mean()) + 1e-12)


def octaves(w, rate):
    freq, power = welch(w, rate, nperseg=4096)
    total = power.sum() + 1e-20
    return [10 * np.log10(power[(freq >= EDGES[i]) & (freq < EDGES[i + 1])].sum() / total + 1e-12) for i in range(len(EDGES) - 1)]


def measure(path):
    rate, x = read(path)
    windows = loudest_windows(x, rate)
    beats = [envelope_beat(w, rate) for w in windows]
    return {
        "peigne": float(np.median([comb_db(w, rate) for w in windows])),
        "relief": float(np.median([relief_db(w, rate) for w in windows])),
        "battement": float(np.median([b[0] for b in beats])),
        "profondeur": float(np.median([b[1] for b in beats])),
        "crête": float(np.median([crest(w) for w in windows])),
        "crête HF": float(np.median([crest(band(w, rate, 1000, 8000)) for w in windows])),
        "octaves": np.mean([octaves(w, rate) for w in windows], axis=0),
    }


def plot(paths, target):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(len(paths), 2, figsize=(14, 2.6 * len(paths)), squeeze=False, width_ratios=[3, 2])
    for row, path in zip(axes, paths):
        rate, x = read(path)
        f, t, s = spectrogram(x, rate, nperseg=2048, noverlap=1536)
        row[0].pcolormesh(t, f, 10 * np.log10(s + 1e-12), vmin=-100, vmax=-30, shading="auto", cmap="magma")
        row[0].set_ylim(0, 3000)
        row[0].set_title(path.rsplit("/", 1)[-1], fontsize=9, loc="left")
        w = loudest_windows(x, rate, 0.05)[0]
        span = int(rate * 0.06)
        row[1].plot(np.arange(span) / rate * 1000, w[: span], lw=0.7)
        row[1].set_xlabel("ms", fontsize=8)
    fig.tight_layout()
    fig.savefig(target, dpi=80)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--png")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()
    print(f"{'':28s} peigne relief batt. crête | " + " ".join(f"{e:>5}" for e in EDGES[:-1]))
    for path in args.files:
        m = measure(path)
        name = path.rsplit("/", 1)[-1][:28]
        bands = " ".join(f"{v:5.0f}" for v in m["octaves"])
        print(f"{name:28s} {m['peigne']:6.1f} {m['relief']:6.1f} {m['battement']:5.2f} {m['crête']:5.1f} | {bands}")
    if args.png:
        plot(args.files, args.png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
