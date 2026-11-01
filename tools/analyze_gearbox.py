#!/usr/bin/env python3
"""Mesure un sifflement de boîte : usage analyze_gearbox.py fichier.wav:début:fin:f1min:f1max [...]

Suit la raie la plus forte de [f1min, f1max] en fenêtres de 46 ms, puis la regarde le long de sa trajectoire. Une
fenêtre longue étale une raie qui bouge et la fait passer pour faible : tout est mesuré en fenêtres courtes.

`niveau/fond` : hauteur de chaque harmonique au-dessus du fond voisin.
`battement` : de combien son niveau bouge d'une trame à l'autre, tendance lente (0,5 s) retirée. Une sinusoïde : 0.
`relatif` : niveau moyen, rapporté au premier harmonique suivi.
`jupe/raie` : énergie à ±8-150 Hz autour du pic, rapportée à celle du pic, sur 370 ms. Une sinusoïde : très négatif.
`raies/bande` : part de l'énergie de chaque bande qui tient dans les raies.

Ces mesures disent si la structure ressemble à un vrai engrènement, jamais si cela sonne juste.
Sur de vraies marches arrière : harmoniques à environ -3 dB par rang, battement de 2,6 à 4,5 dB, jupe/raie de -2 à +4 dB.
"""
import sys

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import stft


def load(path):
    rate, x = wavfile.read(path)
    x = x.astype(np.float64) / 32768.0
    return rate, (x.mean(axis=1) if x.ndim > 1 else x)


def track(power, f, low, high):
    """Le pic le plus fort, en restant proche de la trame précédente, dans les deux sens depuis sa trame la plus forte."""
    keep = np.where((f >= low) & (f <= high))[0]
    start = keep[np.argmax(power[keep].mean(axis=1))]
    reach = max(2, int(25 / (f[1] - f[0])))
    out = np.zeros(power.shape[1], dtype=int)
    middle = int(np.argmax(power[start]))
    for frames in (range(middle, power.shape[1]), range(middle, -1, -1)):
        at = start if frames.start == middle and frames.step == 1 else out[middle]
        for t in frames:
            window = np.arange(max(at - reach, keep[0]), min(at + reach + 1, keep[-1] + 1))
            at = window[np.argmax(power[window, t])]
            out[t] = at
    return out


def refined(z, ridge, df):
    """Position du pic à mieux que la largeur d'un point, par une parabole."""
    freq = np.zeros(len(ridge))
    for i, k in enumerate(ridge):
        k = int(np.clip(k, 1, z.shape[0] - 2))
        a, b, c = np.log(np.abs(z[k - 1:k + 2, i]) + 1e-20)
        freq[i] = (k + 0.5 * (a - c) / (a - 2 * b + c + 1e-12)) * df
    return freq


def harmonics(power, f, t, line):
    df = f[1] - f[0]
    step = t[1] - t[0]
    span = max(3, int(0.5 / step))
    base = None
    print("  h   f médian   niveau/fond   battement   relatif")
    for h in range(1, 9):
        fh = line * h
        levels, floors = [], []
        for i, hz in enumerate(fh):
            k = int(round(hz / df))
            if k < 16 or k + 17 >= power.shape[0]:
                continue
            levels.append(power[k - 2:k + 3, i].max())
            floors.append(np.median(np.r_[power[k - 16:k - 6, i], power[k + 7:k + 17, i]]))
        if len(levels) < 2 * span:
            break
        levels, floors = np.array(levels), np.array(floors)
        db = 10 * np.log10(levels + 1e-20)
        trend = np.convolve(db, np.ones(span) / span, "same")
        if base is None:
            base = db.mean()
        print(f"  {h}   {np.median(fh):7.0f} Hz  {np.median(10 * np.log10(levels / (floors + 1e-20))):7.1f} dB  {(db - trend)[span:-span].std():7.1f} dB  {db.mean() - base:+7.1f} dB")


def skirt(seg, rate, centre):
    n = 16384
    window = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / rate)
    span = int(300 / (f[1] - f[0]))
    curves = []
    for i in range(0, len(seg) - n, n // 2):
        s = np.abs(np.fft.rfft(seg[i:i + n] * window)) ** 2
        keep = np.where((f > centre * 0.9) & (f < centre * 1.1))[0]
        k = keep[np.argmax(s[keep])]
        if k - span >= 0 and k + span < len(s):
            curves.append(s[k - span:k + span + 1] / s[k])
    if not curves:
        return float("nan")
    c = np.mean(curves, axis=0)
    freq = np.arange(-span, span + 1) * (f[1] - f[0])
    return 10 * np.log10(c[(np.abs(freq) > 8) & (np.abs(freq) < 150)].sum() / c[np.abs(freq) <= 8].sum())


def bands(power, f, ridge):
    lines = np.zeros(power.shape)
    for i, k0 in enumerate(ridge):
        for h in range(1, 13):
            k = int(round(k0 * h))
            if k + 2 >= power.shape[0]:
                break
            lines[max(k - 2, 0):k + 3, i] = power[max(k - 2, 0):k + 3, i]
    total = power.mean(axis=1)
    held = lines.mean(axis=1)
    print(f"  raies/total : {10 * np.log10(held.sum() / total.sum() + 1e-20):+.1f} dB")
    for low, high in [(500, 1000), (1000, 2000), (2000, 4000), (4000, 8000)]:
        keep = (f >= low) & (f < high)
        print(f"  {low:5d} à {high:5d} Hz : raies/bande {10 * np.log10(held[keep].sum() / total[keep].sum() + 1e-20):+6.1f} dB, bande/total {10 * np.log10(total[keep].sum() / total.sum() + 1e-20):+6.1f} dB")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for item in sys.argv[1:]:
        path, start, end, low, high = item.rsplit(":", 4)
        rate, x = load(path)
        seg = x[int(float(start) * rate):int(float(end) * rate)]
        f, t, z = stft(seg, rate, nperseg=2048, noverlap=2048 - 256, window="hann")
        power = np.abs(z) ** 2
        ridge = track(power, f, float(low), float(high))
        line = refined(z, ridge, f[1] - f[0])
        centre = float(np.median(line))
        print(f"{path.rsplit('/', 1)[-1]} [{start}-{end} s] : raie vers {centre:.0f} Hz")
        harmonics(power, f, t, line)
        print(f"  jupe/raie vers {centre:.0f} Hz : {skirt(seg, rate, centre):+.1f} dB ; vers {2 * centre:.0f} Hz : {skirt(seg, rate, 2 * centre):+.1f} dB")
        bands(power, f, ridge)
    return 0


if __name__ == "__main__":
    sys.exit(main())
