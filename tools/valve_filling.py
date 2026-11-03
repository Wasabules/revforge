#!/usr/bin/env python3
"""
D'où viennent les constantes de `core/src/valvetrain.cpp` : le passage du gaz par les soupapes, intégré en
quasi-stationnaire, sans bibliothèque. `python tools/valve_filling.py`.

Admission. Le cylindre aspire de l'air à 1 bar, soupape ouverte comme dans le son (240° de vilebrequin), à travers une
section efficace en sinus, en écoulement compressible d'orifice. Un clapet remplace l'inertie du gaz : à haut régime,
la seule zone où ce calcul compte, l'air continue d'entrer après le point mort bas et ne ressort pas. On lit la masse
enfermée, sans résonance ni réglage de came : c'est la pure restriction. Résumé par
fill(Z) = (1 + (Z / Zc)^4)^(-1/4), où Z est l'indice de Mach d'admission de Taylor sur la section efficace moyenne.

Échappement. Le cylindre s'ouvre à 130° après l'explosion sur 5 bar et 1250 K, et se vide à travers sa section : soit
il se vide trop tôt (de la détente perdue), soit trop tard (le piston pousse contre ce qui reste). On lit ce travail
perdu, en bar de pression moyenne, à comparer d'un Z à l'autre : c'est la table `kExhaustLossTable`.
"""
import math

COMPRESSION = 10.5
ROD_RATIO = 1.6


def orifice_flux(gamma, p_up, p_down):
    """Débit par unité de section efficace, ramené à ρ·a de l'amont : écoulement d'orifice compressible."""
    ratio = max(p_down / p_up, 0.0)
    critical = (2 / (gamma + 1)) ** (gamma / (gamma - 1))
    if ratio < critical:
        return (2 / (gamma + 1)) ** ((gamma + 1) / (2 * (gamma - 1)))
    return math.sqrt(max(2 / (gamma - 1) * (ratio ** (2 / gamma) - ratio ** ((gamma + 1) / gamma)), 0.0))


def volume(deg):
    """Volume du cylindre à `deg` degrés du point mort haut, en parts de cylindrée."""
    a = math.radians(deg)
    crank = 0.5
    rod = crank * 2 * ROD_RATIO
    return 1 / (COMPRESSION - 1) + crank * (1 - math.cos(a)) + rod - math.sqrt(rod * rod - (crank * math.sin(a)) ** 2)


def filling(z):
    """
    Masse enfermée / masse de la cylindrée, pour Z = Ap·Sp / (Aeff·a0). Une course dure S/Sp, donc en unités de cylindrée
    Aeff·a0·dt = dθ / (180·Z). La section suit sin(π·x), de moyenne 1 : son pic vaut π/2.
    """
    gamma = 1.4
    start = -10.0
    steps = 2400
    d_theta = 240.0 / steps
    p = 1.0
    entered = 0.0
    v = volume(start)
    for k in range(steps):
        deg = start + (k + 0.5) * d_theta
        section = math.pi / 2 * math.sin(math.pi * (k + 0.5) / steps)
        v_next = volume(deg + d_theta / 2)
        dm = section * d_theta / (180.0 * z) * orifice_flux(gamma, 1.0, p) if p < 1.0 else 0.0
        p += (gamma * dm - gamma * p * (v_next - v)) / max(v, 1e-6)
        entered += dm
        v = v_next
    return entered


def exhaust_loss(z, p_open=5.0, t_open=1250.0):
    """Travail perdu (bar de pression moyenne), pour Z = Ap·Sp / (Aeff·a), a étant le son du gaz à l'ouverture."""
    gamma = 1.33
    start = 130.0
    end = 370.0
    steps = 4800
    d_theta = (end - start) / steps
    p = p_open
    t = t_open
    v = v_open = volume(start)
    m = p * v / t
    loss = 0.0
    for k in range(steps):
        deg = start + (k + 0.5) * d_theta
        section = math.pi / 2 * math.sin(math.pi * (k + 0.5) / steps)
        v_next = volume(deg + d_theta / 2)
        dv = v_next - v
        dm = section * d_theta / (180.0 * z) * orifice_flux(gamma, p, 1.0) * (p / t) * math.sqrt(t / t_open) if p > 1.0 else 0.0
        if deg < 180.0:
            # Avant le point mort bas : ce que la détente aurait encore donné.
            loss += (p_open * (v_open / (v + 0.5 * dv)) ** gamma - p) * dv
        else:
            # Après : ce que le piston doit pousser.
            loss += (p - 1.0) * -dv
        energy = p * v / (gamma - 1) - p * dv - dm * t * gamma / (gamma - 1)
        m -= dm
        v = v_next
        p = max(energy * (gamma - 1) / v, 1e-3)
        t = p * v / max(m, 1e-9)
    return loss


def fit(z, zc, n):
    return (1 + (z / zc) ** n) ** (-1 / n)


def main():
    points = [0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.5, 2.0]
    full = filling(0.05)
    print(" Z    intégré  fill(Z)")
    for z in points:
        print(f"{z:4.1f}   {filling(z) / full:6.3f}   {fit(z, 0.8, 4.0):6.3f}")
    best = min(((max(abs(filling(z) / full - fit(z, zc / 100, n)) for z in points), zc / 100, n) for n in (2.0, 3.0, 4.0, 5.0, 6.0) for zc in range(60, 100, 5)))
    print(f"meilleur ajustement : Zc = {best[1]:.2f}, n = {best[2]:.0f}, pire écart {best[0]:.3f}")
    print("limite étranglée : fill·Z =", round(filling(4.0) / full * 4.0, 3))

    print("\nkExhaustLossTable, de 0 à 1,2 par pas de 0,05 :")
    base = exhaust_loss(0.01)
    table = [0.0] + [exhaust_loss(0.05 * k) - base for k in range(1, 25)]
    print(", ".join(f"{value:.3f}" for value in table))


if __name__ == "__main__":
    main()
