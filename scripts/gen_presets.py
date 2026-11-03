#!/usr/bin/env python3
"""Transforme presets/presets.json, source unique des préréglages, en table C++."""
import json
import sys

LAYOUT = {"inline": "Inline", "v": "V", "cross_plane": "CrossPlane", "boxer": "Boxer", "rotary": "Rotary"}
EXHAUST = {
    "stock": "Stock", "sport": "Sport", "straight": "Straight",
    "no_muffler": "MufflerDelete", "no_resonator": "ResonatorDelete", "quiet": "Quiet",
}
ROUTING = {"single": "Single", "dual": "Dual", "h": "HPipe", "x": "XPipe"}
PLACEMENT = {"front": "Front", "mid": "Mid", "rear": "Rear"}
INSULATION = {"stripped": "Stripped", "light": "Light", "standard": "Standard", "comfort": "Comfort", "luxury": "Luxury"}
ASPIRATION = {"natural": "Natural", "turbo": "Turbo", "supercharger": "Supercharger"}
FUEL = {"petrol": "Petrol", "diesel": "Diesel"}
POPS = {"off": "Off", "light": "Light", "strong": "Strong"}
RELEASE = {"dump_valve": "DumpValve", "flutter": "Flutter"}
TURBO_SIZE = {"small": "Small", "medium": "Medium", "large": "Large"}
SUPERCHARGER = {"roots": "Roots", "twin_screw": "TwinScrew", "centrifugal": "Centrifugal"}
ANTILAG = {"off": "Off", "retard": "Retard", "air": "AirInjection", "cut": "CylinderCut"}
CAMSHAFT = {"dohc": "Dohc", "sohc": "Sohc"}
AIR_FILTER = {"stock": "Stock", "panel": "Panel", "cone": "Cone", "stacks": "Stacks"}
BODY = {"city": "City", "compact": "Compact", "coupe": "Coupe", "supercar": "Supercar", "van": "Van"}
DRIVE = {"front": "Front", "rear": "Rear", "all": "All"}
GEARBOX = {"synchro": "Synchro", "dog": "Dog", "sequential": "Sequential", "dual_clutch": "DualClutch", "converter": "Converter", "reducer": "Reducer"}
TEETH = {"helical": "Helical", "spur": "Spur"}
FAMILY = {"matched": "Matched", "short": "Short", "close": "Close", "long": "Long", "custom": "Custom"}
FLYWHEEL = {"single": "Single", "dual_mass": "DualMass"}


def text(value):
    return json.dumps(value, ensure_ascii=False)


def main(source, target):
    with open(source, encoding="utf-8") as f:
        presets = json.load(f)
    lines = ["// Généré par scripts/gen_presets.py : ne pas modifier à la main."]
    for p in presets:
        config = (
            f"{{{p.get('cylinders', 4)}, Layout::{LAYOUT[p.get('layout', 'inline')]}, "
            f"{p.get('displacementCc', 2000)}, {p.get('boreMm', 84)}, {p.get('redlineRpm', 6800)}, "
            f"Exhaust::{EXHAUST[p.get('exhaust', 'stock')]}, Aspiration::{ASPIRATION[p.get('aspiration', 'natural')]}, "
            f"Fuel::{FUEL[p.get('fuel', 'petrol')]}, 0.0f, PopLevel::{POPS[p.get('pops', 'off')]}, "
            f"TurboRelease::{RELEASE[p.get('turboRelease', 'dump_valve')]}, {p.get('boostBar', 1.0)}f, "
            f"ExhaustRouting::{ROUTING[p.get('routing', 'single')]}, TurboSize::{TURBO_SIZE[p.get('turboSize', 'medium')]}, "
            f"SuperchargerKind::{SUPERCHARGER[p.get('superchargerKind', 'roots')]}, AntiLag::{ANTILAG[p.get('antiLag', 'off')]}, "
            f"{p.get('turboPipeMm', 400)}, Camshaft::{CAMSHAFT[p.get('camshaft', 'dohc')]}, AirFilter::{AIR_FILTER[p.get('airFilter', 'stock')]}}}"
        )
        electric = "true" if p.get("electric") else "false"
        # Pas de valeur par défaut pour la voiture : chaque préréglage la donne en entier.
        car = (
            f"{{Body::{BODY[p['body']]}, {p['massKg']}, {p['rimInch']}, Drive::{DRIVE[p['drive']]}, "
            f"EnginePlacement::{PLACEMENT[p['placement']]}, Insulation::{INSULATION[p['insulation']]}}}"
        )
        # La boîte : tout est optionnel, un champ absent laisse le choix au constructeur (zéro).
        ratios = ", ".join(f"{float(r)}f" for r in str(p.get("boxRatios", "")).split(",") if r)
        gearbox = (
            f"{{GearboxKind::{GEARBOX[p.get('boxKind', 'synchro')]}, {p.get('boxGears', 0)}, "
            f"GearFamily::{FAMILY[p.get('boxFamily', 'matched')]}, {{{ratios}}}, {float(p.get('boxFinal', 0))}f, "
            f"ToothForm::{TEETH[p.get('boxTeeth', 'helical')]}, {float(p.get('boxHelix', 0))}f, {float(p.get('boxModule', 0))}f, "
            f"{float(p.get('boxLimit', 0))}f, Flywheel::{FLYWHEEL[p.get('boxFlywheel', 'single')]}}}"
        )
        lines.append(f"{{{text(p['id'])}, {text(p['name'])}, {text(p['description'])}, {config}, {electric}, {car}, {gearbox}}},")
    with open(target, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
