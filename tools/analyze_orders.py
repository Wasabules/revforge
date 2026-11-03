#!/usr/bin/env python3
"""Les rangs harmoniques d'un régime tenu, en ordres de l'arbre (1 = un tour par rotation) : ce qui distingue deux moteurs de même cadence.

  tools/analyze_orders.py --rpm 4000 a.wav b.wav

Fenêtres courtes (0,5 s) moyennées en puissance : le régime dérive, une longue fenêtre étalerait les raies aiguës.
Affiche le niveau de chaque ordre en dB sous le plus fort, tous les 1/6 d'ordre : tiers et demi-ordres y tombent tous.
Le bruit de fond, la médiane des creux entre les ordres, donne ce qui dépasse vraiment.
"""
import argparse
import sys
from fractions import Fraction

import numpy as np
from scipy.io import wavfile


def read(path):
    rate, data = wavfile.read(path)
    data = data.astype(np.float64)
    if data.ndim > 1:
        data = data.mean(axis=1)
    return rate, data / 32768.0


def spectrum(data, rate, window_s=0.5):
    size = int(window_s * rate)
    hop = size // 2
    win = np.hanning(size)
    power = np.zeros(size // 2 + 1)
    count = 0
    # On laisse passer la première seconde : le fondu du départ.
    for start in range(rate, len(data) - size, hop):
        power += np.abs(np.fft.rfft(data[start : start + size] * win)) ** 2
        count += 1
    if count == 0:
        raise SystemExit('trop court : il faut au moins 1,5 s')
    return np.fft.rfftfreq(size, 1.0 / rate), power / count


def levels(path, rpm, top):
    rate, data = read(path)
    freqs, power = spectrum(data, rate)
    shaft = rpm / 60.0
    step = freqs[1] - freqs[0]
    out = {}
    orders = [Fraction(k, 6) for k in range(1, top * 6 + 1)]
    for order in orders:
        center = float(order) * shaft
        reach = max(2.0 * step, 0.015 * center)
        band = (freqs >= center - reach) & (freqs <= center + reach)
        out[order] = 10.0 * np.log10(power[band].max() + 1e-20)
    best = max(out.values())
    return {order: value - best for order, value in out.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--rpm', type=float, required=True, help="le régime tenu de l'arbre")
    parser.add_argument('--top', type=int, default=8, help="dernier ordre affiché")
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    columns = [levels(path, args.rpm, args.top) for path in args.files]
    sys.stdout.write(f"{'ordre':>6} {'Hz':>7}" + ''.join(f' {path.replace(chr(92), "/").rsplit("/", 1)[-1][:16]:>17}' for path in args.files) + '\n')
    for order in columns[0]:
        text = f'{order.numerator}/{order.denominator}' if order.denominator != 1 else str(order.numerator)
        hz = float(order) * args.rpm / 60.0
        sys.stdout.write(f'{text:>6} {hz:7.1f}' + ''.join(f' {column[order]:17.1f}' for column in columns) + '\n')
    # Ce qui résume le mieux : le niveau moyen des familles de rangs, entre 2 et le dernier ordre.
    for name, test in (('entiers', lambda o: o.denominator == 1), ('demis', lambda o: o.denominator == 2), ('tiers', lambda o: o.denominator == 3)):
        sys.stdout.write(f'{name:>14}' + ''.join(f' {np.mean([v for o, v in column.items() if test(o) and o >= 2]):17.1f}' for column in columns) + '\n')


if __name__ == '__main__':
    main()
