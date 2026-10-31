#!/usr/bin/env python3
"""Compare deux rendus : écart échantillon par échantillon, et spectre par octave."""
import sys

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import welch

EDGES = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]


def bands(x, rate):
    freq, power = welch(x, rate, nperseg=4096)
    total = power.sum() + 1e-20
    return np.array([10 * np.log10(power[(freq >= EDGES[i]) & (freq < EDGES[i + 1])].sum() / total + 1e-12) for i in range(len(EDGES) - 1)])


def main(first, second):
    rate_a, a = wavfile.read(first)
    rate_b, b = wavfile.read(second)
    if rate_a != rate_b or len(a) != len(b):
        print(f"FORMAT DIFFÉRENT : {rate_a}/{rate_b} Hz, {len(a)}/{len(b)} échantillons")
        return 1
    a = a.astype(float) / 32768
    b = b.astype(float) / 32768
    delta = a - b
    same = (delta == 0).mean() * 100
    rms = np.sqrt((a**2).mean()) + 1e-12
    err = 20 * np.log10(np.sqrt((delta**2).mean()) / rms + 1e-12)
    diverge = np.argmax(delta != 0) if (delta != 0).any() else -1
    level = abs(20 * np.log10(rms) - 20 * np.log10(np.sqrt((b**2).mean()) + 1e-12))
    band = np.abs(bands(a, rate_a) - bands(b, rate_a)).max()
    where = "-" if diverge < 0 else f"{diverge / rate_a:.2f} s"
    print(f"identiques {same:6.2f} %  écart {err:7.1f} dB  max {np.abs(delta).max():.5f}  1re diff {where:>8}  niveau ±{level:.2f} dB  octave ±{band:.2f} dB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
