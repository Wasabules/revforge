#!/usr/bin/env python3
"""Transforme presets/presets.json, source unique des préréglages, en table C++."""
import json
import sys

LAYOUT = {"inline": "Inline", "v": "V", "cross_plane": "CrossPlane", "boxer": "Boxer"}
EXHAUST = {"stock": "Stock", "sport": "Sport", "straight": "Straight"}
ASPIRATION = {"natural": "Natural", "turbo": "Turbo"}
FUEL = {"petrol": "Petrol", "diesel": "Diesel"}
POPS = {"off": "Off", "light": "Light", "strong": "Strong"}
RELEASE = {"dump_valve": "DumpValve", "flutter": "Flutter"}


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
            f"TurboRelease::{RELEASE[p.get('turboRelease', 'dump_valve')]}}}"
        )
        electric = "true" if p.get("electric") else "false"
        lines.append(f"{{{text(p['id'])}, {text(p['name'])}, {text(p['description'])}, {config}, {electric}}},")
    with open(target, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
