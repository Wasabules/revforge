#!/usr/bin/env python3
"""Transforme presets/presets.json, source unique des préréglages, en table C++."""
import json
import sys

LAYOUT = {"inline": "Inline", "v": "V", "cross_plane": "CrossPlane", "boxer": "Boxer", "rotary": "Rotary", "vr": "Vr", "w": "W"}
EXHAUST = {
    "stock": "Stock", "sport": "Sport", "straight": "Straight",
    "no_muffler": "MufflerDelete", "no_resonator": "ResonatorDelete", "quiet": "Quiet", "expansion": "Expansion",
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
CAMSHAFT = {"dohc": "Dohc", "sohc": "Sohc", "ohv": "Ohv"}
AIR_FILTER = {"stock": "Stock", "panel": "Panel", "cone": "Cone", "stacks": "Stacks"}
FUEL_SYSTEM = {"injection": "Injection", "single": "Carburetor", "multi": "MultiCarb", "direct": "Direct"}
# Les réglages du socle moteur : mêmes valeurs que les tableaux de web/src/lib/config.ts.
START_TEMP = {"warm": "Warm", "cold": "Cold"}
FUEL_GRADE = {"ron95": "Ron95", "ron98": "Ron98", "race102": "Race102", "e85": "E85", "methanol": "Methanol"}
REV_LIMITER = {"stock": "Stock", "soft": "Soft", "ignition_cut": "IgnitionCut"}
TRACTION = {"off": "Off", "sport": "Sport", "full": "Full"}
DIESEL_INJECTION = {"mechanical": "Mechanical", "common_rail": "CommonRail"}
VALVE_TIMING = {"fixed": "Fixed", "phased": "Phased", "two_step": "TwoStep", "phased_two_step": "PhasedTwoStep"}
VALVE_SPRINGS = {"stock": "Stock", "stiff": "Stiff"}
TURBO_LAYOUT = {"single": "Single", "twin_parallel": "TwinParallel", "twin_sequential": "TwinSequential", "twincharger": "Twincharger"}
BOOST_CONTROL = {"spring": "Spring", "electronic": "Electronic", "by_gear": "ByGear"}
INTERCOOLER = {"air_to_air": "AirToAir", "air_to_water": "AirToWater", "none": "None"}
CYCLE = {"four_stroke": "FourStroke", "two_stroke": "TwoStroke"}
# L'huile du carter (démarreur) : mêmes valeurs que OIL_GRADES de web/src/lib/config.ts.
OIL_GRADE = {"5w30": "Sae5W30", "0w20": "Sae0W20", "10w40": "Sae10W40", "15w40": "Sae15W40", "20w50": "Sae20W50", "sae30": "Sae30"}
BODY = {"city": "City", "compact": "Compact", "coupe": "Coupe", "supercar": "Supercar", "van": "Van", "retro": "Retro"}
DRIVE = {"front": "Front", "rear": "Rear", "all": "All"}
GEARBOX = {"synchro": "Synchro", "dog": "Dog", "sequential": "Sequential", "dual_clutch": "DualClutch", "converter": "Converter", "reducer": "Reducer"}
TEETH = {"helical": "Helical", "spur": "Spur"}
FAMILY = {"matched": "Matched", "short": "Short", "close": "Close", "long": "Long", "custom": "Custom"}
FLYWHEEL = {"single": "Single", "dual_mass": "DualMass"}


def text(value):
    return json.dumps(value, ensure_ascii=False)


def real(value):
    return f"{float(value)}f"


def flag(value):
    return "true" if value else "false"


def tuning(p):
    """Les réglages du socle, dans l'ordre d'EngineConfig : un champ absent prend le défaut, le moteur d'aujourd'hui."""
    return (
        f"{real(p.get('ambientC', 20))}, {int(p.get('altitudeM', 0))}, StartTemp::{START_TEMP[p.get('startTemp', 'warm')]}, "
        f"{real(p.get('compressionRatio', 0))}, FuelGrade::{FUEL_GRADE[p.get('fuelGrade', 'ron95')]}, "
        f"{int(p.get('ignitionOffsetDeg', 0))}, {real(p.get('fullLoadLambda', 0))}, RevLimiter::{REV_LIMITER[p.get('revLimiter', 'stock')]}, "
        f"{int(p.get('launchRpm', 0))}, {flag(p.get('flatShift', False))}, TractionControl::{TRACTION[p.get('tractionControl', 'off')]}, "
        f"DieselInjection::{DIESEL_INJECTION[p.get('dieselInjection', 'mechanical')]}, {flag(p.get('egr', False))}, {flag(p.get('particulateFilter', False))}, "
        f"{int(p.get('camDurationDeg', 0))}, {real(p.get('camLiftMm', 0))}, {int(p.get('lobeSeparationDeg', 0))}, "
        f"ValveTiming::{VALVE_TIMING[p.get('valveTiming', 'fixed')]}, ValveSprings::{VALVE_SPRINGS[p.get('valveSprings', 'stock')]}, "
        f"TurboLayout::{TURBO_LAYOUT[p.get('turboLayout', 'single')]}, {flag(p.get('variableGeometry', False))}, "
        f"BoostControl::{BOOST_CONTROL[p.get('boostControl', 'spring')]}, Intercooler::{INTERCOOLER[p.get('intercooler', 'air_to_air')]}, "
        f"{flag(p.get('waterInjection', False))}, {int(p.get('nitrousKw', 0))}, EngineCycle::{CYCLE[p.get('cycle', 'four_stroke')]}, "
        f"OilGrade::{OIL_GRADE[p.get('oilGrade', '5w30')]}"
    )


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
            f"{p.get('turboPipeMm', 400)}, Camshaft::{CAMSHAFT[p.get('camshaft', 'dohc')]}, AirFilter::{AIR_FILTER[p.get('airFilter', 'stock')]}, "
            f"FuelSystem::{FUEL_SYSTEM[p.get('fuelSystem', 'injection')]}, {tuning(p)}}}"
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
