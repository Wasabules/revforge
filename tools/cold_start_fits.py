#!/usr/bin/env python3
"""Les constantes du démarrage à froid (core/src/cold_start.cpp), refaites d'ici : python tools/cold_start_fits.py

- les lois de Vogel des huiles (KV40, KV100, limite CCS de la SAE J300 à la marge de la référence) ;
- l'essence d'hiver en neuf corps purs : distillation, tension de vapeur, ce qui se vaporise dans l'air froid ;
- la batterie calée sur l'essai EN 50342-1 (Shepherd, charge qui baisse) ;
- l'exposant de la viscosité au lancement, face à la table d'Exide.
Demande numpy et scipy, comme les autres outils.
"""
import math

from scipy.optimize import brentq, fsolve, minimize_scalar


# ── Les huiles ──

def density(rho15, t):
    return rho15 * (1.0 - 6.5e-4 * (t - 15.6))


def vogel_through(points):
    (t1, m1), (t2, m2), (t3, m3) = points

    def gap(c):
        b12 = math.log(m1 / m2) / (1.0 / (t1 + c) - 1.0 / (t2 + c))
        b23 = math.log(m2 / m3) / (1.0 / (t2 + c) - 1.0 / (t3 + c))
        return b12 - b23

    c = brentq(gap, max(-t for t, _ in points) + 1.0, 2000.0)
    b = math.log(m1 / m2) / (1.0 / (t1 + c) - 1.0 / (t2 + c))
    return m1 / math.exp(b / (t1 + c)), b, c


def walther(kv40, kv100, t):
    """ASTM D341 : log log (ν + 0,7) = A − B log T, pour une monograde minérale."""
    z = lambda v: math.log10(math.log10(v + 0.7))
    t1, t2 = math.log10(313.15), math.log10(373.15)
    b = (z(kv40) - z(kv100)) / (t2 - t1)
    a = z(kv40) + b * t1
    return 10 ** (10 ** (a - b * math.log10(t + 273.15))) - 0.7


def oils():
    margin = 4250.0 / 6600.0  # la référence (Motorcraft 5W-30) sous sa limite CCS de -30 °C
    grades = {
        "0W-20": (44.0, 8.3, 0.845, (-35.0, 6200.0)),
        "10W-40": (95.0, 14.3, 0.865, (-25.0, 7000.0)),
        "15W-40": (108.0, 14.6, 0.880, (-20.0, 7000.0)),
        "20W-50": (165.0, 18.5, 0.885, (-15.0, 9500.0)),
        "SAE 30": (100.0, 11.0, 0.885, None),
    }
    print("Vogel μ = a·exp(b/(T + c)), Pa·s et °C")
    for name, (kv40, kv100, rho15, ccs) in grades.items():
        p40 = (40.0, kv40 * 1e-6 * density(rho15, 40.0) * 1e3)
        p100 = (100.0, kv100 * 1e-6 * density(rho15, 100.0) * 1e3)
        if ccs is None:
            low = (-10.0, walther(kv40, kv100, -10.0) * 1e-6 * density(rho15, -10.0) * 1e3)
        else:
            low = (ccs[0], ccs[1] * margin * 1e-3)
        a, b, c = vogel_through([low, p40, p100])
        print("  %-7s a = %.6e, b = %.4f, c = %.4f" % (name, a, b, c))


# ── L'essence d'hiver ──

SPECIES = [  # part massique, masse molaire, Antoine (log10 mmHg, °C)
    (0.06, 58.12, (6.80896, 935.86, 238.73)),
    (0.16, 72.15, (6.78967, 1020.012, 233.097)),
    (0.07, 72.15, (6.87632, 1075.78, 233.205)),
    (0.16, 86.18, (6.87601, 1171.17, 224.41)),
    (0.17, 114.23, (6.81189, 1257.84, 220.735)),
    (0.13, 92.14, (6.95464, 1344.8, 219.482)),
    (0.11, 106.17, (6.99891, 1474.679, 213.686)),
    (0.09, 120.19, (7.04383, 1573.267, 208.564)),
    (0.05, 142.28, (6.94363, 1495.17, 193.858)),
]


def psat_bar(antoine, t):
    a, b, c = antoine
    return 10 ** (a - b / (c + t)) * 133.322 / 1e5


def fuel():
    n = [100.0 * w / m for w, m, _ in SPECIES]
    total = sum(n)
    tvp = sum(ni / total * psat_bar(s[2], 37.8) for ni, s in zip(n, SPECIES)) * 100.0
    # Distillation différentielle à 1,013 bar.
    left = list(n)
    gone = 0.0
    marks = {}
    while gone < 99.0:
        tot = sum(left)
        lo, hi = -60.0, 400.0
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            (lo, hi) = (lo, mid) if sum(x / tot * psat_bar(s[2], mid) for x, s in zip(left, SPECIES)) > 1.01325 else (mid, hi)
        t = 0.5 * (lo + hi)
        for mark in (70.0, 100.0, 150.0):
            if t <= mark:
                marks[mark] = gone
        step = 0.002 * tot
        for i, s in enumerate(SPECIES):
            take = min(left[i], left[i] / tot * psat_bar(s[2], t) / 1.01325 * step)
            left[i] -= take
            gone += take * s[1] / 100.0
    print("Essence d'hiver : E70 %.0f %%, E100 %.0f %%, E150 %.0f %%, tension de vapeur vraie à 37,8 °C %.0f kPa" % (marks[70.0], marks[100.0], marks[150.0], tvp))


# ── La batterie ──

def battery():
    tau, x_end, amps_per_ah, empty = 60.0, 0.96, 9.0, 0.5

    def run(a, vk, s, dt=0.01):
        capacity = 3600.0 / amps_per_ah
        q, charge = 0.0, 1.0

        def volts(i):
            x = min(q / (s * max(charge, 0.05)), 0.98)
            return 11.8 + 0.9 * charge - i * a * (1.0 + empty * (1.0 - charge)) - vk * x / (1.0 - x)

        def step(i):
            nonlocal q, charge
            charge -= i * dt / capacity
            q = max(q + (i - q / tau) * dt, 0.0)

        for _ in range(int(10.0 / dt)):
            step(1.0)
        v10 = volts(1.0)
        for _ in range(int(10.0 / dt)):
            step(0.0)
        t3 = 0.0
        while volts(0.6) > 6.0 and t3 < 400:
            step(0.6)
            t3 += dt
        return v10, t3, q / (s * max(charge, 0.05))

    a, vk, s = fsolve(lambda v: [run(*v)[0] - 7.5, run(*v)[1] - 73.0, run(*v)[2] - x_end], [5.1, 0.15, 30.0])
    print("Batterie (EN 50342-1) : I·R(-18 °C) = %.3f V, V_K = %.3f V, charge à fort débit = %.2f s × I" % (a, vk, s))


# ── L'exposant de la viscosité au lancement ──

def exide():
    a, b, c = 3.476900e-5, 1345.5610, 144.8707
    mu = lambda t: a * math.exp(b / (t + c))
    compression, base, hydro = 4.3, 1.5 * 9.96, 0.001406 * 150.0  # citadine à 150 tr/min (Nm)
    join = math.sqrt(mu(80.0) / mu(100.0))

    def total(t, n):
        e = math.sqrt(mu(t) / mu(100.0)) - 1.0 if t >= 80.0 else join * (mu(t) / mu(80.0)) ** n - 1.0
        return compression + base + hydro * (1.0 + e)

    points = [(0.0, 1.55), (-18.0, 2.10), (-35.6, 3.50)]
    err = lambda n: sum((math.log(total(t, n) / total(27.0, n)) - math.log(r)) ** 2 for t, r in points)
    n = minimize_scalar(err, bounds=(0.3, 1.5), method="bounded").x
    print("Exposant au lancement, face à la table d'Exide : %.2f" % n)


if __name__ == "__main__":
    oils()
    fuel()
    battery()
    exide()
