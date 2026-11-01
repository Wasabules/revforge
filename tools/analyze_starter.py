#!/usr/bin/env python3
"""Mesure un démarreur : usage analyze_starter.py fichier.wav:début:fin [...]

`ondulation` : de combien la raie d'engrènement balance à chaque compression, en %. C'est ce qui fait
qu'un démarreur « s'exprime ». Il faut des fenêtres courtes : une longue étale une raie qui bouge, et la
fait passer pour faible.
`raies` : hauteur des raies au-dessus du fond, sur ces fenêtres courtes.
`graves` : énergie sous 250 Hz, par rapport aux médiums (500 à 2000 Hz), en dB.
`aigus` : énergie au-dessus de 4 kHz, par rapport aux mêmes médiums, en dB.
`contraste` : écart de volume entre les bouffées et les creux, en dB. Un démarreur est d'abord
un volume qui bat ; un volume constant dont seule la hauteur balance sonne comme un disque freiné à la main.
"""
import sys

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import butter, sosfiltfilt, stft


def ridge(seg, rate, low=350, high=1100):
    """Suit la raie d'engrènement : le pic le plus fort, en restant proche de la trame précédente."""
    f, _, z = stft(seg, rate, nperseg=1024, noverlap=1024 - 128)
    keep = (f >= low) & (f < high)
    power = np.abs(z[keep]) ** 2
    freq = f[keep]
    start = np.argmax(power.mean(axis=1))
    track = []
    at = start
    for frame in power.T:
        reach = np.abs(np.arange(len(freq)) - at) <= 2
        at = np.argmax(frame * reach)
        track.append(freq[at])
    return np.array(track), f, z


def measure(seg, rate):
    track, f, z = ridge(seg, rate)
    smooth = np.convolve(track, np.ones(5) / 5, "valid")
    swing = (np.percentile(smooth, 92) - np.percentile(smooth, 8)) / np.median(smooth) * 100
    power = np.abs(z) ** 2
    band = (f >= 400) & (f < 3000)
    lines = 10 * np.log10(np.percentile(power[band], 98, axis=0) / (np.median(power[band], axis=0) + 1e-20))
    total = power.mean(axis=1)
    mid = total[(f >= 500) & (f < 2000)].sum()
    low = total[(f >= 40) & (f < 250)].sum()
    top = total[(f >= 4000) & (f < 10000)].sum()
    return {
        "mesh": float(np.median(smooth)),
        "swing": float(swing),
        "lines": float(np.median(lines)),
        "low": float(10 * np.log10(low / mid + 1e-12)),
        "top": float(10 * np.log10(top / mid + 1e-12)),
    }


def rhythm(seg, rate):
    env = np.abs(sosfiltfilt(butter(4, [150, 6000], btype="band", fs=rate, output="sos"), seg))
    env = sosfiltfilt(butter(2, 70, fs=rate, output="sos"), env)
    step = rate // 1000
    e = env[::step] - env[::step].mean()
    corr = np.fft.irfft(np.abs(np.fft.rfft(e, 2 * len(e))) ** 2)[: len(e)]
    corr /= corr[0] + 1e-12
    lo, hi = 33, min(250, len(corr) - 1)
    lag = lo + np.argmax(corr[lo:hi])
    return 1000 / lag, env.std() / env.mean()


def contrast(seg, rate):
    n = int(rate * 0.005)
    levels = np.array([np.sqrt((seg[i : i + n] ** 2).mean()) for i in range(0, len(seg) - n, n)]) + 1e-6
    return 20 * np.log10(np.percentile(levels, 90) / np.percentile(levels, 10))


def main():
    print(f"{'':30s} engrèn.  ondul.  raies  graves  aigus  rythme  prof.  contraste")
    for item in sys.argv[1:]:
        path, start, end = item.rsplit(":", 2)
        rate, x = wavfile.read(path)
        x = x.astype(float) / 32768
        if x.ndim > 1:
            x = x.mean(axis=1)
        seg = x[int(float(start) * rate) : int(float(end) * rate)]
        m = measure(seg, rate)
        hz, depth = rhythm(seg, rate)
        print(f"{path.rsplit('/', 1)[-1][:30]:30s} {m['mesh']:5.0f} Hz {m['swing']:5.0f} % {m['lines']:5.1f} {m['low']:7.0f} {m['top']:6.0f}  {hz:5.1f} {depth:5.2f} {contrast(seg, rate):7.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
