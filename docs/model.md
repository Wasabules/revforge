> This is the English version of [`docs/modele.md`](modele.md), which is the reference if they differ.

# The model and its calibration

## Why simulate the gas path

A real engine is not periodic at all: each firing differs from the one before, and the exhaust is
turbulent. Two simpler models were tried and rejected by ear:

- impulses in a resonant tube: "a fart in a can" (« un pet dans une canette »);
- a sum of pure spectral lines, locked to the rotation: "a mosquito" (« un moustique »), too clean.

A third defect appeared while listening to a big V8: "a zipper being pulled up" (« une fermeture éclair qu'on monte »).
On the spectrogram, this was a fan of parallel lines, as strong at 3 kHz as at 300 Hz, and a timbre that rose with
the rpm. The real engines that were measured show something else: a few low lines, energy grouped in fixed bumps
below 600 Hz, and lines that fade gradually toward the highs. The chain described here follows from that observation, refined further down.

## What makes the car move

Nothing is entered by hand: everything starts from displacement, maximum rpm and aspiration.

1. **Torque.** A brake mean effective pressure (from 10.5 to 14.5 bar for a naturally aspirated gasoline engine,
   depending on maximum rpm), a breathing curve across the rpm range, a loss when piston speed exceeds 24 m/s, and the
   effect of the exhaust: a free-flowing line gains at the top and loses at the bottom.
2. **Turbo.** Its pressure is adjustable, above atmospheric pressure: 0.5 to 1.5 bar on the road, up to 2 bar on a
   production sports car, 3 bar and more in racing. Each bar adds 90% of a fill. The engine computer then holds torque
   constant: the plateau of turbo engines. The model ignores breakage and knock: nothing forbids 3.5 bar on a road
   engine. The time this pressure takes to arrive is computed: see "The turbo".
3. **Engine.** Inertia according to displacement, friction, pumping against a closed throttle (engine braking), a
   manifold that takes time to fill, idle control, starter.
   **Rev limiter:** combustion stops at maximum rpm and only resumes 4% lower, after at least 30 ms. The throttle stays
   open, so the engine only comes down on its friction: it bounces 7 to 15 times per second depending on the engine.
4. **Transmission.** A clutch that slips or grips depending on the torque transmitted, an input shaft and its inertias,
   driveshafts that twist, and a gearbox that is a separate part: see "The gearbox".
5. **Car.** Mass, air and rolling resistance, load transfer, and tires whose grip limits torque: an overpowered
   front-wheel-drive car spins its wheels.

The 0 to 100 shown is measured by driving this model, pedal to the floor. These figures are still those of a model:
the tests compare peak power to real engines, to within 30%.

## The turbo

`TurboCharger` (`core/src/turbo.cpp`) advances at each 1 ms step of `DrivingModel`. It holds no "lag" time constant: lag is what it takes the shaft to store its energy.

```
J·ω·dω/dt = P_turbine − P_compressor − P_bearings
```

- **Sizing.** The engine picks its turbo: the flow at the rated point (88% of redline, at the requested pressure) and the tip speed that pressure demands give the wheel diameter. The size setting multiplies that diameter by 0.85 or 1.2: turbine area follows D², inertia D⁵. More pressure asks for a bigger wheel.
- **Turbine.** Its inlet pressure follows Stodola's ellipse law, solved directly: `p3² = p4² + ṁ²·T3/K²`, where K also counts the wastegate when open. Power is the expansion of that gas, with an efficiency set by the blade speed ratio U/c (peak at 0.65). `T3` comes from the fuel burnt, slowed by the manifold walls (0.35 s rising, assumed). Gas leaves at its mean flow, but pulses add to the expansion a pressure difference that falls with revs (0.25 bar at a standstill, assumed): without it, the shaft would not turn at idle, and low revs would lack energy.
- **Compressor.** Head depends on reduced flow, after Moore and Greitzer: peak at the surge limit, positive slope on the left, a drop towards choke on the right. The air column in the duct has inertia, and the 2.5 to 14 L between compressor and throttle has to fill. Air leaves hot (`T2`, from efficiency), passes the intercooler (effectiveness 0.86 at low flow, 0.72 at rated, with thermal mass) and loses 0.12 bar there at the rated point.
- **Valves.** The wastegate, spring and diaphragm, opens around the requested pressure, with the engine computer's anticipation, and also opens if the wheels exceed the speed they can take. A blow-off valve opens under manifold vacuum when there is pressure to release, and stays open as long as air passes. Without it (flutter setting), the air comes back through the compressor: flow reverses, pressure falls, the compressor recovers, and it starts again. Surge is not scripted: it comes from the positive slope of the speed line, loaded by that volume and that air column.
- **The spec's curve** is the equilibrium of this same balance (`turboSteady`, two bisections): what you get holding the revs. A test checks the simulation reaches it. `measurePerformance` drives the real model: it includes the lag.
- **Back pressure** costs torque: whatever exceeds what a well-matched turbo would cost at the same flow. The reference torques come from real engines, which already count it: the medium turbo loses very little, a small one chokes up high, a large one relieves.

### Against published figures

| | Model | Reference |
|---|---|---|
| Shaft inertia, 2.0 L | 2.8·10⁻⁵ kg·m² (medium size) | 1.844·10⁻⁵ (rotor) + 8.786·10⁻⁶ (wheel) kg·m²: Chung and Lee, 2016, on a 2.0 L petrol (read) |
| Build-up at 2000 rpm, 2.0 L | 2.6 s held at speed, 2.3 s in 4th from 1800 rpm (revs rise) | 2785 ms on a load step at 2000 rpm: Energies 2013, 6(3), 1746 (seen quoted, could not read the article) |
| Inertia and lag | ×2 inertia, lag × about 1.6 (at 3000 rpm) | -50% inertia, -25% lag: Guarda et al., 2020 (seen quoted); TiAl, up to -13% on build-up: Park et al., 2022 (seen quoted) |
| Shaft speed | 150,000 to 175,000 rpm (medium 2.0 L), up to 290,000 (34 mm wheels) | 185,000 rpm at most on a GTX2860R Gen II: Garrett (seen on the catalogue). Within the 100,000 to 250,000 of most car turbos |
| Surge without a valve | 7 to 9 Hz on the 2.0 L with a 6.5 L volume | 5.1 and 9.6 Hz in mild surge on two compressors: Imperial College thesis, 2020 (seen quoted). Frequency depends on the plumbing |
| Compressor outlet at 2.3 bar | 140 °C | Known order of magnitude (no reference) |
| Intercooler loss | 0.12 bar at rated | Known order of magnitude (no reference), set here: not a result |

**What is assumed:** the wheel diameter of the engine Chung and Lee measured (they do not give it; we take what this calculation gives a 2.0 L at 1.3 bar); the share of gas through the turbine at rated (half, 70% on a diesel); the shape of the speed line; peak efficiencies (0.74 and 0.68); the slowness of the manifold walls; bearing friction (800 W for a 49 mm wheel at rated); the wheels' speed limit (520 m/s).

**What is not modelled:** the detail of pulses in the turbine (only their mean energy counts), variable geometry (a diesel would gain much: this one behaves like a fixed turbo), sequential twin turbos. The large turbo surges slightly at very low revs under full load: a real fault of an oversized turbo, but its level has not been compared with a measurement. On a V, the sound model and the gas see one equivalent turbo, even though the views draw two.

### Lag, engine by engine

Time to reach 90% of the established pressure, held at speed, from gentle driving:

| Engine | Size | 2000 rpm | 2500 | 3000 | 4000 | 0-100 km/h | Top speed | Peak torque | Power |
|---|---|---|---|---|---|---|---|---|---|
| 2.0 at 1.3 bar | small | 1.0 s | 0.7 s | 0.6 s | 0.4 s | 6.4 s | 254 | 350 Nm | 182 kW |
| 2.0 at 1.3 bar | medium | 2.6 s | 1.2 s | 0.9 s | 0.6 s | 6.9 s | 260 | 357 Nm | 194 kW |
| 2.0 at 1.3 bar | large | no boost | 0.3 of the boost | 2.0 s | 1.1 s | 7.3 s | 262 | 362 Nm | 201 kW |

`hot_hatch_turbo` used to reach 90% in 2.0 s at 2500 rpm and 1.6 s at 3800 rpm, with pressure already there at part load; it now takes 1.2 s and 0.6 s starting from almost nothing. Its 0-100 moves from 6.84 to 6.89 s, peak power from 198 to 194 kW.

### The air path on screen

In the gas (`fluidGas.ts`), a naturally aspirated engine keeps its simple duct. On a turbo, the intake is a real chain of computed sections: airbox (large section, where the filter brakes), inlet duct, compressor, hot pipe, intercooler (a large volume that brakes and cools, about 6 L with the pipes on a 2 L, as in C++), cold pipe, blow-off, throttle, plenum. The gas does not compute its own turbo: it receives the pressure ratio, the compressor outlet and the valve openings from C++, and its turbine has the same size law. Its pressure before the turbine stays within 15% of C++ at the rated point. In the animations' slow motion, the gas gets twelve times less physical time: the volume then takes twelve times longer to fill on screen.

The views (`turboChain.ts` for the engine, 2D and 3D; `ExhaustView` and `CarStage` for the rest) draw the same chain, only on a turbo: two wheels on a shaft, their housings, the airbox and its air-mass sensor, pipes tinted by temperature, the intercooler at the front, blow-off and wastegate moving with telemetry. On a V or boxer, one turbo per bank, like the collector, and one intercooler. The shaft turns on screen in proportion to its real speed and to `motion.slow`, with a further fixed slow-down of 60: at 150,000 rpm, even twelve times slower it makes 200 turns per second. Too fast for the eye, its blades give way to a blurred disc.

### Turbo sound

The whistle is at shaft speed: its second order dominates, with a little of the first (imbalance). Pitch and level therefore follow the real shaft, its rise after a stab, its slow fall without a valve. Under `setTurbo`, blow-off and flutter no longer have an envelope: their level follows the jet's strength, flow times the square root of pressure (the valve stays broadband noise, around 1.8 to 3.8 kHz), or the return through the compressor wheel. Their draws come from a separate generator: they shift no other sound. Without `setTurbo`, an `EngineSound` driven from outside keeps the older behaviour, deduced from pressure.

The dominant order, the flutter level and the split between orders remain choices: no reference recording was compared, and only listening will settle them.

## The turbo's pipe

`turboPipeMm` is the length of pipe between the cylinders and the turbine, from 150 to 3000 mm (`core/src/turbo_pipe.cpp`). 400 mm is the calibration's reference: at that value, nothing below runs, and no hash moves. Only the difference from that reference exists, and no lag constant is written: lag remains what the shaft needs to store its energy, with less energy available.

- **The wall.** The gas passes over the added surface, `π·d·ΔL`, against a wall that has mass. Inside exchange from Dittus-Boelter (`Nu = 0.023·Re^0.8·Pr^0.4`, raised by 1.5 because the flow pulses), depending on flow; outside, 50 W/(m²·K) for convection under the car and radiation; 1.5 mm steel, about 40 s to heat. Cold or at low flow, the gas leaves a lot of heat there, so enthalpy at the turbine. This is the dominant cause.
- **The volume.** The pipe's section over the added length fills up: pressure before the turbine joins the ellipse's value with `τ = V·(dp/dṁ)/(γ·R·T)`, and what leaves through the turbine isn't yet what enters. A few tens of milliseconds: 37 ms for 63 % of the rise with 3 m.
- **The travel.** The mass of gas in the pipe, renewed by the flow: 125 ms at 20 g/s, 12 ms at 200 g/s. The pipe is mixed, not a piston: this is its mean time, not a pure delay.
- **The pulses.** The pipe's volume filters the firing rate to first order, with this same `τ`. The pressure excursion the pulses bring is multiplied by the ratio of this filter to that of a 400 mm pipe: above 1 on a short pipe, far below on a long one. It matters mostly at low revs.
- **Friction.** `f·(ΔL/d)·½ρv²` adds to what the cylinders push against, not to the turbine's pressure: a few Nm at the top.
- **The air's return.** Remote, the compressor goes with the turbine: `chargeVolumeM3`, the duct's air column and its loss grow by ΔL at the duct's section. Surge slows from 8.7 to 3.3 Hz at 3 m: that is what flutter follows by ear.
- **Shorter than 400 mm.** A volume that isn't counted can't be removed: what remains is the heat the removed pipe would have taken (hotter gas, inverting the wall's law), better kept pulses, and less heat to take before the turbine (a 3 kg manifold stays, assumed). The air's return doesn't change: the intercooler stays at the front.

### What it gives

`hot_hatch_turbo`, 2.0 L at 1.3 bar. `--scenario turbo`: time to 50 and 90 % of the requested pressure. "Cold": as the scenario plays it, the pipe has only run for a few seconds. "Warm": after two more minutes at part load.

| Pipe | Scenario, cold | Scenario, warm | Gas at 90 %, cold | 2500 rpm held, 90 % | 3500 rpm, 90 % | Settled gas, 2500 / 5500 rpm | Torque at 2500 / 5000 | Power | 0-100 km/h | WLTC | 90 km/h |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 150 mm | 1.39 / 1.91 s | 1.43 / 1.96 s | 770 °C | 1.06 s | 0.66 s | 794 / 888 °C | 351.1 / 349.0 Nm | 194.6 kW | 6.75 s | 8.89 L/100 | 7.26 L/100 |
| 400 mm | 1.76 / 2.33 s | 1.70 / 2.28 s | 754 °C | 1.21 s | 0.71 s | 782 / 882 °C | 350.2 / 348.4 Nm | 194.4 kW | 6.86 s | 8.90 L/100 | 7.26 L/100 |
| 1000 mm | 2.55 / 3.34 s | 2.39 / 3.16 s | 647 °C | 1.57 s | 0.79 s | 756 / 866 °C | 349.5 / 347.7 Nm | 194.0 kW | 7.14 s | 8.92 L/100 | 7.26 L/100 |
| 2000 mm | 4.19 / 5.31 s | 3.74 / 4.81 s | 519 °C | 2.54 s | 0.99 s | 714 / 841 °C | 348.9 / 346.7 Nm | 193.4 kW | 7.61 s | 8.93 L/100 | 7.20 L/100 |
| 3000 mm | 6.06 / 7.47 s | 5.32 / 6.66 s | 431 °C | 7.97 s | 1.26 s | 675 / 817 °C | 348.4 / 345.8 Nm | 192.7 kW | 8.06 s | 8.95 L/100 | 7.20 L/100 |

Time in the scenario grows more than lag alone: in fourth gear, until pressure arrives, revs climb slowly, and gas stays scarce. At 2500 rpm, a 3 m pipe nears the energy limit: the 90 % arrives at a crawl, as the wall warms. Consumption hardly moves. At 90 km/h, it falls by 0.8 % in one step between 1 and 2 m: the cause hasn't been isolated, and it isn't a gain to rely on.

What each cause weighs at 3 m, removing it alone (`--no-skip -tc="le tube : poids*"`):

| Removed | 3500 rpm held | Scenario, warm |
|---|---|---|
| Nothing | 1.26 s | 6.66 s |
| Exchange with the wall | 0.87 s | 2.99 s |
| Wall mass (÷ 100: already hot) | 1.13 s | 4.20 s |
| Pulse filter | 1.19 s | 5.84 s |
| Volume | 1.14 s | 6.50 s |
| Cold air return | 1.14 s | 6.46 s |
| Travel time | 1.24 s | 6.65 s |
| Friction | 1.26 s | 6.70 s |

### Against real installations

| | Model | Reference |
|---|---|---|
| Full boost on a V8 | 500 rpm later with 2.6 m more (4.8 L, 0.48 bar, large turbo) | About 500 rpm later with 3.66 m more, 4.8 L, twin 61 mm GT35, 0.48 bar, on the dyno: R. Holdener, EngineLabs (article read; a test with no scientific publication or error bar) |
| Peak power | -0.4 % on this V8; -0.9 % at 3 m on the 2.0 L | 613 → 609 hp: same test |
| Air return alone | 0.2 s of 6.7 s in the scenario, 0.1 s at 3500 rpm | 2.44 m more on the discharge: almost nothing at 0.48 bar, a visible delay at 0.9 bar: same test |
| Cooler gas at the turbine | -107 °C settled at 2500 rpm, -65 °C at 5500; -296 °C in the "warm" run, wall still tepid | "260 °C less": Turbodynamics, a workshop's estimate, with no measuring conditions (read) |
| Volume before the turbine and response | Lag grows, mainly through the pulses | SAE 920361 and 2004-01-0926: dead volume before the turbine degrades the transient (abstracts read, on diesels) |
| Integrated manifold | -18 % on the time to 90 %, +16 °C | The direction is known; no comparable figure found |

**What is assumed:** the pipe's diameter (50 mm on 2 L, the same as the on-screen gas), the 1.5 mm wall, the outside coefficient, the 1.5 increase in exchange (Caton and Heywood, 1981, *Int. J. Heat Mass Transfer*, measure it much closer to the valves: from memory, not reread), the friction of 0.02, the 3 kg manifold, and the first-order filter for pulses. None was tuned on lag or on the test above: agreement to within 500 rpm comes from a single test, on an approximated engine, and proves nothing else. The bulk of the effect rests on the wall: it is also the least measured part. The turbo isn't resized: a real remote installation would often use a smaller turbine.

**What isn't modelled:** waves in the pipe for the shaft's balance (only their mean energy counts, as on the rest of the turbine), the line that would shorten by as much behind the turbine, oil and its return on a remote turbo, cold air under the car that could replace the intercooler, underbonnet heat around a turbo bolted to the head.

### On screen

In the gas, a remote pipe has its own cells and one more volume before the turbine: pulses and heat are lost there by the same computation as everywhere else, and the `hot` part of `TURBO_INTAKE` grows by the same length as in C++. Shorter, what is missing comes off the junction's volume. The gas remains a display computation, with nothing going back to the sound.

`turboChain.ts` places the turbo at true scale: it moves back along the engine until the air box has passed the flywheel, then drops below the intercooler's level, then all the rest goes rearward; a test checks that parts cross neither the flywheel nor the block's envelope. The 3D view keeps that scale, and its frame follows: at 3 m, the engine is small there, and is looked at by zooming. The side section folds the pipe beyond 2.5 bores, with a break mark and its length written, so the cylinders stay readable. On the car (`CarStage`), the turbo moves rearward by the true length, without leaving the body, and once well remote the filter follows.

### The sound

It changes through what already drove it, without one more line in `EngineSound`: the whistle follows the shaft, its pitch and level rise later; pressure delays `boost`, hence the turbine's muffling; flutter follows surge, slower. Measured on `--scenario turbo`, from 400 to 3000 mm: the 4 and 8 kHz bands lose 2 dB, the rest 1 dB at most (`tools/analyze_sound.py`), and the scenario shifts in time. Nothing was added for the pulsation: the pulse filter holds below a few tens of hertz, where the pipe is shorter than the wave; above that, it is a waveguide that passes almost everything, and its reflections off the turbine would need a model of that reflection and a recording we don't have. The whistle's position doesn't change either: it stays in the intake source, even on a turbo remote at the rear. Listening is still to be done.

## Anti-lag

Off the throttle, an ordinary turbo loses its gas: the fuel is cut, the valve vents the pressure, the shaft falls. Anti-lag (`core/src/anti_lag.cpp`, the `--antilag` setting) keeps sending energy to the turbine. No system has a time constant or a pressure target: each changes what reaches the turbine, and the power balance of `TurboCharger` does the rest.

It works when the pedal is lifted (under 3%) and the engine is above 27% of its maximum speed, outside the limiter and shift cuts. The ECU holds the blow-off valve shut, as real builds do: otherwise the pressure would leave that way. Air pushing against a nearly closed throttle comes back through the wheel: the surge already computed, around 17 Hz here, becomes the audible "chatter", valve fitted or not.

- **Ignition retard.** Throttle at 30%, spark 55° after best torque, so 35° after top dead centre. Torque follows a parabola, `1 − (Δ/68°)²`, 0.35 here: the shape of ECU "torque ratio" tables. The work the piston does not receive goes into the gas, less 20% into the walls: `T3` climbs towards 1090 °C. Lambda 0.9: this surplus does not burn, for lack of oxygen, and it counts.
- **Air injection.** A valve, 17 mm on a 2 L, links the cold pipe to the manifold. Its flow is an orifice law between the charge pressure and the pressure before the turbine, solved by bisection at each step (explicitly, it would chatter); a check valve stops it if the manifold pushes harder. The ECU adds the matching fuel, up to λ 0.65 in the cylinder, and 70% of this surplus burns in the manifold, if it is hot enough. The heat arrives without going through the cycle. On the 2.0 L at 4400 rpm, compressor, flame and turbine sustain one another like a gas turbine: pressure holds at 1.3 bar, turbine valve open, without any value asking for it, until the speed falls to around 3500 rpm, when it drops to 0.3 of the pressure. Throttle at 25%, 40° of retard: less open (15%), it lacks the air to carry the surplus, and the system stays much weaker.
- **Cylinder cut.** Two firings out of five, in firing order (F C C F C): the pattern thus moves from one cylinder to the next on 4 or 6 cylinders. Throttle at 35%. The mixture of the cut cylinders passes through unburnt, and 45% ignites in the manifold if the gas there exceeds 800 K (all of it at 1100 K): cold, it does not light, and the manifold cools further, as seen around 1900 rpm (260 °C), where the system no longer does anything.

Nothing regulates the torque that remains. Pedal lifted, the engine brakes far less, and on the 5-cylinder air injection even gives 4 Nm more than its losses in fourth. In neutral, every petrol turbo tried finds idle again (tested).

**The diesel is left out, by choice.** No throttle to crack, no spark to retard, no prepared mixture to pass through unburnt. A post-injection into oxygen-rich gas exists to regenerate a filter, at 600 °C: no figure was found to make an anti-lag of it. The site hides the choice, and everything stays identical to the bit (tested).

### The four choices, measured

`hot_hatch_turbo` (2.0 L, 1.3 bar) on `--scenario turbo --seconds 32`, read with `--trace`, except the catch after a 3 s lift. The mean runs from 0.3 to 3 s after the last lift.

| | Off | Retard | Air | Cut |
|---|---|---|---|---|
| Shaft, 1 s after the lift (rpm) | 54,000 | 109,000 | 149,000 | 110,000 |
| Shaft, 3 s after | 22,000 | 97,000 | 149,000 | 99,000 |
| Pressure at 3 s (share of 1.3 bar) | 0.01 | 0.29 | 0.99 | 0.34 |
| 90% of the pressure, after a 0.35 s lift | 0.40 s | 0.21 s | 0.21 s | 0.15 s |
| 90% of the pressure, stab from 1800 rpm | 2.20 s | 1.62 s | 1.92 s | 2.00 s |
| 90% of the pressure, after a 3 s lift (test, in fourth) | 0.97 s | 0.53 s | 0.51 s | 0.54 s |
| Peak `T3` | 789 °C | 1090 °C | 1094 °C | 937 °C |
| Crankshaft torque, pedal lifted | −80 Nm | −36 Nm | −4 Nm | −24 Nm |
| Torque taken from this air charge | 0 | 54 Nm | 34 Nm | 59 Nm |
| Air sent to the manifold | 0 | 0 | 27 g/s | 0 |
| Fuel, pedal lifted | 0 | 2.08 g/s | 3.66 g/s | 2.30 g/s |
| Fuel over the 32 s | 0.136 L | 0.163 L | 0.178 L | 0.166 L |
| WLTC, L/100 km and g CO₂/km | 8.90 and 210 | 9.48 and 224 | 9.39 and 222 | 9.54 and 225 |
| 0-100 km/h | 6.86 s | 6.64 s | 6.66 s | 6.68 s |

The differences have their cause. Retard heats without depending on the manifold temperature, but at a low flow. Air injection alone raises the flow through the turbine, hence the pressure it holds, and it is the dearest in fuel. The cut burns less well what it sends, but keeps its reserve: after a 0.35 s lift it catches fastest, on a still-hot manifold. Lower in the revs it goes out, and air injection is cooled by fresh air: around 1800 rpm, only retard clearly wins.

### Consumption

The brake torque given to `ConsumptionMeter` follows the admitted air, not the torque delivered: a retarded or cut cylinder thus already pays for all its fuel. To this are added `extraFuelGps`, the surplus of retard and air injection, and `extraAirGps`, the air of the valve. CO₂ follows the fuel and lambda both. None of this goes back to the physics.

### The sound

Everything goes through `setTurbo` (`TurboDrive::antiLag`), with its own generator. Without anti-lag, none of this code runs: no fingerprint moves.

- The fuel is not cut: no `overrun`, no pressure drop of a lift. The pressure when the valve opens is multiplied by the extra heat retard leaves (`evoRatio`).
- The cut acts on the firing forces: a cut cylinder opens on 28% of the pressure and does not strike the block. Against retard, the firing line loses 8 dB (140 Hz), and the sub-harmonics of the pattern (21 to 35 Hz) gain 6 to 18: the sound is chopped by the cause, not by an envelope.
- The bangs leave by the same path as `pops`. One chance per valve opening, depending on whether the manifold can ignite what arrives: 8 per second on the cut, 4 on retard and 1 on air injection. Their amplitude follows the square root of the energy per opening, against an ordinary 90 J bang: about 360 J for a cut cylinder.
- The flame is a low noise, around 380 Hz, at the square root of the heat released, which crosses the line like the rest. The 8-16 kHz band does not rise above that of the same turbo at full throttle (−69 dB against −64 dB).
- The outlet jet follows the real flow, at the computed temperature.

**Listening still to do.** Nothing could be listened to or compared with a recording: the chances, the heat shares and the flame level are assumed, and only listening will decide.

### What is assumed, and what is missing

Sources, seen through a search and not read in full: [Antilag system, Wikipedia](https://en.wikipedia.org/wiki/Antilag_system), [Car Throttle](https://www.carthrottle.com/news/anti-lag-blogpost) and [TorqueCars](https://www.torquecars.com/tuning/anti-lag-systems.php) for the principles and orders of magnitude (spark 30 to 45° after top dead centre, over 1000 °C), enthusiast sources none of whose figures has been confirmed by a published measurement. **Assumed:** the torque parabola (the shape is known, its zero at 68° is an order of magnitude), the throttle openings, the 20% to the walls, the shares that burn in the manifold (45 and 70%), the 800 to 1100 K of ignition, the valve section, the 27% threshold.

**Not modelled:**
- **Wear and breakage.** Nothing computes them, so nothing breaks: a real system quickly wears the manifold, turbine and valves, and real ECUs limit duration and temperature. Here that time has no limit, and `T3` can stay above 1000 °C. The site's help says so.
- **Torque firing by firing.** `DrivingModel` keeps a mean torque; only the sound and the acyclism passed to the gearbox see the pattern, and their counters are not in phase.
- **The car and engine views.** Only the gas view draws the air valve and fires the spark late (`fluidGas.ts`); `CarStage` and `EngineView` do not show it.

## The supercharger

`Supercharger` (`core/src/supercharger.cpp`) advances at each 1 ms step of `DrivingModel`. Its shaft is tied to the crank by a belt: its speed is a fixed ratio of engine speed, with no turbine and no inertia to spin up. The only response time is that of the volume between the compressor and the throttle (1 L + 0.8 times the displacement): 80 % of the pressure in 0.28 s at 2500 rpm on a 2.0 L, against 1.2 s for the medium turbo at the same speed.

```
dp/dt = (R·T/V)·(ṁ_compressor − ṁ_engine − ṁ_bypass)
```

- **Positive displacement (Roots, twin-screw).** `ṁ = ρ·(V_r·N − A_leak·√(2Δp/ρ))`: the swept volume, minus leaks that follow the pressure difference rather than the speed. Hence pressure from low revs, falling short only at the very bottom. The pulley ratio comes from the maximum shaft speed (16,000 rpm for a Roots, 20,000 for a screw, manufacturers' orders of magnitude); the swept volume from the requested pressure at 40 % of the redline. This gives 0.95 L per turn on a 2.0 L at 1 bar, the size of an Eaton M45 to M62. The leak area (`kRootsLeak`, `kScrewLeak`) is assumed: it gives 85 % volumetric efficiency at 8000 rpm and 0.5 bar on 1.5 L.
- **The work.** A Roots does not compress inside itself: the pocket opens on the outlet, and the whole swept volume pays `Δp`. A screw first closes its volume (ratio 1.3, assumed) and finishes at the opening: `w = γ/(γ−1)·p₁·(v^(γ−1)−1) + (p₂ − p₁·v^γ)/v`. With `v = 1`, this is the Roots. The same sum explains two known facts: the Roots runs hotter (all this work ends up in the outgoing air, 144 °C at 2720 rpm and 1 bar, against 104 °C for the screw), and the screw costs a little more with the bypass open, since it compresses anyway.
- **Centrifugal.** A wheel like a turbo's, but driven: head `ψ·U²`, so a pressure that grows with the square of speed. The rated point is at 88 % of the redline. With a reduced flow of 1.5, the calculation gives 101 mm and 67,000 rpm on a 6.2 L at 0.8 bar, 60 mm and 120,000 rpm on a 2.0 L at 1 bar: the range of ProCharger and Rotrex. Tip speed is capped at 520 m/s: on a small engine, 3.5 bar is out of reach. Efficiency is 72 %, with no surge modelled: the bypass valve is meant to avoid it. Past the rated point it dumps at full pressure what the wheel keeps raising, hence a kink in the torque curve: a real kit without relief would let the pressure rise instead, but here `boostBar` is held.
- **The bypass valve.** Manifold vacuum opens it (wide open at 70 % fill, closed at 95 %, in 40 ms): pedal released, the flow returns to the inlet across a few kPa. At full throttle it also holds the requested pressure by returning the surplus: that is `spillShare`. What is returned has still been compressed: at 6500 rpm, a 1 bar Roots returns 33 % of its flow and takes 29 kW from the crank.
- **Torque.** `fullLoadTorqueNm` is what the pistons give: the atmospheric torque (compression lowered by 5 %, like a turbo) times `p/T` of the air after the intercooler (effectiveness 0.86 at low flow, 0.72 at rated, 0.05 bar lost). A Roots' hotter air weighs less: at equal pressure it gives less gross torque than a screw. The drive is taken off separately, like pumping, and adds to engine braking: this power does not come from the exhaust. It is also `driveNm` that makes raising a Roots above 2 bar cost more than it brings: 3.5 bar gives less net torque than 1 bar. This is not a flaw of the calculation: it is why nobody boosts that way at high pressure. Knock is not modelled, and no engine would survive 3.5 bar of that air.
- **What compares to `Aspiration::Turbo`, decided one by one.** Like a turbo: the torque peak a little earlier (`peakAt`, milder cams), the starter's 9.5 compression, the fuel model's indicated efficiency and full-load enrichment, Anderton's formula for a boosted diesel, exhaust flow under pressure. Without a turbine, unlike it: `popForce` is not damped, pulses are not smoothed (`active.turbo` in `gasPathSample`), nothing is taken from the exhaust, no extra oil for shaft bearings (rotors have their own oil, outside this figure). The release setting has no meaning: there is no valve to atmosphere, and the setting disappears from the panel.
- **Consumption** reads the torque: `burnt_.frictionNm` counts the drive, because the pistons pay for it. With the bypass open it stays small: see the table.

| 2.0 L, 4 cylinders, 6800 rpm, 1 bar | Peak torque | Power | 0-100 km/h | Top speed | 90 km/h | WLTC |
|---|---|---|---|---|---|---|
| Naturally aspirated | 174 Nm at 4100 | 97 kW | 10.2 s | 209 km/h | 6.8 L/100 km | 8.9 L/100 km |
| Medium turbo | 311 Nm at 3750 | 170 kW | 7.8 s | 254 km/h | 6.4 L/100 km | 8.9 L/100 km |
| Roots | 260 Nm at 3750 | 138 kW | 8.5 s | 237 km/h | 6.6 L/100 km | 9.2 L/100 km |
| Twin-screw | 280 Nm at 3750 | 150 kW | 8.2 s | 244 km/h | 6.6 L/100 km | 9.3 L/100 km |
| Centrifugal | 235 Nm at 5950 | 146 kW | 9.8 s | 242 km/h | 6.4 L/100 km | 9.2 L/100 km |

Consumption figures remain `ConsumptionMeter` estimates (see "Consumption"). A different gearbox, longer when there is more power, plays as large a part as the compressor.

### The supercharger's sound

The sound comes from `SuperchargerSound` (`core/src/supercharger_sound.cpp`), with its own generator: no engine draw is shifted, and without a supercharger none of it runs.

- **Positive displacement.** When a pocket opens on the outlet, compressed air rushes in: a flow pulsation at (pockets per turn) × (turns per second). Three lobes on two rotors make six pockets per turn of a Roots (1412 Hz at 6000 rpm on the 2.0 L). Three lobes on the male rotor make three pockets per turn of a screw. The shaft's phase follows the crank's, cyclic irregularity included. The level is a monopole `ρ/(4π)·2πf·Q`, with `Q = V_pocket·share·f`, where `share` is what the pocket has to catch up: `Δp/p₂` on a Roots, the gap to its inner pressure on a screw, plus a remainder (0.2, assumed). Hence a level in `f²·Δp`: almost nothing with the bypass open or at idle, and a whine that climbs fast at full throttle. The transmission through the casing and duct (36 dB, 31 for a screw) is assumed, with no measurement behind it.
- **Timbre.** Four harmonics, a narrow noise around the line, slight phase jitter, neighbouring rotor-order lines and variation from one firing to the next: these keep it from being a siren. The casing modes are fixed (1.7 kHz): the lines move past them.
- **Centrifugal.** A shaft order 2, in a narrow noise, like the turbo, and the passing of six blades, which fades between 4 and 12 kHz: without this, on a 2.0 L, the line sat at 12 kHz in front of everything. Level in `U³` with flow, 0.35 Pa at 1 m at rated (assumed).
- **Its place.** A positive-displacement casing radiates under the hood (`Source::Bay`). A centrifugal wheel and the inlet rush (0.5 Pa at rated flow, in flow squared, below 3 kHz) come out through the intake duct (`Source::Intake`). The whine has no fixed level, so it does not move ahead at idle: at 800 rpm, bypass open, neither the 500-2000 Hz band nor the 8-16 kHz band moves by more than 1.5 dB (counter-tested).

Measured on `--scenario launch`, from the front, at full throttle: the line shows on the spectrogram from 500 to 1500 Hz without changing the band balance (500-2000 Hz: −31.7 dB against −31.3 dB without a supercharger; 8-16 kHz: −55.7 dB against −54.2 dB). From behind, the exhaust masks it, as on a real car. **No listening has been done**: level and timbre remain to be judged.

## What is simulated in the sound

A first version, which destroyed every spectral line above 850 Hz to avoid the zipper, had gone too far: measured on
real engines, the lines stay sharp up to 2 or 3 kHz, and a test was locking in this defect. Inspired by the code of
engine-sim and the Sound Design Toolkit, the current chain sends all of the gas through the exhaust line.

1. **Cylinders.** Each one keeps its own pressure, different on every cycle and tied to the real fill. The valve opens
   quickly, but the cam lifts gently, over 15° of crankshaft rotation: a 3 ms front at idle, 0.4 ms at 6000 rpm. Without
   this, each firing made a click in the mids at idle, where real engines have a continuous bed of sound. The cylinder
   empties in about 2 ms at full load, much more slowly at low load, because it then opens on little pressure.
   Combustion varies more at low rpm.
2. **Crankshaft.** It speeds up on each firing and slows down on each compression, especially at idle.
3. **Headers.** One delay line per cylinder, with a length specific to the layout, whose walls hold back the highs. A
   cross-plane V8 fires at equal intervals, but does not alternate between banks: two consecutive puffs in one bank
   get in each other's way there, hence the "burble" (« glouglou »).
4. **Turbine.** It takes its energy from the puffs, which come out softer and weaker.
5. **Turbulence.** A sample-to-sample jitter (22 µs) gradually undoes the spectral lines toward the highs, with no
   cutoff, and a noise proportional to the varying flow is added to it. According to measurements on a V6, it grows very
   quickly with rpm at full load, and stays roughly stable at part load.
6. **The line.** A 4096-point impulse response, computed by transfer matrices from the geometry: pipes, catalytic
   converter, resonator, packed muffler, outlet to open air. It follows the displacement and the length of the car, and
   all of the gas goes through it, spectral lines and hiss alike; only the mouth noise and the rumble bypass it. The radiated pressure follows the derivative of the flow.
   The calculation is checked against the analytical formula for an expansion chamber. Partitioned convolution. Above
   700 Hz, the back-and-forth reflections gradually blur, and only the direct path stays sharp: flow and temperature
   shift the resonances from one moment to the next. Without this, 35 cm sections form a comb of fixed bumps every
   680 Hz, heard as "a can sound" (« un son de canette »), which real engines do not have. See "The exhaust".
7. **The rest.** Intake, rhythmless rumble, mechanical hiss, diesel knock (broad, around 1 kHz, over 12 ms), wind, tires (which lose their highs above 2.2 kHz: otherwise they covered everything like rain once the car moved), squeal. The block follows the firings more than it provides a continuous background, and loses its highs above
   4.5 kHz: when it was more continuous and broader, it sounded like white noise under the hood, 4 to 8 dB above real
   engine bays. A valve is only heard when it drops back onto its seat, and faintly outside the hood. Louder and on every
   valve movement, these fixed-level bursts came in front of the exhaust at low rpm, and sounded like "something metallic
   slapping against the ground" (« un truc métallique qui claque contre le sol »). The mechanical hiss and the intake lose
   their highs above 3 kHz at low load: full-band and at an almost fixed level, they stayed behind the engine like white
   noise, 10 to 18 dB above real idles between 8 and 16 kHz. They keep them under the hood.
8. **Listening and level.** All these sources are in pascals, at 1 m from them, and placed on the car. See "Where you
   listen from", "The cabin" and "From real level to output". A limiter acts on the gain, without distorting the signal.

Accepted limit: the response is computed when the engine is changed, and does not follow a displacement slider until
parts are changed. Nor does it follow gas temperature.

Fuel is only cut off the throttle (throttle under 3%, or load under 15% when its position is unknown), above 1.35 times idle: a low-load cruise still burns its fuel. With a higher
threshold, it lost 72% of its pressure, and the sound jumped by 8 dB at constant rpm.

If it is not given the fill, the sound deduces it from the rise in rpm: it can therefore work from rpm alone, for
example read from a CAN bus.

## The exhaust

Previously, the line's response was normalized, then an automatic level equalized the engines: a straight-through line
came out 2.4 dB(A) *quieter* than a stock line, even though the geometry alone already gave 14 to 51 dB of insertion
loss. The physics was there, and the normalization threw it away. The response now stays absolute.

**Six lines, from their geometry** (`ExhaustLine`, copied for drawing into `EXHAUST_LINES`): stock (catalytic converter,
Helmholtz resonator tuned to the drone, muffler), sport (free-flowing muffler, little wool), straight-through, no muffler,
no resonator, comfort (several volumes). The transfer matrices do everything: no gain is entered per line. The mouth
follows Levine and Schwinger, through the approximation of Silva et al. (2009) for |R| and the end correction. The source
impedance is 2 Zc: neither rigid (resonances too pronounced) nor anechoic.

| Sport 4-cyl., stationary, 0.5 m | Difference from stock | Target (registers, Japan) |
|---|---:|---:|
| Comfort | −10.9 dB | no target |
| No resonator | +2.0 dB | +1.5 (0 to +3), weak |
| Sport | +8.9 dB | +7 (+3 to +11) |
| No muffler | +11.9 dB | +11 (+9 to +14) |
| Straight-through | +21.5 dB | +15 (+10 to +25), weak |

**One or two outlets.** On a V or flat engine with at least 4 cylinders, unless a single outlet is chosen, two lines are genuinely simulated, each bank in
its own, at half the displacement and cross-section. The H-pipe mixes the two banks before the outlets, the X-pipe even
more. A line that passes every transmission-loss test can still sound like a can: the comb test covers all the lines.

**What pops and the jet owe to the line.** A pop comes out through the line: held back as much as its mids. The mean flow
gives the gas speed at the outlet, and therefore the mouth noise (57 dB per decade of rpm, Selamet) and, very close up, the
wind of the jet: ½ρU² on the axis, with U ∝ 6D/z, over a cone of 5.4° at half speed, where the ear receives wind and puffs.

A test is not enough to judge these lines: at the same load and in the same place, no recording comparing several exhausts on the same car, with the same microphone, has been found. The differences come from Japanese manufacturer measurements and from
registers; the timbres remain to be listened to.

## Where you listen from

Six sources are placed on the car (`Source`, `carAcousticsOf`): outlets A and B, intake, engine bay, underfloor, road. The
starter is in the bay, the turbo at the intake, the gearbox under the floor, in the middle (at the rear for a rear engine).
No gain is entered per position: everything follows from this layout and from `SoundField`, in this order:

1. **Directivity** of the mouth, according to the angle and ka (Levine and Schwinger, through one pole and one zero per
   angle), plus the effect of the flow (1 + M cos θ, Masovic). Beyond ka ≈ 5, the jet diffuses: the on-axis gain levels off.
2. **The body**: the shortest path around it, over the roof or along the sides, and Maekawa attenuation, softened to
   10·log(1 + 20 N).
3. **The air**: ISO 9613-1, at 20 °C and 50%. Nothing up close, 6 dB at 8 kHz at 50 m.
4. **Delay and 1/r.** One delay line per source and per ear: the Doppler effect and late arrival come on their own. The
   pressure of a monopole follows 1/r at any distance, with no near-field term (Rienstra and Hirschberg, §6.2). What bounds
   the level up close is the extent of the source: 0.35 times the radius for an outlet, measured from its acoustic center,
   0.61 radius past the mouth.
5. **The ground**: the image of the source, reflected off asphalt (Delany and Bazley). Below 250 Hz, it stays in phase;
   higher up, only a part (25%) stays coherent, because real recordings do not dig a comb deeper than 3 dB.
6. **The place**: a little reverberation, so as not to listen in a vacuum.
7. **The head**: stereo using Brown and Duda. A path around a sphere of 8.75 cm to each ear (hence the time difference), and
   a shadow shelf α(θ) (hence the level difference). No HRTF: the cost of one convolution per source and per ear, plus
   measured data to ship, for a gain in elevation and front-back perception, 3 to 10 dB on the shadow behind the head.
   Without it, front-back localization is weak: headphones do not tell you whether the car is in front or behind.

The head **glides** to its place (`setTarget`, 6 m/s for a ready-made position), without a click. `render` stays mono, at the
center of the head: this is the path used by the tests and the fingerprints; `renderStereo` gives both ears, and the site
uses it.

**The hood.** When closed, the bay radiates through its front opening and from below (the highs stay inside, −10 dB above
3 kHz). When you approach the block (`bayOpen`), the source glides toward it and opens up.

Measured differences, idle and full throttle (dB per octave, see `tests/acoustics_test.cpp` for what is locked in). The
references come from real recordings, same cars, same microphones: BBC, La Sonothèque, Mercedes 190E, Saturn ION, Volvo S60. Only numbers are taken from them: none of these recordings is in this repository.

| | 125 | 250 | 500 | 1 k | 2 k | 4 k | 8 k |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cabin − outside, idle, real (BBC, n=14) | 0 | −3 | −4 | −8 | −11 | ≤−14 | ≤−10 |
| City car, simulated | +5 | −5 | −2 | −10 | −14 | −16 | −17 |
| V8, simulated | +3 | −3 | −1 | −8 | −14 | −15 | −15 |

Known to be out of range: under the hood, from 1 to 8 kHz, still 4 to 6 dB above the reference envelope, and the lows are
missing there (A−Z of −0.2 versus −3.5); sport 4-cylinder, at idle: 6 dB too much at 125 Hz, 6 dB too little at 500 Hz. The
perceptual comparator (`tools/perceptual_compare.py`) says so too.

## The cabin

Three groups of sources (exhaust, engine, road and underfloor) each go through a 1024-point minimum-phase filter, computed by
cepstrum when a car is applied, then convolved. This filter comes from an energy balance over ten walls (floor, roof, two doors,
two windows, firewall, rear wall, windshield, rear window), each with its own mass law, its coincidence frequency for glass, and its leaking
share (`cabinTransfer`). Up close, each source couples more strongly to the wall it faces. The engine mounts let the low
orders in directly, without going through the air (`structureGain`). The cabin adds its modes and its short reverberation.

| Insulation | Sheet steel | Glass | Firewall | Leaks | City car, idle | City car, full throttle |
|---|---:|---:|---:|---:|---:|---:|
| Race | 4 kg/m² | 4.8 | 6.5 | 0.3% | 52.1 | 85.0 |
| Sport | 5.5 | 8 | 9 | 0.08% | 38.8 | 74.4 |
| Standard | 7 | 10 | 12 | 0.025% | 33.9 | 69.8 |
| Comfort | 10 | 13 | 17 | 0.008% | 30.5 | 66.3 |
| Luxury | 15 | 20 | 26 | 0.002% | 26.5 | 62.1 |

Open windows turn the driver's window into a hole (30% leak) and raise road noise, through the wheel arches: +8.7 dB(A) over
a ride (« balade »), real +5 on the road.

**Engine placement** (`placement`) changes the position of the block and the intake, that of the seat and the firewall, the
length of the line (about 0.4 times for mid-engine, 0.25 times for rear-engine), and the share of mass on the front: these
effects are linked. In the cabin, the block and the intake gain 3 to 5 dB behind the seats. **At full throttle, on a
free-flowing line, the outlet stays dominant and masks this difference:** at total level, mid-engine and front-engine come out
the same. With a comfort line: front 67.9, mid 71.6, rear 73.4.

Missing: ventilation. Real idles of city cars and sedans are 3 to 6 dB louder than the model, and a fan at idle can add
15 dB(A) on a Volvo 240.

## From real level to output

The sound level meter (`LevelMeter`) reads the pressure before the volume: A-weighting filter from the IEC 61672 standard
(class 1 at common sample rates, the highest pole corrected for the warping of the bilinear transform), "fast" time constant
of 125 ms. This is the figure displayed: **an estimate**, which no microphone has checked.

An ear at 118 dB(A) and a speaker at 70 dB cannot have anything in common: `toneGain` compresses the real level, piecewise,
toward an output level. It rises by about half: 40 dB(A) to −42 dBFS, 80 to −22.5, 100 to −15, 120 to −13. Attack in 1 s,
release in 2 s, 0.04 s when the head moves: a throttle blip keeps its contrast, the gain does not catch up with it. The volume
stays separate. An RMS ceiling at 0.25, then the limiter, preserve the waveform: during a run-up, its average gain is 1.000 on
all engines except the V10 with a straight-through line (0.97, 117 dB(A) behind). Before, it acted continuously at full load
(0.35 to 0.40).

**Laws**: load only acts on the exhaust, as on real recordings (Saturn: +15 to +22 dB above 500 Hz at the exhaust microphone,
nothing at the engine microphone).

| Law | Target | Measured |
|---|---:|---:|
| Block, dB(A) at 1 m, full load | Anderton, 50 log N + 60 log B − 203 (petrol; diesels have their own formulas) | ±2 dB from 3750 rpm up, as much as −5 at 2000 |
| Exhaust at no load | 10 dB per doubling of rpm | 10.0 |
| Full load − no load | +12 (+10 to +15) | +13.9 |
| Idle − stationary test | −25 (±6) | −20.8 |
| Road at 7.5 m, 130 km/h | CNOSSOS: 72 + 34 lg (v/70) | 83.9 versus 81.1 |

`exhaustLaw`, a slope of 3.6 dB per octave, is the only tuning in this column, along with a factor of 0.5 on the diesel filter: the synthesis already gives almost everything,
and this slope was what it lacked.

| Preset | Stationary test (R51.03, 0.5 m) | Target | Cabin, idle | Target | Cabin, full throttle | Target |
|---|---:|---:|---:|---:|---:|---:|
| City car 1.4 | 76 | 78 ±4 | 34 | 40-46 | 70 | 72-76 |
| Sport 4-cyl. | 89.5 | 86 ±4 | 43 | 45-56 | 79.5 | 82-98 |
| GTI turbo | 88.5 | 88 ±6 | 42.5 | 41-52 | 82 | 78-85 |
| Diesel | 73 | 74 ±4 | 41.5 | 43-50 | 72.5 | 68-75 |
| Inline 6 | 93 | 87-93 ±3 | 48 | 45-56 | 82 | 82-98 |
| V8 | 93 | 93 ±4 | 48.5 | 45-56 | 84 | 82-98 |
| Flat 6 | 93 | 96 ±3 | 49 | 45-56 | 86.5 | 82-98 |
| V12 | 102 | 108 ±4 | 48 | 52-72 | 88.5 | 88-98 |
| V10, straight-through line, race | 108 | 112 ±6 | 68 | 56-61 | 107.5 | 98-108 |

These targets come from the Dutch RDW database (registration documents, values U.1 and U.3) and from *auto motor und sport*.
They say that a level is plausible, not that a sound is right.

**These figures do not say whether these positions and lines sound right: the ear decides.**

## Starting

Five attempts were rejected by ear: a whistle and hiss, "a vacuum cleaner" (« un aspirateur »); pure sine waves, "an alien
spaceship" (« un vaisseau extraterrestre »); random impacts on soft resonances, "pebbles in a plastic box" (« des cailloux dans
une boîte en plastique »); friction on very narrow resonances, "a latex balloon" (« un ballon en latex »); a loud note gliding at
constant volume, "a DJ drifting a CD" (« un DJ qui fait drifter un CD »). All of them had the right rhythm and the right average
spectrum: these measurements are not enough.

The model is now fitted to nine real starts: a Mazda MX-5, a Shelby GT500 and seven close-miked recordings from
[La Sonothèque](https://lasonotheque.org/search?q=demarrage) (including Peugeot 106 and 107, Citroën Saxo, Ford Courrier, Kia
Carnival and Iveco Daily). Sounds generated by ElevenLabs served as additional references, notably for the
V8: they guide by ear, without being worth a measurement. None of this is in this repository, and the site's sound remains
entirely simulated. `tools/analyze_starter.py` repeats part of these measurements (mesh line, rhythm, contrast) on your own recordings.

What these recordings taught:

- **It is mostly shaped noise, not spectral lines.** Above 800 Hz, the lines of the nine references cannot be distinguished from
  a random-phase control. Whitened, the residual is Gaussian: neither impulse trains nor comb, and a single peak, near the gear
  mesh frequency, below 800 Hz.
- **A fixed fine structure.** Bumps 40 to 100 Hz wide and 2 to 5 dB high, which do not move during cranking (correlation of 0.5
  to 0.97 between two halves of a recording, on seven references out of nine). Part of it may come from the room where the
  recording was made.
- **The attack.** A precursor at -22 to -35 dB, 8 to 20 ms of rise, a peak 10 to 15 dB above cranking (15 to 25 dB in the highs),
  fallen back within 30 to 60 ms, with several peaks. On a diesel, the peak is only 4 to 7 dB up.
- **Two distinct sources.** Between two compressions of a diesel, the mids drop by 12 to 19 dB, and the highs by only 3 to 7 dB:
  tooth contact follows torque, the brushes follow current.

Two hypotheses from the literature were not retained, for lack of finding them in these recordings: the two-inertia freewheel,
and a resonance that is more damped under torque.

What is simulated (`StarterSound`):

- **The starter depends on the engine.** Gear-reduction (small fast motor, planetary gearset, angled teeth) or direct-drive (big
  slow motor, straight-cut teeth, rougher); power, ring gear and pinion follow displacement.
- **The crankshaft.** A DC motor against compressions computed from the geometry, with gases that lose heat and unequal cylinders:
  the rpm and its swing emerge from this balance, and the model passes to the sound the torque that actually goes through the
  teeth. When this torque reaches zero, the teeth separate, then make contact again.
- **Contact noise and brushes.** The first follows torque, with a knee at 2 kHz; the second follow current. A weak, intermittent
  gear mesh line barely emerges from the noise.
- **The structure.** Everything goes through a dense response, specific to each engine and convolved: it gives the fixed bumps
  and the impression of a housing.
- **The sequence.** Several impacts: relay, plunger, pinion, contact resuming. Then the engine catches, the pinion withdraws, and
  the starter winds down discreetly.
- **The engine catches.** The engine computer enriches the mixture: the engine starts even from very low, climbs to 1.45 times its
  idle, which comes back down in about a second.

The fit is automatic (bounded CMA-ES on nine starts, checked on five others), touched up by hand on the attack, the V8 and the
diesel. Known limits: the V8 is missing lows below 400 Hz, probably the air pumped into the exhaust, and the moving arcs of one of
the recordings near 2.6 kHz are not reproduced. The V8 rests on very few recordings, and the 2 L diesel on 3 L recordings. Since
the sound is mostly noise, its main risk remains "the vacuum cleaner", which only the ear can rule out.

## The stroke

The stroke is not a stored setting: `V = π/4 × bore² × stroke × cylinders` derives it from the displacement and the bore, which remain what the share link keeps. The slider is another way of writing the displacement, as on a real engine: a longer crankshaft on the same bore, a stroker. When it moves, the bore stays and the displacement adjusts, rounded to a whole number; when the displacement, the bore or the number of cylinders moves, it follows. `web/src/lib/stroke.ts` does the sum, and so does the CLI's `--stroke` (`displacementForStroke`).

**Limits.** From 40 mm (a 2.4 L Formula 1 V8, 98 mm bore, so 39.8 mm) to 130 mm (Bugatti Royale, 125 × 130), narrowed so that the displacement stays between 600 and 7000 cc. The 0.5 mm step keeps the round trip exact: on the smallest cylinder, rounding the displacement moves the stroke by less than 0.09 mm, less than half a step. Going back to the starting notch restores the starting displacement, including a preset's. Beyond those limits, a preset, a link or the other two sliders are still allowed: the text gives the real stroke, the slider rests at its end, and nothing is rewritten until you touch it. The views draw to scale everything the slider can reach, from 0.36 to 2.2 bores.

**What the stroke really changes.**

- *Torque*, only through mean piston speed: above 24 m/s, `breathing` loses `0.012 × excess²`, without going below 0.55. It is a good indicator: flow through the valves, whose area follows the bore, reads on piston speed (Taylor's Mach index). A 39.8 mm Formula 1 V8 at 18,000 rpm does 23.9 m/s. Below this limit, at equal displacement, the curve does not move.
- *The sound*, by two paths that move together at constant displacement, separated by removing each in turn. The stroke sets the length of the pulse (`blowdownBase`, `√stroke`, for the same expelled volume): behind the car, at full throttle, the level loses 1.9 dB from 69 to 110 mm, and with this path removed it no longer moves (89.8 to 89.9 dB(A)); at the end of the pipe, the 8-16 kHz band gains 1.9 dB. The bore sets the block noise (Anderton's law, `60 log10 bore`): in the engine bay, 108.0 dB(A) at 96 mm bore, 102.2 at 76, and 105.2 throughout if it is removed. The mean flow and the line follow the displacement. More treble from a longer pulse is not the expected direction: it follows from the model and has not been checked against real engines.
- *The drawing and the gas*: the crank, the rod (1.6 strokes), the block height, the volume of each cylinder and the piston area.
- *The displacement*, if the bore stays: everything that follows from it.

**At 2 L and 4 cylinders, 6800 rpm** (compact body, sport line):

| | Oversquare, 96 × 69.1 | Square, 86 × 86.1 | Long stroke, 76 × 110.2 |
|---|---|---|---|
| Peak torque | 173.6 Nm at 4100 rpm | same | same |
| Peak power | 132 hp at 6250 rpm | same | same |
| Piston speed at redline | 15.7 m/s | 19.5 m/s | 25.0 m/s |
| WLTC | 8.85 L/100 km | same | same |
| 90 km/h | 6.70 L/100 km | same | same |
| LAeq, full throttle | 90.9 dB(A) | 90.0 dB(A) | 89.0 dB(A) |
| 8-16 kHz band, full throttle | 17.1 dB | 17.9 dB | 19.0 dB |
| LAeq, engine bay | 108.0 dB(A) | 105.2 dB(A) | 102.2 dB(A) |

At 8500 rpm, the long stroke reaches 31.2 m/s: 182 hp at 6750 rpm instead of 191 hp at 7800.

**What the stroke does not change, and why.** Three real effects are missing, and adding them would move every fingerprint, since every engine has a stroke: friction (`frictionTorqueNm` follows engine speed, not piston speed), inertia (`engineInertia` follows displacement, not crank radius or piston mass) and the speed of peak torque (a fixed share of redline). Consumption at equal displacement therefore does not depend on the stroke. Adding friction alone to consumption would be no more correct: a long stroke rubs more but loses less heat through its walls and burns faster (Filipi and Assanis, *International Journal of Engine Research* 1(2), 2000, read only through second-hand summaries). The model has neither, and showing only one would give the wrong direction: that balance remains to be done. The 0.55 floor remains a modelling choice: above 30 m/s, no real engine gives a reference.

## The gearbox

A part you choose and tune (`GearboxConfig`), independent of the engine: the same one can be fitted to two engines, including an
ill-suited gearbox. A zero value lets the manufacturer decide: the final drive and the torque rating then adapt to the car and the
engine, which is what the presets do. Choose a final drive and a torque rating, and nothing depends on the engine any more.

**Teeth, not ratios.** Each pair has whole numbers of teeth, coprime if possible, at a roughly constant center distance. Ratios,
speeds and gear mesh frequencies follow from them. The center distance follows the torque rating (70 mm for 300 Nm, scaling with the
cube root), and the module sets the number of teeth. A module too small for the torque requested lowers what the gearbox accepts:
the first-gear pinion tooth breaks first (Lewis). A front-wheel-drive final drive is a helical pair; in rear-wheel or all-wheel
drive, the gearbox is longitudinal (one constant-mesh pair, a direct drive if a ratio falls at 1) and the final drive is a bevel gear
set.

**An ill-suited gearbox:** too short, the rev limiter arrives before top speed; too tall, the car drags and stalls in manual. Beyond
what it accepts, it wears according to Miner's rule, with the sixth power of torque: 30 s just above its limit, about half a second at twice its limit. Then a tooth gives way.

**The types.** Synchros: the cone brings the shafts to the same speed, T = µFR/sin α (Socin and Walters), in a time J·Δω/T. So it
takes longer across a large gap. Dog gearboxes: no cone, the dogs drop in as long as the speed difference allows, and the impact is
worth ½J·Δω². Sequential: barrel, under 100 ms, ignition cut on upshifts, pedal to the floor; the clutch stays closed and slips under
the impact. Dual-clutch: the next gear is already engaged, torque passes from one disc to the other with no gap. Automatic: torque
converter, which multiplies by two at launch, then locks up. Electric: a two-stage reducer.

**A shift has stages:** declutching, leaving the gear (impossible under load, the teeth are pressed together), travel, synchronization,
engagement, re-engaging the clutch. With assist, the lever works the clutch. With the pedal, shifting without declutching grinds on a
downshift, because the cone does not brake a whole engine; on an upshift, you just wait for the rpm to fall, like a double-declutch. A
blip of throttle in neutral, with the clutch engaged, brings the input shaft back up: that is the double-declutch.

**No more timer in place of this part.** The input shaft is a real inertia: disc, shaft, gears. In neutral, it turns with the engine if
the clutch is closed, which is the case with the pedal up. The driveshafts twist (6000 Nm/rad): the natural mode goes from 2 to 3 Hz in
first gear to about ten hertz in the top gears, and gives the jerks when you get back on the throttle. Efficiency comes from the stages
(helical 98.5%, straight-cut 99%, bevel 95.5%) and from a speed-dependent drag.

### Its noise

Nothing is played back: everything is a force on the bearings, going through a single fixed response, that of the casing. More than 99%
of this noise travels through the structure (Rust, Brandl and Thien, 1992), and the spectral lines sweep past its resonances. The sound
never reads the engine rpm to make the teeth whine: only the shaft speeds and the torque. It has its own random generator, and without
`setGearbox` it stays silent and costs nothing.

- **The whine** is the transmission error of the loaded pairs, tooth by tooth. The depth of each harmonic comes from the contact ratios:
  2 Sinc(k εα) Sinc(k εβ) (Velex, 2012), plus a remainder due to defects, because a whole-number contact ratio erases nothing on real
  teeth. With torque, it passes through a minimum where the profile is modified, as on the NASA test rig (TM-105331), and it depends on
  the flank: the coast flank is less carefully finished. Off load, it dies out. On a road gearbox, the lines stay discreet; with
  straight-cut gears, they are clearly heard: 16 dB of difference on average at steady rpm, from 7 to 22 dB depending on load. NASA
  measures 7 to 17 dB, 22 at best (TM-106511).
- **It is not a sine wave.** Measured on real reverse gears (Peugeot 106, Saxo, Golf), straight-cut even on a road car: a line beats by 2.6
  to 4.5 dB from one 46 ms window to the next, and sometimes vanishes for a whole second; as much energy in a ±100 Hz skirt as in its peak;
  the harmonics drop by about 3 dB each. Hence a once-per-revolution runout and a per-tooth defect, which come back every pinion × wheel
  teeth, a skirt and a swing on each harmonic, and a speed never held better than a few centimeters per second.
- **The rattle** is not triggered: the idler gears are simulated within their backlash, shaken by the cyclic irregularity of the input
  shaft. It arises above Rust's threshold, J·α > drag, and goes quiet as soon as you declutch, because nothing is shaking the input shaft
  any more. A V12 runs too smoothly for this, a diesel with a single-mass flywheel rattles, a dual-mass flywheel removes it, and so does a
  torque converter. The impacts fall in phase with the firings.
- **Shifts:** the cone rubbing, the sliding sleeve (two impacts, the double bump), the engagement impact, the backlash take-up when torque
  changes direction, much stronger on wide dog teeth. When the sleeve is forced, a tooth strikes on each pass, irregularly, at the frequency
  of the speed difference, and the lever bounces near 120 Hz: measured on a real grind, like the casing bumps near 700 Hz, 2.1, 4 and 6 kHz.
- **Reverse** is straight-cut with no profile modification in the simulator, as on many cars: its defects dominate even at low load, which makes it the loudest
  gearbox noise on a road car.
- **The rest:** the oil and the bearings, the slipping clutch, the release bearing with the pedal down.

Accepted limits. The real module and helix angle of production gearboxes cannot be found: they are assumed. No isolated recording of rattle at
idle has been found: its shape comes from the literature (one burst per firing, above 1 kHz), and from an idle of a rally car whose highs beat
at the firing rate. The sound does not follow oil temperature or wear, and automatic gearboxes have no planetary gearset: their lines are those
of a parallel-shaft gearbox. **The measurements say that the structure is that of real gearbox noise, not that it sounds right: the ear decides,
**

## The automatic's shift modes

Five modes (`ShiftMode`, `core/src/shift_logic.cpp`). No rpm is written per car: every point comes from `ShiftMap`, computed whenever the engine or the car changes from the torque curve, peak torque and power, and the gear steps. **Normal stays the original logic, to the bit** on a flat road: the `golden_lap_*` fingerprints did not move because of it.

| | Upshift point, light foot | Full throttle | Downshifting | Other |
|---|---|---|---|---|
| **Eco** | just enough to land above its floor | between peak torque and peak power (35%) | kickdown at 96% pedal; on the brakes, below 30% of max rpm | also upshifts on lift-off; lock-up from 84% speed ratio |
| **Normal** | `max(2.6 × idle, 0.38 × max)` | 96.5% of max | kickdown at 85% below 42% of max | original |
| **Sport** | above the floor (55% of the peak-torque rpm), times the gear step | where the wheel forces cross, between 97.9 and 98.5% of max | kickdown at 65%; on the brakes as soon as the engine stays below 75%; throttle blip | no upshift in a corner; remembers the foot for 4 s |
| **Track** | at most 92% of its full-throttle point | 99.5% of max, practically the limiter | kickdown at 45%; on the brakes below 88% | no upshift in a corner; remembers the foot for 10 s; shorter shifts; lock-up at full load |
| **Paddles** | never by itself | at the limiter (99.8%), to protect the engine | below its floor, so as not to stall | the driver decides; `requestShift` refuses what would over-rev; the autopilot does not use them and drives in Normal |

At the same pedal, Eco < Normal < Sport ≤ Track at every demand and in every gear (`upshiftPoint`, checked on all presets). **Demand** is the pedal position; Sport and Track add its rate (a pedal going down fast counts ahead) and its memory (lifting for a moment does not bring back gentle shifting). Braking and cornering raise it: the gear stays for the exit.

**The crossing** is the rpm where the next gear pulls as hard at the wheels as this one, computed on the curve. Sport uses it while it stays below the limiter, but on these curves it only falls below maximum rpm in the V10's top gears: nearly everywhere, pulling to the end is what pushes hardest. So Sport shifts beyond peak power, not at it. And Normal already goes to 96.5% at full throttle: the modes differ most at half pedal, not on the floor, where Normal, Sport and Track are a hundred rpm or so apart. Measured, rpm when the 1st-to-2nd shift starts, from rest:

| Engine | Pedal | Eco | Normal | Sport | Track |
|---|---|---|---|---|---|
| City 1.4 | 60% | 2890 | 3664 | 5393 | 6059 |
| | 100% | 4401 | 5985 | 6109 | 6171 |
| Six (8000 rpm) | 60% | 3495 | 4728 | 6846 | 7832 |
| | 100% | 5817 | 7725 | 7883 | 7964 |
| Diesel four (4500 rpm) | 60% | 2451 | 2987 | 3898 | 4401 |
| | 100% | 3165 | 4345 | 4435 | 4480 |

**To avoid hunting:** it only upshifts if the engine lands above the floor (2% margin), and if the next gear pulls, 10% below the landing rpm, a share of the road resistance chosen so as not to fall back under the kickdown threshold (1.25 to 2.6 times). It only downshifts to accelerate if the engine stays below 88% of the lower gear's upshift point and if the wheels gain from it, given what the tyres can transmit: one gear less on spinning wheels brings nothing. Right after a shift, it only goes back if the car has really stopped pulling. And if the road changes under the gear (a climb, a trailer) so that it no longer holds the reserve it needed to be chosen, the foot asks for more than that reserve leaves (above 1/reserve) and the car no longer gains (under 0.3 m/s²), it takes the lower gear back, if it pulls more and would not upshift at once: below the kickdown threshold, nothing else did it. A weak car thus stayed in 4th at 1500 rpm on a 3 % climb, gaining 0.25 km/h per second, when 3rd pulled 1.8 times more at the wheels; the eight journey fingerprints (inline six, from Eco to Track mode) did not move. All this applies to the modes other than Normal, and to Normal as soon as the road is not flat (slope, trailer): on the flat, its original logic has none of these guards, and can still hesitate for a moment between 1st and 2nd at a very light pedal.

**Shift duration** stays the gearbox's own, with one deliberate exception: Sport (0.85) and Track (0.7) shorten the piloted clutch and the handover, and cut more torque during an upshift on a dual-clutch or torque-converter box. That is what real controllers do: more hydraulic pressure, a firmer clutch, at the cost of a harder shift. A sequential gains nothing: its barrel has its own duration. A manual gearbox is never touched.

**Torque-converter lock-up** depends on the mode: from an 84% turbine/pump speed ratio in Eco (90% elsewhere), and up to full load in Sport and Track, from first gear in Track.

**Consequences:** 0-100 is always at least as good in Sport as in Eco (checked on every preset and gearbox); on a city journey, Eco uses less fuel than Sport (`consumption()`).

## The autopilot's journeys

`step` follows a journey (`DriveScenario`, `core/src/driver_scenarios.cpp`): a list of data, with no logic. A leg gives its start, its target speed, the most pedal, the slope, the corner radius and what to call it. The journey also has a driving style (pedal rates, gain, braking margin and force, anticipation, accepted lateral acceleration) and may pull a trailer. `City`, the first, is the original loop, values unchanged: its fingerprints did not move.

| Journey | What you hear |
|---|---|
| `city` | city, traffic light, expressway, overtaking: the original loop |
| `stop_go` | a jam: first and second, very little throttle, never above 40 km/h |
| `country` | straights at 90, bends of 45 to 150 m, small hills |
| `motorway` | slip road at full throttle, cruise at 130, overtake with a kickdown, an incline, the exit |
| `mountain` | 8 to 10% going up, 18 m hairpins, then the way down |
| `drag` | 16 s at full load from rest, hard braking, a slow roll back |
| `track` | full load, heavy braking, corners at 0.95 g; does not go through a standstill on every lap |
| `hill_start` | stopped on 12 then 16%, pulling away |
| `towing` | 1500 kg more to pull, a 5% climb, the descent |

**What the car feels:**

- **The slope** adds `m g sin θ` to the resistance, and loads the rear like an acceleration: a rear-drive car spins less there than a front-drive one. It changes progressively, at 2% per second.
- **The corner** limits speed to `√(a R)`, with the driver's lateral acceleration `a`, and costs 5% of the lateral force (tyre slip). It also makes the sporty modes hold their gear.
- **The trailer** adds its mass to accelerate and to brake (it has its own brakes), its tyres and its air.

**The driver** keeps the original calculation (pedal to hold the speed, plus the error to the target), and on the new journeys adds anticipation: the speed not to exceed now in order to reach the next leg's, at the intended deceleration. That is what makes it brake before the corner. `underpowered()` says when, foot to the floor, the car does not reach the target; the site shows it. It also says so when the driver, in a hurry (a leg where the pedal goes to at least 0.7), is at the pedal ceiling and at this rate the car will not reach the leg's speed before it ends: it still accelerates, but lacks the strength. This signal changes nothing in the physics.

**Changing journey while driving** keeps the speed: the new journey is joined at its first leg that moves, and the slope arrives progressively.

**What the model cannot do:** the car does not roll back (speed stays positive), so a hill start assumes hill hold. There is only one grip: no tyre width, no downforce, no lateral load transfer. A corner is therefore no faster on a wider car. Line and position on the road do not exist: a leg lasts a time, not a distance, and a weak car simply does not reach its speed before the leg ends. The modes rest on steady-state curves: turbo lag does not enter the computation of the points.

## Stalling

With a manual gearbox, nothing protects the engine. Below idle, combustion becomes unstable and torque collapses. Too low in gear, each firing
gives a jolt: the car judders. Below 68% of idle, if the clutch connects the engine to the wheels, it stalls: no more combustion, and the starter
has to be run again. Pressing the clutch avoids it. The only exception, to make it possible to pull away on the keyboard: in first gear, the clutch
stays assisted until it has gripped. The controlled clutch of an automatic gearbox opens in time.

## The rev limiter

A test protects it: without the cut, the sound only varies by 4 to 6 dB and resembles a held note.

- **Clean cut:** the pressure falls by 95% at the next firing, with no smoothing, and whatever follows the flow falls with it: pumping, hiss,
  intake, rumble. The sound stutters with 15 to 20 dB of contrast.
- **Never two identical bounces:** the engine computer resumes to within one firing.
- **No turbo dump:** the throttle stays open, so there is no vacuum to open the valve. A test checks this on the model.
- **Pops:** fuel still arrives during the cut, and can ignite when it resumes.

## Lifting off

- **Fuel cut:** exhaust pressure drops, the engine is now just a pump.
- **Pops:** unburned fuel ignites in the pipe, especially just after lifting off, never at idle, under load or on a diesel. It is a wave that makes
  the pipe resonate according to its length and the displacement. Strength, brightness, duration and frequency depend on the exhaust. A first attempt
  using white-noise cracks sounded like "a pebble on the underbody" (« un caillou sur le fond de caisse »).
- **Turbo:** the blow-off valve opens under manifold vacuum and empties the duct; without it, air comes back through the compressor in bursts (flutter). The shaft keeps its momentum: its whistle falls slowly, faster when the valve still lets it pump air. See "The turbo".

## Consumption

Fuel, oil and energy are a **reading** of what the engine does: they are integrated at every 1 ms step of `DrivingModel`
(`core/src/consumption.cpp`), and nothing flows back into the physics. Mass does not fall as fuel burns, and the sound does not change: the golden hashes are the same with and
without the meter, checked by removing it on a private copy.

### The method: an energy balance

```
fuel power = (brake torque + friction + pumping past the throttle) × ω × enrichment / indicated efficiency
```

- **Brake torque and friction:** those of the model, at each step.
- **Pumping:** it comes from a pressure difference, so never more than one atmosphere across a closed throttle, that is at most 1 bar of mean
  pressure. It depends on manifold pressure, itself set by the work asked for: a straight line, solved directly. Above full atmospheric load there is no throttle. A diesel has none.
- **Indicated efficiency:** 0.405 for naturally aspirated petrol, 0.375 for turbo (lower compression), 0.46 for diesel. It loses up to 8% below 2000 rpm (the gas gives heat to the walls)
  and up to 22% at low fill (10% on diesel).
- **Enrichment:** from 85% of full load, down to λ = 0.88 naturally aspirated and 0.82 turbo, the more so at high power. Diesel stays lean.
- **Fuel:** petrol 0.745 kg/L, 42.9 MJ/kg, 3.17 g of CO₂ per gram; diesel 0.832 kg/L, 43.1 MJ/kg, 3.15 g/g. Common values, quoted from memory and not re-read in the JEC report
  I was looking for; only the diesel row of an Irish EPA table was seen (43.31 GJ/t, 73.3 t CO₂/TJ). Check: Ford states 268 g/km for 11.8 L/100 km, or 2.27 kg/L, against 2.36 kg/L here.

Without any tuning, this produces: zero on the limiter, engine off, stalled, or foot off in gear (torque as negative as a motored engine's), a non-zero idle, a full load dearer than the
best point, a low load dearer still, a big engine using more at the same speed, a short gear using more, and a slipping clutch or tyres burning for nothing. **The cold engine is in "Engine temperatures":** the
figures below are warm.

### Why not air flow, and what it confirms

The second method starts from displacement, rpm and fill. It serves as a check (`lambdaAverage`, shown by `--fuel 1`). Since the model's torque is proportional to fill, it amounts to dividing `fill × full load` by an efficiency.

It was not chosen because that fill carries a **pumping term of 3.2 bar** (`pumpingTorqueNm`): more than a pressure difference can give, because it also sets the engine braking. At low load the model therefore opens the throttle
wider than a real engine. If both methods agreed, λ would be 1 on petrol. Measured:

| | Idle | 90 km/h | 120 km/h | WLTC |
|---|---|---|---|---|
| City car 1.4 | 0.51 | 1.16 | 1.12 | 1.13 |
| 2.0 naturally aspirated | 0.43 | 1.01 | 0.96 | 1.01 |
| 2.0 turbo | 0.45 | 1.05 | 1.03 | 1.03 |
| V8 5.0 | 0.48 | 1.22 | 1.15 | 1.13 |
| Diesel van | 8.0 | 3.0 | 2.9 | 3.4 |

On the move, the two methods agree within 25%. Air flow would give more fuel, further from published figures: 7.9 L/100 km on WLTC for the city car, instead of 7.0. At idle the comparison is worthless: the model's fill only balances friction there, without pumping.

### Oil

It passes the rings and valve guides, and evaporates from the liner. The model is an **order of magnitude, within a factor of three**: `(0.6 × litres × krpm + 0.1 × kW)` g/h, times 1.3 with a turbo and 1.2 on diesel, while the engine turns, even without combustion.
Those two factors are assumed: the source says "more", with no figure. Age, oil level, pressure and temperature are not simulated.

Sources disagree with each other. The Kolbenschmidt/MS Motorservice booklet (read) says all of: 0.5 to 1 g/kWh on a test rig for good sealing, under 0.05% of fuel on a modern engine (40 mL/1000 km at 8 L/100 km), and a 0.5% limit (0.4 L/1000 km).
Yet 0.05% of fuel is about 0.13 g/kWh. The 0.1 to 0.5 g/kWh range in the original request was not found in a source I read; GM allows 1 quart per 2000 miles (seen quoted). The model sits between them: see the table.

### Electric

Power at the battery: motor power divided by an assumed efficiency of 0.90, and multiplied by it when recovering. No fuel, oil or CO₂. No charging losses, heating or auxiliaries.

### Against published figures

Nothing is tuned preset by preset: only three efficiencies and two oil coefficients are global choices.
Measured with `revforge-render --out none --preset X --scenario idle|cruise|wltc [--kmh N] --fuel 1`, hot, no start-stop, flat road, with the model's automatic gearbox. The WLTC is the real class 3b trace. Figures measured with the new turbo model, still being tuned: turbo presets may move by 1 to 2%.

| Measure | Preset | Target | Source | Model | Gap |
|---|---|---|---|---|---|
| Best g/kWh, petrol | `city_14` | 225 to 250 | Wikipedia: 1NZ-FXE, Saturn (citing Muta et al.) | 245 | in range |
| Best g/kWh, turbo | `hot_hatch_turbo` | 245 | Wikipedia: Ford EcoBoost | 240 | −2% |
| Best g/kWh, diesel | `diesel_four` | 197 to 205 | Wikipedia: 1.9 TDI, N47 | 196 | just below range |
| At peak power, petrol | `city_14` | no source read | | 304 | |
| Idle, 2.0 L petrol | `four_cyl_sport` | 0.61 L/h | DOE, Fact #861 (seen quoted) | 0.77 | +26% |
| Idle, V8 | `v8_cross_plane`, 5.0 L | 1.48 L/h for a 4.6 L | DOE (seen quoted) | 1.83 | +24%, +14% scaled to displacement |
| Idle, 2.0 L diesel | `diesel_four` | 0.64 L/h | DOE (seen quoted) | 0.46 | −28% |
| 77 km/h, 1.4 TSI | close settings, 1270 kg | 4.5 L/100 | km77, Golf VI 1.4 TSI DSG (read) | 5.1 | +12% |
| 98 km/h, 1.4 TSI | same settings | 5.6 L/100 | km77 (read) | 6.2 | +10% |
| 120 km/h, city car | `city_14` | 6.0 to 6.4 | km77, i10 1.2 84 hp (seen quoted) | 7.35 | +15 to +23% |
| 120 km/h, GTI | `hot_hatch_turbo` | 7.1 | km77, Golf GTI 220 hp, hilly route (read) | 8.31 | +17% |
| 120 km/h, diesel | close settings, 1400 kg | 5.9 | km77, Golf GTD, same route (read) | 7.3 | +24% |
| WLTC, city car | `city_14` | 5.3 to 5.5 | Sandero 1.0, i10 1.2 (listings, seen quoted) | 6.76 | +23 to +28% |
| WLTC, GTI | `hot_hatch_turbo` | 7.1 to 7.3 | Volkswagen (seen quoted) | 8.90 | +22 to +25% |
| WLTC, V8 | `v8_cross_plane` | 11.8 to 12.1 | Ford, Mustang EU sheet 2022 (seen quoted) | 13.9 | +15 to +18% |
| WLTC, diesel | `diesel_four` | 6.9 to 8.3 | T6.1 2.0 TDI, auto-data (seen quoted) | 7.58 | in range, the driver restarted the engine once |
| WLTC, electric | `electric`, 1800 kg | 13.0 to 14.4 kWh | Model 3, Tesla and FIA (seen quoted, at the plug) | 17.5 kWh, at the battery | +22 to +35% |
| Oil, share of fuel, WLTC | `city_14` | under 0.05%, limit 0.5% | MS Motorservice (read) | 0.10% | twice "usual" |
| Oil, mL/1000 km, WLTC | `city_14` | 40 to 400 | MS Motorservice (read) | 62 | in range |
| Oil, g/kWh, motorway | `city_14` | 0.13 to 1 | MS Motorservice (read, and derived) | 0.23 | in range |

"Read": the page itself. "Seen quoted": a search excerpt or a repeat, without the original document. 90 km/h and 130 km/h have no source read:
the model gives 5.8 and 8.3 L/100 km on the city car, 7.5 and 9.3 on the GTI, 10.9 and 11.9 on the V8, 6.4 and 10.3 on the diesel van.

### What misses

**The model reads high on the road:** about 15 to 25% at steady speed and on WLTC, except the diesel van. The 77 km/h, 98 km/h and custom diesel rows date from before the rolling resistance correction, which lowers them a little; they were not remeasured. Official figures include a cold start (which costs) and often start-stop (which saves), both absent here: they pull in opposite directions. The cold start, measured since with `--start cold`, costs 1 to 3% over the cycle here (see "Engine temperatures"); start-stop is not measured. On the city car's WLTC, the 29% gap splits into:

- a little energy to supply, despite rolling resistance now at 0.0095: the preset weighs 1100 kg and loses 7% in the gearbox, plus its drag;
- about 8% of efficiency: 361 g/kWh over the cycle, against an estimated 335.

The model's automatic gearbox matters little: a driver following longer gears, as the regulation does, only gains 3 to 8%.

**Electric still reads high:** CdA of 0.70 against 0.47 on a Model 3, and a figure at the battery compared with official figures at the plug, which include charging losses and so flatter this comparison. The rolling and motor drag corrections took it from 22.2 to 17.5 kWh/100 km.

**Diesel idle reads low, petrol idles read high,** and all three turn at 850 rpm, higher than some real engines. No alternator or accessory is simulated.

**With the automatic gearbox, the engine can still die** on `diesel_four`, once during the WLTC, rolling at low speed in first with the clutch open: its idle control does not catch it. It now declares a stall, so the site offers a restart, but the cause remains.

**Turbo and gearbox settings count:** these figures follow the model's real torque and pressure, and will move if those parts change.

## The 80s body

`Body::Retro` is a sharp-edged three-box coupe. It adds no computation: `BodySpec` gives its figures and the rest follows: drag, top speed, final drive, source placement, line length, driver's head. The other bodies do not move.

| Quantity | Chosen | Where it comes from |
|---|---:|---|
| Cd | 0.40 | Wikipedia's list (read): Capri III 0.374, 280ZX 0.38, Mark VII 0.38, Cougar 1983 0.40, XJS 0.40, 1982 Volvo 740 0.41; but Sierra 0.34 and E30 M3 0.33: not everyone was there. Picked inside the requested range, at the top of a wider real one |
| Frontal area | 1.85 m² | **Assumed.** No published value read. The 0.84 × width × height rule (seen quoted, without the original page) would give 1.88 m². Check: the Volvo 740 Turbo's CdA of 0.808 m², read, with Cd 0.41, implies 1.97 m² for a bigger car |
| CdA | 0.74 m² | The only figure the physics reads: Cd and area are not separate there |
| Dimensions | 4.25 × 1.65 × 1.36 m, 2.50 m wheelbase | E30 325i coupe (Auto-Data, read): 4.325 × 1.645 × 1.38 m, 2.57 m. Capri 2.8i (seen quoted): 4.35 × 1.70 × 1.30 m, 2.565 m. Shorter than both: a **choice** of a short wheelbase |
| Mass | 1100 kg when choosing the body, 1150 kg in the preset | E30 325i: 1145 kg (Auto-Data, read), 1180 kg (seen quoted). Capri 2.8i: 1170 kg (seen quoted) |
| Rims | 14 inches | 5.5J × 14 with 195/60 R14 on the 325i (read). The radius stays the shared formula: 273 mm, for about 295 mm in reality |
| Gearbox | 5 speeds | Common at the time, with no source read to pin it down: the Capri came with 4 and 5 speeds (seen quoted) |
| Centre of gravity, front share, shift time | 0.52 m, 53 %, 0.36 s | **Assumed**, between the coupe and the city car |
| Line | 0.98 times a compact's | Follows length, like the other bodies |
| Insulation | Sporty when choosing the body | **Assumed:** little trim, thin glass. No period cabin measurement read. The site sets it when you choose the body and takes it back when you leave, unless you changed it by hand. On the command line, `--body retro` does not change it: give `--insulation light` |

The `retro_coupe` preset is a 2.5 L inline six, 84 mm bore, 6400 rpm, single cam. Nothing in it is tuned to match a published figure, and the model gives less power than the reference: 137 hp and 208 Nm, for 171 hp and 222 Nm (Auto-Data, read). These figures count the single cam: see "The single cam".

| | Coupe, preset | Compact, same engine, compact settings | Compact, same engine, same mass and rims |
|---|---:|---:|---:|
| Cd, area | 0.40, 1.85 m² | not entered | not entered |
| CdA | 0.74 m² | 0.70 m² | 0.70 m² |
| Mass, rims, drive | 1150 kg, 14", rear | 1350 kg, 18", front | 1150 kg, 14", rear |
| Gears | 5 | 6 | 6 |
| Top speed | 207 km/h | 212 km/h | 210.5 km/h |
| 0 to 100 km/h | 8.75 s | 9.71 s | 8.58 s |
| WLTC | 9.65 L/100 km | 9.67 L/100 km | 9.56 L/100 km |
| 120 km/h | 9.51 L/100 km | 9.13 L/100 km | 9.21 L/100 km |
| Cabin, idle, full throttle | 43.9, 73.3 dB(A) | 39.6, 66.9 dB(A) | with the same insulation: 44.4, 72.9 dB(A) |
| Outside, at 5 m, full throttle | 77.4 dB(A) | 77.7 dB(A) | 77.7 dB(A) |

Measured with `measurePerformance`, `--fuel 1` and `soundSheet`. Levels remain estimates.

Against published figures: 8.3 s and 217 km/h with a manual (E30 325i, seen quoted, Cd given as 0.38), 10 s and 206 km/h with a 4-speed automatic (Auto-Data, read, Cd given as 0.35); 8 s and 210 km/h for the Capri 2.8i (seen quoted). Consumption: 12.7 L/100 km urban and 6.8 extra-urban for that automatic, under a period standard that is not WLTC.

**What the body changes little:** the compact's CdA of 0.70 m² is already high for a recent car, and a small frontal area makes up for a poor Cd. The drag gap is therefore only 6 %. That is worth 3.5 km/h at the top and 3.3 % at 120 km/h; in WLTC, at low speed, the gap blends with that of the gearing. In the cabin, nearly all the gap comes from insulation: with equal insulation, the two bodies are within 1 dB.

**What is missing:** rolling resistance (0.0095) and grip are those of recent tyres, for every car. The engine has no carburettor or period ignition: it is the same torque model, and its consumption keeps the gap of the "Against published figures" table.

## The air filter

Four filters (`AirFilter`, `core/src/air_filter.cpp`): stock paper in its airbox, a sport panel in the same box, an open cone, velocity stacks. Paper is the reference: the torques are calibrated on it, and with it none of these calculations runs. Everything else is written relative to it. The setting does nothing on an electric motor, which has no filter.

**Pressure drop** follows `ΔP = k·ṁ²`. At the design point, 0.11 kg/s, that is 2 L at 6000 rpm:

| | Drop | Where the figure comes from |
|---|---|---|
| Paper and box | 3 kPa | Set from the best filter fitted in a stock airbox: 2.56 kPa at 6 m³/min (fastcar.co.uk, a magazine flow bench, read in summary). Already the turbo's `kRatedFilterPa` |
| Panel | 2.55 kPa | Same box, freer element. A stock box with no element at all still loses 1.25 kPa (5 inH₂O: Bob Is The Oil Guy, an amateur test, read in summary): removing paper does not remove the box |
| Cone | 1.21 kPa | The most restrictive cone on the same fastcar.co.uk bench, at 6 m³/min |
| Stacks | 0.2 kPa | Assumed: a rounded mouth and hardly any mesh |

`k` depends on the engine. A box grows like a box, its area as V^(2/3): a 5 L does not get a box 2.5 times larger than a 2 L, and it loses more in it. A manufacturer still stays under 5 kPa on a clean filter, since the usual limit for a loaded filter is 20 to 25 inH₂O, 5 to 6 kPa (order of magnitude read in a filtration patent). On a turbo, `k` is the existing one, 3 kPa at rated flow, multiplied by the ratio from this table.

**Temperature.** With no box, the filter breathes underbonnet air. Its rise follows a balance: what is rejected follows power, so engine speed, and what ventilates follows car speed, with a fan share (`kFanShare`, 35%, assumed). At full load, with engine speed `r` relative to the rated point, `ΔT = ΔT₀ · r / (0.35 + 0.65·r)`. `ΔT₀` is 12 K with a front engine, 16 K at the rear and 20 K in the middle, where the exhaust is enclosed with the engine: **these values are assumed**. No controlled bonnet-closed test was found; accounts range from 10 to 30 K, and a bonnet-open dyno sees almost nothing, which is why a cone "gains" on the dyno and loses on the road. The temperature depends on neither load nor actual road speed: it is the full-load figure, the one the spec sheet also sees, with no thermal inertia. Heat soak at a standstill is not simulated, nor is clogging.

**Torque.** On a naturally aspirated engine, the air mass follows the pressure and density behind the filter, and acts only on the work of the gases: `T = (T_ref + T_friction)·r_fill − T_friction`, minus the change in pumping (`ΔP·V/4π`). On a turbo, the wastegate holds the pressure: the drop takes no torque on the plateau, it changes the shaft. Air enters the compressor at `inletPa` and at its temperature, which changes the reduced flow, the head and the compressor outlet (`TurboSpec::inletFilter`). Only the charge temperature touches torque, softened by the intercooler (`turboChargeDensityShare`). The turbo stays sized on the stock paper, and so does the gearbox: changing filter does not change the ratios (`gearingOf`).

**Stacks.** Their length is chosen: tuned to 72% of redline, it comes to 28 cm on a 2 L at 6800 rpm, a quarter wave of `f = c/(4(L + 0.61a))`, 294 Hz. The wave returning in the tube changes what the valve finds as it closes (inertial tuning: Heywood, *Internal Combustion Engine Fundamentals*, § 7.6, cited from memory and not re-read). The model takes `−A·cos(2π·f·25/N)` from this, damped with the number of round trips (`A` = 3%, `Q` = 6, **assumed**). The torque peak therefore moves towards the tuned speed, with a dip above it. A turbo's stack is a single mouth on the compressor inlet: no tuning, and no plenum removed.

**Sound.** This is the strongest effect. `IntakePath` (`core/src/intake_path.cpp`) computes both paths from valve to open air by transfer matrices: duct, box, element, snorkel, mouth for paper; short duct, element and cone for the cone; one tube per cylinder for stacks. Since the paper's level is calibrated, only their **ratio** is applied, at minimum phase, to everything leaving the intake (mouth noise, orders, turbo whistle and blow-off). There is no new noise and no new random draw. The box leaks through its walls (mass law, 2.5 kg/m²): that is what bounds the gain from removing it, and no level is typed in per filter. Above 3 kHz the ratio falls back: there the box was hiding a part of the source the model never described, and without this the stacks made white noise right by the block (8-16 kHz only 3 dB under 0.5-2 kHz at idle, 21 dB once fixed, against 14 dB with paper). Those 3 kHz are a choice, close to `kNoiseTopHz`.

Computed gain for a 2 L: the panel stays at 2 dB or less. The cone and stacks lose 1 to 7 dB below 100 Hz, where the box resonates on its snorkel, then gain 10 to 25 dB between 200 Hz and 2 kHz. With no box, the intake opens into the engine bay: it leaves with it (`Source::Bay`, bonnet closed), no longer through an inlet behind the grille. These levels are compared with no recording: **only listening will settle them**.

**The figures.** Computed by this model, compact car of 1300 kg (coupe of 1650 kg for the V8). The level is an estimated LAeq during an 8 s full-load launch, never a measurement.

| Engine | Filter | Peak torque (Nm) | Peak power (kW) | Air at peak power (°C) | Drop at peak power (kPa) | 0-100 km/h (s) | WLTC (L/100 km) | Cabin, launch (dB(A)) | In front, launch (dB(A)) |
|---|---|---|---|---|---|---|---|---|---|
| 2.0 L NA | Paper | 174 | 96.8 | 20 | 2.1 | 9.99 | 8.80 | 72.4 | 82.1 |
|  | Panel | 174 | 97.3 | 20 | 1.8 | 9.97 | 8.80 | 72.5 | 82.4 |
|  | Cone | 168 | 93.3 | 32 | 0.8 | 10.21 | 8.77 | 75.9 | 84.0 |
|  | Stacks | 173 | 93.6 | 32 | 0.1 | 10.16 | 8.77 | 77.6 | 85.0 |
| 5.0 L V8 | Paper | 473 | 275.1 | 20 | 4.7 | 6.28 | 15.81 | 77.6 | 84.8 |
|  | Panel | 475 | 277.9 | 20 | 4.0 | 6.26 | 15.81 | 77.6 | 85.4 |
|  | Cone | 463 | 270.8 | 32 | 1.8 | 6.37 | 15.76 | 80.7 | 86.2 |
|  | Stacks | 482 | 273.4 | 32 | 0.3 | 6.37 | 15.76 | 83.8 | 88.5 |
| 2.0 L turbo, 1.3 bar | Paper | 354 | 193.5 | 20 | 2.9 | 8.09 | 8.69 | 70.2 | 83.7 |
|  | Panel | 354 | 193.6 | 20 | 2.5 | 8.09 | 8.69 | 70.1 | 84.1 |
|  | Cone | 350 | 190.0 | 32 | 1.1 | 8.06 | 8.69 | 74.2 | 84.2 |
|  | Stacks | 350 | 190.2 | 32 | 0.2 | 8.06 | 8.68 | 76.5 | 85.5 |

On the turbo, at 6000 rpm: shaft at 176,700 rpm with paper, 176,500 with the panel, 179,650 with the cone. The time to reach 90% of the pressure at 2500 rpm goes from 1.31 s to 1.35 s with the cone: the hot air costs more than the lower drop brings. Yet this turbo's 0-100 gains 0.03 s with less torque: this 350 Nm front-driver spins its wheels, and with all-wheel drive the same cone loses 0.09 s (6.32 to 6.41 s). WLTC consumption barely moves, as expected: at part load the throttle does the restricting, not the filter.

At a held speed, the difference in the cabin depends heavily on engine speed, since body modes can cover everything: 0.4 dB at 5000 rpm, up to 9 dB at 6500 rpm for the cone, on the 2 L at full load. At 30% load the cone's stays under 3 dB; behind the car the exhaust stays in charge, within 1 dB. At idle the level moves by no more than 2 dB in the cabin, and 3 to 11 dB with the head above the engine: low-speed balance holds, and a quiet exhaust is not covered by a fixed background.

**The gas on screen** (`AIR_FILTER_GAS`, `fluidGas.ts`): friction follows the same ratio, the inlet section shrinks without a box, and the air enters hotter. The volume between compressor and throttle does not change. **The views** draw the box, a panel's frame, the cone, or one stack per cylinder with no plenum or central throttle. The vacuum gauge shows the intake air, only with a cone or stacks: it is `intakeAirK`, what the torque sees.

**Not modelled:** filtration and the wear it spares (stacks stop nothing), clogging, the knock that hot air encourages, a stack airbox fed with fresh air, and an engine computer compensating for density on a turbo.

## The single cam

The original model is a twin cam: `Camshaft::Dohc` goes through none of the calculations in this section, and no fingerprint moved. `Camshaft::Sohc` compares it with one overhead camshaft per bank, two valves per cylinder and rocker arms (`core/src/valvetrain.cpp`, copied for the drawing and the gas in `web/src/lib/valves.ts`).

**The section, from what fits in the bore.** Two valves side by side: d = (bore − 2 × 2.5 mm of edge − 5 mm of bridge) / 1.83, the exhaust being 83% of the intake. Four valves, each in its quarter, the intakes 100° apart and the exhausts 80°, with a 4 mm bridge. These margins are assumed, but they find real engines within 6%: 41.5 mm at 86 mm of bore (C20NE, 41.8, from memory), 36.1 and 32.2 mm at 93.4 (S38, 37 and 32, read). Lift follows the bore (0.12 times). The mean effective section puts the curtain at mean lift in series with the seat throat, with Cd = 0.7; referred to the head it gives 0.35, within Taylor's coefficient of 0.3 to 0.4. At 86 mm: 473 mm² of intake against 676, 363 of exhaust against 566. Two large valves leave 30% less section than four small ones, and that ratio barely moves with the bore: **piston speed, and so the stroke, decides** when it starts to matter.

**Filling falls off with gas speed.** Taylor's Mach index (SAE 520259, seen cited) Z = Ap·Sp / (Aeff·a) is 0.53 on a 2 L 8-valve at 5200 rpm, and 0.43 on a 16-valve at 6000 rpm: real engines are sized at about 0.55 or less at peak power. `tools/valve_filling.py` integrates the quasi-steady filling of a cylinder, with no resonance: fill(Z) = (1 + (Z/0.8)⁴)^(−1/4), within 1.8% from Z = 0.2 to 2. Its choked limit, 0.76/Z, agrees with the 0.75/Z of engine courses (seen cited). `breathing` multiplies its shape by the ratio of the two fillings, which touches nothing of the twin cam.

**The exhaust costs at the top.** The same script empties a cylinder opened on 5 bar and 1250 K: too early, you lose expansion; too late, the piston pushes. The lost work goes through a dip, then climbs fast: 0.3 bar at Z = 0.35, 1.2 bar at Z = 0.5 (`kExhaustLossTable`). Referred to 12 bar of indicated mean pressure (assumed), it enters `breathing`.

**Torque comes earlier.** Intake tuning follows the root of section over length and volume (Engelman, from memory): the peak comes down by √(section) = 0.84. Low down, the single cam is therefore ahead, as real engine pairs also say. This shift assumes ports sized to the valves.

**Friction and inertia, modest.** Total spring force stays the same, two valves twice as heavy having a spring twice as strong, but the rocker loads the cam by its ratio and adds its shaft: 50% more on the cam-follower contact, about 0.03 bar at 90 mm of stroke (Patton, Nitschke and Heywood, 1989, from memory: their paper could not be re-read). The bearings of one less camshaft weigh under 0.01 bar (Petroff), neglected. One less camshaft and wheel per bank, at half speed: −0.1% of inertia.

| Engine | Cam | Peak torque | Peak power | 0 to 100 km/h | WLTC |
|---|---|---|---|---|---|
| 2 L, 86 × 86, 6500 rpm, 1200 kg compact | twin | 168.5 Nm at 3900 | 122 hp at 5950 | 9.95 s | 8.40 L/100 |
| | single | 166.9 Nm at 3050 | 108 hp at 5700 | 10.38 s | 8.48 L/100 |
| `hot_hatch_na` | twin | 194.1 Nm at 4500 | 160 hp at 6750 | 8.72 s | 9.07 L/100 |
| | single | 188.8 Nm at 3400 | 131 hp at 6100 | 9.42 s | 9.07 L/100 |
| `inline_six`, high revs (8000) | twin | 335.3 Nm at 4950 | 303 hp at 7400 | 6.48 s | 13.08 L/100 |
| | single | 325.0 Nm at 3650 | 243 hp at 6600 | 6.47 s | 13.24 L/100 |
| `v8_cross_plane` | twin | 428.9 Nm at 3700 | 291 hp at 5550 | 6.95 s | 13.90 L/100 |
| | single | 424.0 Nm at 3000 | 264 hp at 5400 | 6.78 s | 14.13 L/100 |

The 0 to 100 km/h does not always follow power: `gearingOf` shortens the final drive when top speed falls, and the V8 gains 0.17 s that way. A real car in two versions would often keep its final drive.

Against real engines in two versions, at equal displacement:

| | 8 or 12 valves | 16 or 24 valves | Gap |
|---|---|---|---|
| Opel 2.0 L, 86 × 86 (read, revs from a forum) | 115 hp at 5200, 170 Nm at 2600 | 150 hp at 6000, 196 Nm at 4500 | −23%, −13%, peak ×0.58 |
| Mercedes 2.3 L (read, sources disagree on the 8-valve) | 136 hp at 5200, 200 Nm at 3500 | 185 hp at 6200, 235 Nm at 4500 | −26%, −15%, peak ×0.78 |
| BMW 3.4 L and 3.5 L (M30 read, M88 from memory) | 218 hp at 5800, 310 Nm at 4200 | 286 hp at 6500, 340 Nm at 4500 | −24%, −9%, peak ×0.93 |

The model gives −9 to −20% of power, −1 to −3% of torque, and a peak at ×0.74 to ×0.81. **The direction and the high-rev case are right; the gap on a modest-revving 2 L and on peak torque stays smaller than in reality.** These real pairs differ by more than their valves: the 16-valve also has sharper cams, more compression and a higher limiter, which this setting does not touch. No constant was adjusted to match this table.

**The sound.** The blowdown empties through a smaller section: its duration grows with it (τ = V / (Cd·A·a)), ×1.56, and its peak flow falls as much, for the same gas. The flow pushed by the piston does not change. Lost filling towards the limiter adds to this. Measured at held revs, `hot_hatch_na`, outside: −2.7 dB(A) at 900 rpm, −3.1 at 4000, −3.3 at 6000; `v8_cross_plane`: −1.7, −2.3, −2.7. Nothing measured says a real 8-valve is 3 dB quieter: **this is what the calculation gives, and listening will decide.** The lines keep their structure: comb from 13.8 to 13.4 dB, over a lap.

The valvetrain changes voice, and each part is a marked choice. Under a larger head, the seat resonates lower (stiffness in d, mass in d³): 2.85 kHz instead of 3.6. Each closing strikes only one head; the total mass being nearly equal, the impact energy stays the same. Mechanical hiss loses 0.7 dB, the valvetrain being 30% of it (assumed) with half as many cams. Above all, the rocker takes up its lash at each opening: a tick around 2.4 kHz (assumed), with its own generator, living in the idle floor and following cam speed, far more slowly than the block. Its strength (`kRockerShare`) is not derived: it is chosen to stand out under the bonnet at idle without coming out of the car. Relief from 1.8 to 3.2 kHz, in 1 ms windows: under the bonnet from 10.0 to 12.6 dB at 800 rpm, from 9.2 to 13.2 at 1500, from 6.1 to 6.2 at 6000; outside at 800 rpm, from 9.6 to 8.4. **Mechanical noise therefore does not pass in front of the exhaust**, but with a quieter exhaust the 8-16 kHz over 500-2000 Hz ratio rises by 2.4 dB at idle outside (−21.1 to −18.7): to watch when listening. Hydraulic lash adjusters would remove this tick; the model assumes mechanical lash.

**What is not modelled.** The chamber itself: off-centre plug, slightly slower combustion, less advance. Consumption therefore knows nothing of it, and moves only through friction, gearing and load: 0 to +1.7% over the WLTC. Residual gas, more plentiful when the cylinder empties badly at very high revs, does not reduce the next filling. Air noise in the valve stays that of the twin cam. Timing stays the same, and the chain makes no noise of its own, on either.

**The view.** `engineLayout` deduces everything from the valve stem: the rocker is perpendicular to the stem at mid-lift, its pad perpendicular to the cam radius at mid-lift, and the angle is solved exactly at each frame (`rockerAngle`), as it reaches some twenty degrees. The lobe is the lift divided by the rocker ratio (1.4). The camshaft in the middle of the head takes the spark plug's place: it passes beside it, leaning 30° towards the exhaust. The chain goes round only the crankshaft and one wheel. The gas on screen receives the same section ratios, with no allocation and no change to `EVO`, `EVC`, `IVO`, `IVC`.

## The carburetor

`FuelSystem`: `Injection` (the reference, unchanged to the bit), `Carburetor` (one throat and a shared manifold) or `MultiCarb` (one body per cylinder). `core/src/carburetor.cpp`. No effect on a diesel or on an electric motor.

**The venturi.** The throat is sized at peak power: `d = k √(V × N)`, in mm, litres and thousands of rpm. One body per cylinder follows the Weber rule (k = 20, on the volume of one cylinder). A shared throat is k = 10 on the whole displacement, which gives 32 mm on the 2 L at 5500 rpm of a BMW 2002 (Solex 40 PDSI). The loss then follows from the flow: `Δp = (K₁ + K₂ (d/D)⁴) × ½ρv² × F × (1 + M²/4)`, where `F = <v³>/<v>³` is computed by adding the pulses that go through that throat (about 10 for a single cylinder, 1.1 for four). K₁ = 0.9 is assumed, between what Holley's flow ratings give at 1.5 inHg on a four-barrel and at 3 inHg on a two-barrel. The result, 7 kPa at peak power on a 2 L single carburetor and 3 kPa with one body per cylinder, lands in that order of magnitude. This loss takes charge away without taking friction away, and adds to pumping: `breathing` accounts for it at the brake. Nothing is added at low revs: a long manifold helps hold the flow, but I have no figure that justifies a gain over the reference injection, which has tuned runners too.

**The mixture.** The jet targets the leanest cylinder, so the mean gets richer the more unequal the distribution: ± 7% on a shared manifold, ± 3% with one body per cylinder (± 5 to 10% in Heywood, 3% assumed). At part load, λ = 1 − spread; at full load, λ = 0.88 × (1 − spread), 0.82 on a turbo; on the idle circuit, λ = 0.9. The power valve follows vacuum and knows nothing of power, unlike an ECU. On overrun, the idle circuit keeps flowing through its fixed jet: as much as at idle, at any revs. At the limiter, the spark is cut but the carburetor carries on. None of this feeds back into torque in steady state: consumption reads. The accelerator pump, however, counts on top.

**The film.** This is Aquino's x-τ model (1981), integrated at each `DrivingModel` step, in physical time. The fuel column delays the flow by `2L/v`, with v drawn from the throat vacuum by Bernoulli (L = 5 cm assumed): that is the slowness at low revs. A share `x = (1 − e^(−L/0.5)) × 40/(40 + v) × (0.4 + 0.6 × fill)` settles, according to the path travelled, and less at high air speed, where droplets are fine (Nukiyama and Tanasawa). It leaves with `τ = 0.35 s × (50/v)^0.75 × fill`. These values fall within the published hot ranges (x from 0.2 to 0.6, τ from 0.1 to 1 s; Hires and Overington, 1981) without being fitted point by point. What stays airborne waits in the manifold, and only reaches the cylinders with the air that leaves it. The pump gives 0.4 cm³ per litre over one stroke, in 0.25 s, mostly onto the walls, and only if the foot moves quickly: a slow stroke returns to the bowl. Torque is multiplied by `f(actual richness)/f(settled richness)`: flat around 1.15, falling on the lean side, with misfires below λ = 1.6 (after Heywood's curves, chapter 5, read by hand).

**In the sound.** For the hiss, confined turbulence: `W = K ρ A v⁶ / c³` at the throat, an acoustic efficiency of `2K M³` (K = 10⁻³ assumed, after Nelson and Morfey, 1981). Pressure therefore follows v³: almost nothing at low revs, which respects "whatever does not come from the exhaust keeps its place at low revs". Its band follows Strouhal, 0.5 v/d, and its highs follow load like intake noise. With one body per cylinder, each throat follows its own pulse, and the powers add. The airbox keeps 12 dB of this hiss on a single carburetor, 3 dB with small flat filters (assumed). Also with one body per cylinder, the straight runner resonates as a quarter wave, and the induction noise loses its box: 6 dB more, at the bottom of what an airbox removes (10 to 20 dB, assumed). On a shared manifold, cylinders differ according to `f`: ± 3.5% every other firing, following from the spread, and nothing with one body per cylinder. On overrun, the idle mixture still burns about one cycle in two (assumed): pressure at opening falls less, and pops remain possible even without asking for them, all the more with a free exhaust. The tip-in dip can be heard, because `load()` passes it to the sound. Measured on rendered files at 1500 rpm and full load: 8-16 kHz against 500-2000 Hz moves by less than 0.1 dB behind the car.

**Forced induction.** Blow-through, like the Renault 5 Alpine Turbo: the carburetor sits after the compressor, in a pressurised box. Volume flow does not change and ρ follows pressure: the loss stays the same share of the pressure, and the same factor applies to `breathing`. The film follows density at the throat. Not simulated: the fuel pressure to follow, heating in the compressor, a draw-through setup (a Roots with carburetors on top, which would see the loss in its pressure ratio and a wet mixture). `Aspiration::Supercharger` goes through the same `breathing`, so under the same assumption.

**Figures.** `revforge-render --out none --carb … --scenario tipin --rpm 1500`, `--scenario wltc --fuel 1`. A 1200 kg compact, 2 L naturally aspirated, and a 1600 kg coupe, 5 L V8. Dip: largest torque drop due to the mixture, within the first second. Intake: LAeq in front of the car at full throttle, a single indicative figure, never a measurement.

| | Torque | Power | 0-100 | WLTC | CO₂ | First-second torque at 1500 rpm | Dip at 1500 / 4000 | LAeq in front |
|---|---|---|---|---|---|---|---|---|
| 4 cyl., injection | 173.6 Nm | 96.8 kW | 9.51 s | 8.64 L | 204 g | 132.0 Nm | 0 / 0% | 82.2 dB(A) |
| 4 cyl., carburetor | 167.3 Nm | 89.3 kW | 9.91 s | 10.02 L | 237 g | 122.2 Nm | 23.5 / 11.7% | 82.3 dB(A) |
| 4 cyl., multiple | 170.6 Nm | 93.5 kW | 9.77 s | 9.63 L | 227 g | 122.2 Nm | 14.7 / 12.8% | 84.8 dB(A) |
| V8, injection | 401.3 Nm | 197.5 kW | 7.23 s | 13.76 L | 325 g | 288.4 Nm | 0 / 0% | 85.2 dB(A) |
| V8, carburetor | 387.0 Nm | 182.5 kW | 7.34 s | 15.99 L | 378 g | 275.4 Nm | 12.9 / 0.8% | 85.7 dB(A) |
| V8, multiple | 394.4 Nm | 190.7 kW | 7.42 s | 15.38 L | 363 g | 276.2 Nm | 8.2 / 1.0% | 89.8 dB(A) |

Expected power gap: the BMW 2002 with one carburetor, two, and injection are rated about 100, 120 and 130 PS, with compression and cams that also differ. Here the gap comes from the fuel system alone, and so stays smaller. Between a carburetor and closed-loop injection, consumption historically differed by 10 to 20% depending on the era and the tune: an order of magnitude, not a target.

**Simplifications and limits.** The choke and the fast-idle cam are in "Engine temperatures"; the film stays that of a warm engine. No progressive two-barrel, altitude correction, float level, icing, or cornering effect on the bowl. Air cooling by evaporation and manifold heating are assumed to cancel, without computing them. Passing lean and rich phases change combustion efficiency only through `f`. Air in a per-cylinder body does not pulse in the film: only its vacuum does. The gas on screen has only one duct: several bodies are one throat there, which keeps the ratio of each to its runner. It loses only its section changes, without the C++ loss coefficient: 2.4 kPa in the plenum at 6000 rpm on a 2 L single carburetor, 0.3 kPa with one body per cylinder, against 7 and 3 kPa at peak power in the torque. Sources are quoted from memory: Heywood (*Internal Combustion Engine Fundamentals*, 1988, chapters 5 and 7), Aquino (SAE 810494), Hires and Overington (SAE 810495), Nelson and Morfey (*J. Sound Vib.*, 1981), Nukiyama and Tanasawa (1939), Passini's Weber tuning manual for the 20 √(V N) rule, Holley's data sheets. Check them before quoting them elsewhere. Listening remains to be done.

**Where it meets other settings.** The air filter (`withAirFilter`) and the venturi are in series: their pressures add to first order, because their factors multiply and stay small. The venturi hiss goes straight to `Source::Intake`: it does not pass through the filter's response, and so does not follow its placement. With a "stacks" filter and one body per cylinder, the box would be removed twice: 6 dB here, and what the filter already removes. A filter that makes no sense on one body per cylinder is not forbidden.

## The rotary engine

`Layout::Rotary`, a Wankel engine. `cylinders` counts rotors there, `displacementCc` the usual figure: one chamber per rotor, 654 cc on the reference engine. `isRotary(config)` exists in C++ (`engine_config.hpp`) and on the site (`rotary.ts`, re-exported by `config.ts`): settings that make no sense here read it.

**Geometry.** Generating radius R, eccentricity e, width b, with V = 3√3·R·e·b per chamber. R 105, e 15, b 80 mm give 654.7 cc. `rotaryGeometry` derives them from the displacement at the same proportions: no bore, no stroke. The housing is the path of a rotor tip, H(u) = R·(sin u, cos u) − e·(sin 3u, cos 3u). A 30-tooth ring gear, pitch radius 3e, rolls on a fixed 20-tooth gear, pitch radius 2e: the rotor turns at a third of the shaft. `rotatif.spec.ts` checks on the drawing that the volume measured between flank and housing gives the displacement, and follows V₀ + V/2·(1 − cos(2A/3)).

**Cycle: the one thing that changes the sums.** Each chamber does its four strokes in three shaft turns, and each rotor has three chambers: one firing per rotor per shaft turn, where a cylinder gives one for two turns. A single quantity carries this, `sweptCcPerTwoTurns`: the volume swept in two turns is twice the displacement. Torque (`bmepToNm`, so p·V/2π), air flow, turbo (`displacementM3`), pumping, throttle and consumption follow with no other coefficient. A 1.3 L twin-rotor therefore breathes like a 2.6 L four-stroke.

**Efficiency: the cause, not a target.** `rotaryEfficiencyShare` multiplies three effects. The compression ratio, 9.4 (8.5 turbocharged), through the ideal cycle. The long chamber: `rotaryWallRatio` compares its surface at top dead centre, a long blade, with a square cylinder of the same volume, and finds about 1.8. At 12.5% of efficiency per unit, an order of magnitude from the 10 to 15% of fuel energy that goes to the walls (Heywood), this removes 10%. Tip leakage, 5%, is assumed. That gives 0.83, applied to the mean pressure and to the efficiency of `ConsumptionModel`. Friction, referred to swept volume, is 75% of a piston engine's, with no valvetrain or rods: assumed.

**Oil.** A rotary burns it on purpose, metered to lubricate its seals: about 8 g/h per litre and per 1000 rpm, on the order of 25 mL/h at 2000 rpm. The figure comes from memory of a 13B metering pump, **not found again on a manufacturer's sheet**; like all oil here, it holds within a factor of three. Neither wear nor apex seals are simulated: they exist only through this leakage and this oil.

**Starting.** Three chambers per rotor over a three-turn cycle, at 2/3 of the lever arm (`rotaryCompressionLoadNm`). Gas leaks past the tips at low speed: n = 0.96 gives 8.6 bar at top dead centre, close to the workshop value of the 13B-MSP ([830 kPa at 250 rpm, Mazda table](https://www.foxed.ca/rx7manual/2003mazdarx8/esicont/en/srvc/html/BHE010002000T04.html)), against 13 to 14 on a piston engine. The table gives this at a ratio of 10.0; it is applied at 9.4, the difference neglected. Starter and flywheel are sized on swept volume: the former compresses as much per turn as a 2.6 L, the latter takes the same firings.

**Sound.** `firing` remains a two-turn cycle: two firings per rotor, 360°/rotors of shaft apart. The profile's `cylinders` and `firingCount` are thus twice the rotors, and `boreM`, `strokeM` give a square cylinder of one chamber's volume: everything that follows flow, exhaust line included, comes out right. What comes next is specific to the rotary:

- Ports (`kPortEvo`…): opening at 195° of shaft after top dead centre, closing at 588°, intake from 572° to 850°, over the 1080° cycle. **This timing is that of a peripheral-port 13B, from memory, and was not cross-checked against a manufacturer's manual**; a side-exhaust Renesis differs noticeably.
- Only one port width makes this timing possible: the leading tip opens it, the trailing tip closes it 120° of rotor later, so (588 − 195 − 360)/3 = 11° of rotor, 33° of shaft. It opens over 4° of shaft, the thickness of the seal (assumed), instead of a cam's 15°.
- The blowdown (`blowdownS`): volume at opening over port area, 14 cm² on the reference (assumed, within 30%), scaled from the 2 ms of the reference cylinder: 1.6 ms. Gas is released at 15% more pressure, from a shorter expansion.
- The rotor pushes out the same volume over 393° instead of 240°: a more continuous flow.
- No valvetrain noise: no valve lands. Block at −5 dB (40 bar peak pressure rather than 70, **order of magnitude, not cross-checked**), idle floor at −3 dB, less shaft ripple (assumed).
- **One memory per chamber, not per firing.** Residual gas passes from one cycle to the next in the same chamber, so every three turns, where a cylinder meets it again every two. This is where the third orders come from, and why the half orders fade, with no gain added to get them. A fixed difference between a rotor's three faces was tried, then removed: it changed nothing measurable, and the figure would have been invented.

Orders measured at 4000 rpm, stock line, held speed (`tools/analyze_orders.py`, in dB below the strongest order): both engines share orders 2 and 4, those of the firing rate, but not the sub-orders.

| Order | 1 | 5/3 | 2 | 5/2 | 3 | 4 | 6 | mean halves | mean thirds |
|---|---|---|---|---|---|---|---|---|---|
| Twin-rotor 1.3 L | −25.7 | −29.8 | 0 | −38.5 | −30.1 | −1.3 | −24.9 | −44.0 | −39.4 |
| 4-cylinder 2.15 L | −21.2 | −38.7 | −2.9 | −25.7 | −28.2 | 0 | −22.5 | −34.8 | −41.2 |

A 360° twin, measured the same way, has its strongest order at 1. `tests/rotary_test.cpp` protects thirds and halves.

**Against a 4-cylinder of the same power** (same 1300 kg coupé, 9000 rpm, stock line):

| | Twin-rotor 1.3 L | 4-cylinder 2.15 L |
|---|---|---|
| Torque | 227 Nm at 5400 rpm | 225 Nm at 5400 rpm |
| Power | 167 kW at 8250 rpm | 166 kW at 8150 rpm |
| 0-100 km/h | 7.1 s | 7.2 s |
| WLTC | 13.35 L/100 km | 10.94 L/100 km |
| Best point of the map | 287 g/kWh | 241 g/kWh |
| Oil, WLTC | 707 mL/1000 km | 103 mL/1000 km |

The match in torque and power is a choice of the 4-cylinder's displacement, for the comparison. The twin-rotor itself was not tuned: its 167 kW and 227 Nm are at −2% and +8% of the 170 kW and 211 Nm of the 13B-MSP of the same displacement (figures from memory), a gap left as it is. On a sport line it reaches 181 kW and 242 Nm. The preset's stock line is also the one that holds the "grain" check of `engine_sound_test.cpp`: on the sport line, very strong in the bass, that ratio falls to −41 dB, under the −40 threshold, within the −47 to −3 dB range of measured cars.

**What stays simplified.** No wear, no seals, no low-speed "ring-a-ding", no variable ports: an intake with a single timing. The turbo's exhaust temperature follows the piston law, although a rotary runs hotter. In `EngineView`, what connects to its ports (turbo, supercharger, carburetors, filter) is an outline, without animated wheels: the full chain stays on the gas view and on the car. The gas on screen (`GasNetwork`) truly computes three chambers per rotor, over 1080°, with one runner per rotor: their volume is written like a piston with an infinite rod, in cycle degrees.

## When settings meet

Each setting has its section. Here, only what exists when they are combined, with its measurements. The tests are in `tests/jonctions_test.cpp` and `web/tests/jonctions.spec.ts`.

**Filter and supercharger.** Air goes through the filter before the supercharger: `airInletBehind` gives its inlet pressure and temperature, `filterDrop` follows the filter, and the supercharger stays the one sized on stock paper. The lost density comes from the supercharger's own computation, through the charge cooler's outlet temperature (`coolK`), which already enters `superchargerAirShare`. `turboChargeDensityShare` is therefore not applied on top: it would count the same heat twice. It serves as a cross-check, within 1 % in the test. The gas on screen already read `AIR_FILTER_GAS` through `CHARGER_INTAKE`.

| 6.2 L V8, Roots, 0.7 bar | Paper | Panel | Cone | Stacks |
|---|---:|---:|---:|---:|
| Drop at 88 % of redline | 2.89 kPa | 2.46 kPa | 1.14 kPa | 0.19 kPa |
| Inlet air | 20 °C | 20 °C | 32 °C | 32 °C |
| After the cooler | 40.1 °C | 39.9 °C | 43.3 °C | 42.9 °C |
| Drive | 76.2 Nm | 75.8 Nm | 74.5 Nm | 73.7 Nm |
| Power | 393.6 kW | 394.1 kW | 390.1 kW | 391.2 kW |

On a supercharger, a cone thus loses 1 %: it breathes better, but hotter air, which is then compressed. Stacks tune nothing here: the wave of `stackRamGain` doesn't reach the valve through the supercharger.

**Carburetor and filter.** The factors in `breathing` multiply: they are two losses in series. In the sound, the airbox is removed only once: that of one carb per cylinder (`kCarbOpenInlet`) gives way to the computed response of a cone or stacks, which already contains it. Stacks and multiple carbs at 3000 rpm, in front of the car, from 100 to 800 Hz: +4.3 dB against injection before this fix, +1.0 dB after. On stock paper, nothing changes, to the bit.

The throat's hiss depends on the layout, and where the throat sits decides. A single throat is behind the filter: its hiss climbs the same path as the induction, so through the filter's response, and comes out where that does. Each body of a carb per cylinder has its own mouth: its hiss has only air ahead of it, and there is no response to cross, only the place where those mouths open. Stock paper remains the calibrated path.

**Carburetor and boost.** `Supercharger` and `TurboCharger` receive `breathing`, throat loss included: torque counts it once. Share of torque kept on the V8 at 5000 rpm, single throat: 0.926 naturally aspirated, 0.926 with a turbo, 0.920 with a supercharger, whose drive does not fall with it. Counted twice, it would be 0.857. Lower down, the turbo hides it under its ECU plateau. The supercharger gets the throat density and full-load mixture of the turbo, which it lacked.

**Carburetor and anti-lag.** A carburetor doesn't meter on demand: retard and air injection can no longer ask for extra fuel, and the only surplus is the mixture it already delivers (`carbRichness`), already counted by `fuelGps`. On 2 L at 1.3 bar, pedal lifted, surplus of 0.20 g/s with injection and 0.13 g/s with a carb on retard, 1.05 and 0.12 g/s on air injection: the latter has almost nothing left to burn in the manifold, which is the cause and not a penalty. The cut asks for none. Anti-lag requires `!cut_`, and having no fuel cut changes nothing there: the idle circuit stays a floor, which doesn't add. In the sound, the opening pressure of the mixture still burning is written before `evoRatio`. The gap is only 0.3 dB: no test can hold it.

**Anti-lag and pipe.** Anti-lag heat enters `stepHeat` like any other. What arrives unburnt ignites at the collector, not at the turbine: `collectorK`. On 2 L at 1.3 bar, 3 s pedal lifted in fourth:

| Pipe | Off | Retard | Air | Cut |
|---|---:|---:|---:|---:|
| 400 mm, shaft at 3 s | 21,700 rpm | 93,700 | 104,200 | 95,800 |
| 400 mm, catch | 0.97 s | 0.53 s | 0.51 s | 0.54 s |
| 3000 mm, shaft at 3 s | 21,900 rpm | 57,700 | 76,400 | 59,800 |
| 3000 mm, catch | 2.18 s | 1.41 s | 1.26 s | 1.50 s |
| Peak T3, 400 then 3000 mm | 813, 435 °C | 1072, 455 °C | 1030, 485 °C | 927, 442 °C |

Still useful, less so on a long pipe: the wall takes its share. Air injection at 400 mm starts to surge after a second in this fourth-gear run: the shaft falls from 149,000 to 104,000 rpm. That was already so before merging the branches.

In passing, the short pipe inverted the wall law up to 2051 °C with no flow, pedal lifted: the inversion stops at the flow of a sustained idle, 20 g/s on 2 L.

**Rotary.** `valvetrain()` and `effectiveBoreMm()` state what it lacks: camshaft and bore have no effect there, to the bit in the sound. `strokeMm()` gives the square cylinder of its chamber, and `--stroke` is ignored with a warning. Supercharger, carburetor and pipe go through `sweptCcPerTwoTurns`: the supercharger of a 1.3 L twin-rotor moves as much as that of a 2.6 L piston, within 10 %. A carb body sees two pulses per cycle. Any boost takes the turbo's compression ratio.

The F C C F C cut pattern keeps its meaning on two rotors, and stays as it is. Five is odd: it moves from one rotor to the other, as from one cylinder to the next on a 4 or a 6, and each rotor fires 8 events out of 20. At 3600 rpm, the pattern's lines gain 11 dB over a smooth engine. Nobody has listened, and no real build was consulted: this is a deduction, not a measurement.

**Views.** The chosen filter shows on a supercharger as on a turbo. A blown carburetor has no second filter. The rotary shows in outline what connects to its ports, without animated wheels.

## Calibration

The tuning is compared to ten car recordings from Wikimedia Commons ("engine and exhaust sound" series, CC BY 3.0: Shelby Cobra, Mustang GT, Shelby GT500,
Lexus RC F, Honda S2000, Mazda MX-5, Porsche 911 GT3 RS, Ferrari F12, Audi S1, Alfa Romeo GTV6). They were used for measurement only and are not in this
repository. `tools/analyze_sound.py` reproduces these measurements and plots the spectrograms.

Averages over the loudest windows, in dB per octave. The simulation is measured on six presets, with the `lap` scenario:

| | 63 Hz | 125 | 250 | 500 | 1 k | 2 k | 4 k | 8 k |
|---|---|---|---|---|---|---|---|---|
| Real | −11 | −9 | −11 | −14 | −19 | −24 | −30 | −42 |
| Previous version | −13 | −5 | −10 | −14 | −18 | −22 | −27 | −32 |
| Simulation, before the acoustics rework | −10 | −4 | −9 | −15 | −23 | −28 | −31 | −37 |

These averages change little from one version to the next, even though the sound has changed a lot: the old version already had the right average spectrum,
and yet sounded like a zipper. The defect is temporal, and shows up on a spectrogram of a run-up. A test protects against it (« les raies s'effacent progressivement vers les aigus, sans disparaître »).

These measurements say whether the sound is plausible, not whether it is pleasing: only listening decides. In this table, only 8 kHz stays above the real level: a darker sound had been judged muffled. Known limits: the lows stay louder than real around 125 Hz, and the gusts are less marked than on the rawest recordings,
such as the Cobra.

## Reproducibility

The render is bit-identical between native and WebAssembly: calculations in `double` rounded to `float`, frozen order of random draws, no floating-point
contraction, fixed-step physics. Parity with the original Kotlin version (CANCitroen) was abandoned during this rework. `tools/compare_wav.py` compares two renders.

## The architectures: VR, W, V14 and V16

An engine is described by its firing order and by the place of each number: its cylinder head, its row in that head, its rank along the crankshaft. Everything else follows (`core/src/architecture.cpp`, copied in `web/src/lib/architecture.ts`): when each cylinder fires, which exhaust line it blows into, the angle of each crankpin. The earlier engines (inline, V, cross-plane V8, boxer) have their row there, a copy of `FIRING_ORDERS` and of the former `placementOf`, but their sound does not go through it: it stays bit-exact. Only VR, W and engines above twelve cylinders use it (`ownArchitecture`).

### The model

- **Firings.** With even intervals, rank s fires at 720·s/N degrees. On crankpins shared at a V angle that does not divide the cycle (the V14), the left bank fires every 720/(N/2) degrees and the right cylinder on the same pin fires the V angle later: the intervals follow, uneven.
- **Cylinder offset.** On a VR and a W, each row's bore axis passes e = 12.5 mm from the crankshaft, towards the outside of its head (VW, SSP 248). Top dead centre comes when crankpin, gudgeon pin and crankshaft line up, at asin(e/(l+r)) from the row axis: the piston travel is r·cos a + √(l² − (r·sin a − e)²). A cylinder's crankpin therefore sits at φ = β + asin(e/(l+r)) − θ, θ being its firing angle.
- **Crankpins.** On a VR6, the pairs an inline six shares (1/6, 2/5, 3/4) are split by 15° + 2·asin(12.5/(164 + 45.15)) = 21.85°; SSP 248 draws 21.833°. On a W, the two cylinders of one throw are split by the head angle minus their firing gap: +12° on the W12 (72 − 60), −18° on the W8 (72 − 90), the SSP 248 "splitpins". The W16 and the V16 come out on shared pins (90 − 90, 45 − 45). The drawing uses a rod of 1.6 strokes: 22.6° on the drawn VR6.
- **Sound.** `applyArchitecture` (`engine_config.cpp`) gives each firing its instant (uneven on the V14), the head it blows into (`bank`, hence line A or B when there are two) and the length of its runner: 0.62 m on a single head, 0.55 and 0.72 on two, as inline and V. The row far from the exhaust also crosses the head by the gap between both rows at the deck, 2·(H·sin(α/2) + e·cos(α/2)) with H = 2.1·stroke + 0.36·bore: 57 mm more on the VR6. No gain is tuned for the unevenness: it comes from the firing order and the split between lines. A VR has one head, hence one line, whatever outlets are chosen.
- **Starter.** `compressionLoadNm` puts the compressions at the real firing angles.
- **Views and gas.** The drawing takes placement, angles and crankpins from the architecture; a VR has one throw per cylinder, staggered (seven main bearings for six), a W two rods per throw. The gas links each runner to its own head's collector, and pulses leave at their angle. On the V10, the V12 and the flat-plane V8, which so far blew by the parity of the number (a V12's odd cylinders fired six times in a row into the same collector), each cylinder now blows into the head it is drawn in.

### The engines, and their sources

| Engine | Angles | Order | Intervals | Per head | Source |
|---|---|---|---|---|---|
| VR5 | rows 15°, e 12.5 mm | 1-2-4-5-3 | 144° | one head | Wikipedia (VR5), Motor1 |
| VR6 | rows 15°, e 12.5 mm | 1-5-3-6-2-4 | 120° | one head | Wikipedia (VR6), SSP 248 |
| W8 | heads 72°, rows 15° | 1-5-4-8-6-3-7-2 | 90° | 180, 270, 180, 90° | SSP 248 |
| W12 | heads 72°, rows 15° | 1-12-5-8-3-10-6-7-2-11-4-9 | 60° | 120° | SSP 248 |
| W16 | heads 90°, rows 15° | 1-14-9-4-7-12-15-6-13-8-3-16-11-2-5-10 | 45° | 135, 45, 135, 90, 45, 135, 45, 90° | Bugatti ("asymmetric firing order", 45°); the order from a 2006 press release quoted by a wiki |
| V14 | 45° | 1-8-2-9-4-11-6-13-7-14-5-12-3-10 | 45 and 57.86° | 102.86° | computed, below |
| V16 | 45° | 1-9-6-14-2-10-5-13-8-16-3-11-7-15-4-12 | 45° | 90° | Cadillac 452: two straight-eights at 45° on one crankshaft |

What is assumed:
- the numbers alternate from one row to the other along the crankshaft (zigzag), and a W's go from the left head to the right, as SSP 248 does for the W12; no source says so for the VR6 or the W16. With this numbering, the two cylinders of each W16 throw fire 90° apart, the head angle: shared crankpins, which makes it plausible;
- the V16: Cadillac's order was not found. Each bank takes a straight-eight's (1-6-2-5-8-3-7-4), the right bank 45° after the left. The Cizeta V16T, two 90° flat-plane V8s in one block, would make each head blow in pairs; it is not modelled;
- the V14: no production car ever had one. The only V14 found is a marine or power-plant diesel (MAN 14V32/44CR, 45° V crankcase, second-hand source), whose order is not published. Seven crankpins are taken as a star, 720/7 degrees of firing apart, and the left bank's order is the one that best cancels the rocking couples Σ xᵢ·exp(j·k·φᵢ) for k = 1 and 2 (xᵢ the pin's place, φᵢ its angle), out of the 720 possible orders: 1-2-4-6-7-5-3, M₁ = 0.27 and M₂ = 1.01 (in r·m·pitch), against 4.48 and 3.59 for 1-2-3-4-5-6-7.

### What we measure

Shaft orders at a held speed (`tools/analyze_orders.py`, dB below the strongest, same displacement, two lines except the VR6):

| | Dominant order | Half orders |
|---|---|---|
| VR6, 4000 rpm | 3 (0) | 5/2 at −25, 7/2 at −27, 11/2 at −28 |
| Inline six, same dimensions | 3 (0) | 5/2 at −33, 7/2 at −38, 11/2 at −37 |
| W16, 3000 rpm | 5/2 (0), 8 at −3.6 | average −13.6 |
| V16 | 4 (0), 8 at −4.8 | average −25.5 |
| W8 | 4 (0) | average −24.9 |
| Flat-plane V8 | 4 (0) | average −32.8 |
| W12 | 3 (−1.8) | average −26.3 |
| V12 | 3 (−2.0) | average −32.2 |
| V14 at 45° | 7 (0) | average −28.4 |

- The VR6 keeps a six's order 3, but its rows fire three by three (1, 5, 3 then 6, 2, 4) and the far row has the longer runner: its half orders rise 8 to 11 dB over the inline six.
- The W16 and the W8 let each head be heard at its uneven rhythm: half orders 8 to 12 dB stronger than a V of the same count. Bugatti describes an engine "balanced and comfortable in the lower load range" that "increasingly becomes a growling beast"; no published analysis in shaft orders was found to confirm it.
- The W12 sounds like a V12: the same rhythm per head.
- On the V14, a test (`architecture_test.cpp`) isolates the instants: with equal strengths and runners, order 3.5 is 9.7 dB stronger than with even intervals.

The presets, against published figures; nothing is tuned to match:

| | Simulated torque | Published | Simulated power | Published | Simulated 0-100 | Published |
|---|---|---|---|---|---|---|
| VR6 2.8 (Golf III) | 233 Nm at 3050 | 235 Nm at 4200 | 110 kW at 5650 | 128 kW at 5800 | 9.1 s | 7.8 s |
| Twin-turbo W12 (Continental GT 2003) | 778 Nm | 650 Nm from 1600 to 6100 | 406 kW at 5950 | 412 kW at 6100 | 5.5 s | 4.8 s |
| W16 (Veyron 16.4) | 1306 Nm | 1250 Nm | 671 kW at 5900 | 736 kW at 6000 | 3.8 s | 2.5 s |
| V16 (Cadillac 452, 1930) | 578 Nm | 367 to 432 Nm | 179 kW at 3700 | 123 to 138 kW | 10.1 s | — |

- The VR6 breathes less well than the real one, with its two valves per cylinder (`camshaft: sohc`, the 12-valve).
- The W12 and the W16 have one turbo in the simulator until the twin-turbo work lands: `turboLayout: twin_parallel` is already written, without effect.
- The Cadillac's compression was 5.35 (Old Cars Weekly), below the range of the engine settings (6 to 24): its torque is that of a modern engine of the same displacement.
- Preset sources: auto-motor-und-sport and auto-data for the Golf VR6 (ratios 3.30, 1.94, 1.31, 1.03, 0.84, final 3.68, 1285 kg); supercars.net (ZF 6HP26 from 4.17 to 0.69, final 3.52, 2385 kg) and australiancar.reviews (0.7 bar) for the Continental GT; Bugatti, auto-motor-und-sport (Super Sport ratios) and conceptcarz (about 18 psi) for the Veyron; its published final drive (3.64) would give 306 km/h at the limiter in seventh, far from 407 km/h, so the gearbox computes it; Wikipedia and Old Cars Weekly for the Cadillac (3 × 4 in, final 4.39, 2400 to 3000 kg).
- The redlines of the VR6 (6500), the W12 (6500), the W16 (6400) and the Cadillac (4000, peak power being at 3400) are assumed. At 3800, the automatic shift modes of `shift_modes_test.cpp` lost their hysteresis on this three-speed box.

### Sixteen cylinders

`kMaxCylinders` goes from 12 to 16; rotors stay capped at six (`kMaxRotors`). The WASM reaches 1,255,492 bytes instead of 1,249,204, 6 kB more out of a fixed 4 MB. Rendering sixteen cylinders does not allocate (test) and computes 4.1 s of sound in 0.4 to 0.7 s natively. The on-screen gas takes 3.9 ms per frame on the W16, 3.7 on the V16, 3.4 on a V12, measured under the load of other builds; on a saturated machine, 6.8, 8.3 and 4.3 ms: beyond its budget, the gas lowers its own quality. The site's displacement goes up to 8 L: the W16 is 7993 cc.

### Limits

- A VR or W head is drawn wide, over both its rows, but the valves follow each cylinder's axis and the camshafts the head's: without the finger followers that connect them, the drawing is approximate. The flat head deck and the angled piston crowns are not drawn.
- Friction does not depend on the architecture: a VR6's seven main bearings cost the same as a V6's four.
- One line per head: a Veyron's four turbos, two per head, each on four cylinders, are not modelled.
- Listening is still to be done, especially the W16 and the VR6 against real recordings.

## Ambient air

Two settings, the air temperature (`ambientC`, −30 to 50 °C) and the altitude (`altitudeM`, 0 to 4500 m), give the day's air:
`ambientAirOf` (`core/include/revforge/atmosphere.hpp`), copied for the on-screen gas by `web/src/lib/atmosphere.ts`.

### The calculation

- Pressure of the standard atmosphere (ISA, ICAO), in the troposphere: p = 101,325·(1 − 2.25577·10⁻⁵·h)^5.25588. 89,875 Pa at
  1000 m, 79,495 at 2000 m, 70,109 at 3000 m, 57,728 at 4500 m, within 1 Pa of the published table. The temperature does not
  follow the 6.5 K/km lapse: the day's temperature is set separately.
- Temperature in the model's kelvin: T = 293 + (θ − 20). Every engine calculation assumed 293 K at 20 °C; the 0.15 K gap with
  293.15 changes density by 0.05%.
- Ideal gas, ρ = p/(R·T) with R = 287; speed of sound c = 343·√(T/293), so 312 m/s at −30 °C and 360 at 50 °C (331.3 and
  343.2 published at 0 and 20 °C).
- ISO 9613-1 absorption (`airAbsorptionDbPerM`, already there) at the day's temperature and pressure, and at a reference
  relative humidity of **50%**, that of pass-by measurements: humidity is not a setting. The test also checks it against
  table 2 of ISO 9613-2 at 15 °C (20, 50 and 80%) and at 30 °C.

**In the reference air, 20 °C and 0 m, nothing changes, bit for bit.** Each use receives its former constant, not a ratio that
would round differently: p is exactly 101,325 (1^5.25588 = 1 in `portable::pow`), T exactly 293, and δ = p/p₀, θ = T/T₀,
σ = δ/θ and √θ are exactly 1. The constants that differed keep their own values: 1.2 kg/m³ for drag, 1.204 for sound, 343 and
340 m/s. Away from the reference, each becomes constant × σ (or × √θ, × δ): the physical calculation, which gives the constant
back at 20 °C and 0 m. The four `altitude_*` and `ambient_*` fingerprints protect the rest.

### What is sized once and for all

The turbo, the supercharger, the gearbox and its final drive, the clutch capacity, the torque converter and the shift map are
parts and a calibration: they are computed on the same build brought back to the reference air (`withReferenceAir`). Driving
up to 3000 m changes neither the wheel diameter, nor the shaft inertia, nor the ratios.

### Filling

Indicated torque follows the trapped air; friction stays (T_brake = (T + f)·s − f). The trapped air is heated by the port and
chamber walls before the intake closes:

  T_trapped = T_air + ε·(T_wall − T_air), s = (p / p_ref)·T_trapped(T_ref)/T_trapped(T_air)

with the walls at the warm engine's coolant, 90 °C (`kWarmWallK`, to be read from the "thermal" topic). Warmer air takes less
from the walls: that is why filling follows T^−0.5 rather than T^−1, as tests measure (Taylor) and as SAE J1349 corrects.
ε = 0.45 is chosen so that the local exponent is 0.5 at 20 °C; the air gains 31 K, the order of the charge heating by walls
and residual gas (assumed). An under-bonnet filter adds its own heating to any day's air.

- **Turbo** (`turboAmbientChargeShare`): the spring wastegate holds a **gauge** pressure, since the other side of its
  diaphragm sees the atmosphere. At altitude, the charge is therefore the atmosphere plus that pressure; the pressure ratio
  rises, the air leaves the compressor hotter, and the intercooler brings it back towards the day's air.
- **Supercharger** (`superchargerAmbientChargeShare`): it displaces a volume; at altitude, the same speed moves less mass, so
  less gauge pressure, and the trapped air is computed the same way.
- **Carburettor**: its jets, chosen in the reference air, flow as √(ρ_fuel·Δp) and the air as √(ρ_air·Δp): the mixture
  richens as 1/√σ, ×1.20 at 3000 m. Torque also loses what this over-rich mixture costs (`torqueOfRichness`), and fuel
  consumption pays for it. Its pressure drop counts as a share of the day's atmosphere.
- **Diesel**: treated like the rest, torque in proportion to the air (smoke limit). A mechanical pump without an altitude
  compensator would smoke rather than lose that torque: not modelled.

Against SAE J1349 (2004, spark ignition: CF = 1.18·(99/p)·√((T+273)/298) − 0.18), peak torque relative to 20 °C and 0 m:

| | 1000 m | 2000 m | 3000 m | −30 °C | 0 °C | 35 °C | 50 °C |
|---|---|---|---|---|---|---|---|
| SAE J1349 | 0.869 | 0.754 | 0.654 | 1.119 | 1.043 | 0.971 | 0.944 |
| 2.0 naturally aspirated (`hot_hatch_na`) | 0.868 | 0.749 | 0.642 | 1.108 | 1.041 | 0.971 | 0.943 |
| its power | 0.854 | 0.724 | 0.607 | 1.120 | 1.045 | 0.968 | 0.938 |
| 2.0 turbo (`hot_hatch_turbo`) | 0.943 | 0.891 | 0.843 | 1.106 | 1.040 | 0.972 | 0.945 |
| supercharged V8 (`v8_supercharged`) | 0.903 | 0.793 | 0.692 | 1.110 | 1.042 | 0.970 | 0.942 |

The naturally aspirated engine matches J1349 at 1000 and 2000 m; at 3000 m, outside the standard's range (about 90 to
105 kPa), friction weighs more than its fixed 15%, especially at high revs, so power drops more than torque. The turbo loses
half as much: the gauge pressure comes back.

### The turbo at altitude

At 4000 rpm, full load, 2.0 turbo at 1.3 bar: pressure ratio 2.33 at sea level, 2.49 at 1000 m, 2.69 at 2000 m, 2.91 at
3000 m; the shaft goes from 160,700 to 168,500, 177,100 then 186,400 rpm. At 5500 rpm and 3000 m, the wheel is at 99.8% of its
speed limit (`tipLimit`); at 4500 m, the ECU opens the wastegate to protect it, and the requested pressure is no longer held
(82%). This is also what pulls the diesel's peak power back from 4150 to 2800 rpm at 3000 m.

Lag, engine speed held, from 20% to 100% fill, up to 90% of the requested pressure: 1.21 s at sea level, 1.41 s at 1000 m,
1.66 s at 2000 m, 1.96 s at 3000 m (at 2500 rpm); 0.59, 0.67, 0.77 and 0.89 s at 4000 rpm. In fourth from 1800 rpm
(`--scenario turbo`): 2.21, 2.62, 3.12 and 3.72 s. The shaft must spin higher, and the engine sends it less gas.

Electronic control aiming at the same **absolute** pressure as at sea level ("boost" topic, which reads
`TurboSpec::ambientPa`) would ask even more: ratio 3.38 at 3000 m and 4000 rpm, the wheel at its limit from 2000 m at the top of
the rev range and from 3000 m at 4000 rpm, and 2.17 s of lag instead of 1.96 at 2500 rpm. These figures are computed by
replacing `targetBar` with the absolute pressure minus the day's atmosphere; none of this is wired in.

### Road and consumption

Drag follows σ, a trailer's too: at 130 km/h, 715 N at sea level, 597 N at 2000 m, 547 N at 3000 m. Pumping behind a closed
throttle and engine braking follow δ: the manifold empties under the day's atmosphere. The starter cranks more easily: each
compression starts from a lower atmosphere (trough from 175 to 195 rpm on the diesel at 3000 m).

| | 0-100 km/h | Top speed | WLTC | 90 km/h |
|---|---|---|---|---|
| 2.0 NA, 0 / 1000 / 2000 / 3000 m | 8.72 / 9.62 / 10.35 / 11.87 s | 224.5 / 220.5 / 216.5 / 212.0 km/h | | |
| 2.0 turbo, 0 / 1000 / 2000 / 3000 m | 6.86 / 7.38 / 7.96 / 8.65 s | 260.5 / 264.5 / 268.5 / 260.0 km/h | | |
| City car 1.4, 0 m / 2000 m | | | 6.76 / 6.13 L/100 km | 5.57 / 4.84 L/100 km |
| City car 1.4, −10 °C / 40 °C | | | 6.98 / 6.63 L/100 km | 5.84 / 5.42 L/100 km |
| Electric at 130 km/h, 0 m / 2000 m / −10 °C / 40 °C | | 208 km/h everywhere (motor speed) | | 26.0 / 22.1 / 28.1 / 24.9 kWh/100 km |

The turbo is faster at the top at 2000 m than at sea level: it loses 13% of its power, the air 22% of its drag. In deep cold,
the naturally aspirated engine gains 12% power but loses top speed: drag has gained 21%. The cold start of the engine itself
("thermal" topic) is not counted here: at −10 °C, only the air and drag change.

### Sound

- **Propagation**: the speed of sound sets the delays to the ear, the tailpipe mouth (ka), diffraction around the body, the
  jet effect, the cabin modes and the intake response. At 30 m, sound arrives 1.098 times later at −30 °C.
- **Absorption**: the day's ISO 9613-1. At 8 kHz: 10.5 dB/100 m at 20 °C, 14.8 at 0 °C, 1.2 at −30 °C (very cold air holds
  little water), 7.4 at 50 °C.
- **Radiation**: a flow radiates as ρ·Q̇/4πr, a wall as ρc·v. At equal temperature both are σ; from one temperature of the
  setting to another, they differ by √θ only, 0.4 dB at most. All sources at 1 m are therefore multiplied by σ, once, before
  the field and the cabin; the engine mounts, which do not go through the air, are not. At 3000 m the estimated level drops by
  3.2 dB (66.4 → 63.2 dB(A) at the test speed); `toneGain` gives part of it back at the output.
- **Tuning**: velocity stacks, cut in the reference air, resonate higher in warm air; their filling gain shifts with it. The
  Mach index of single-cam valves uses the day's speed of sound.
- **What does not change**: the exhaust line (`ExhaustLine`) keeps its hot gas (500 m/s, 0.6 kg/m³): its temperature comes from
  combustion, not from the day's air.

Listening is still to be done: a render at 3000 m should sound like the same engine, a little softer and slower to pick up
revs, not like another engine.

### On-screen gas

`GasNetwork` opens onto the day's air (`pAtm`, `tAmb`): the line ends at that pressure, the intake swallows that air, the turbo
wastegate holds its pressure above it, and the plume rises with the difference to its temperature. The boost gauges
(`rf_turbo_*`) read above the day's atmosphere, like a real gauge. The car panel shows the resulting pressure, density and
speed of sound.

### Limits and assumed values

- ε = 0.45 (wall heating) is fitted to J1349's exponent at 20 °C, not measured on this engine.
- Humidity stays at 50%: it changes neither density (dry air) nor filling, only absorption.
- A filter's loss follows ṁ² without density: at altitude it drops as σ² instead of σ.
- A turbo-blown carburettor takes the day's σ, without the charge pressure.
- The assumed levels of supercharger whine and aerodynamic noise follow σ like the rest, without measurement.
- Neither engine cooling, knock nor the ECU read the day's air yet: "thermal", "combustion", "ECU", "boost" topics. They
  find `ambientAirOf`, `trappedAirK` and `TurboSpec::ambientPa` here.

## Engine temperatures

Engine temperatures come from a network of four heat capacities (`ThermalNetwork`, `core/src/thermal.cpp`), integrated by `DrivingModel` every 10 ms: the metal (head, liners, upper block), the coolant on the engine side, the coolant in the radiator, and the oil with what it wets (crankshaft, rods, sump, lower block).

```
C_m dT_m/dt = Q_walls + (1 − a)·P_f − G_mc·(T_m − T_c) − G_mo·(T_m − T_o) − G_ma·(T_m − T_air)
C_c dT_c/dt = G_mc·(T_m − T_c) − ṁc·(T_c − T_r)
C_r dT_r/dt = ṁc·(T_c − T_r) − Q_radiator
C_o dT_o/dt = a·P_f + G_mo·(T_m − T_o) − G_oa·(T_o − T_air)
```

- **Heat to the walls.** Q_walls = s·P_fuel, with s = 0.28·(φ/0.25)^−0.25·(T_g − T_m)/(T_g − 100 °C), where φ is the fuel flow relative to that at peak power. The scaling law is Taylor and Toong's (Nu ∝ Re^0.75, so Q ∝ ṁ^0.75); the level is set on Heywood's energy balance (*Internal Combustion Engine Fundamentals*, 17 to 26% to the coolant at full load on a petrol engine, from memory). s is 0.20 at full load and up to 0.5 at idle; a cold wall takes a little more (mean gas at 1100 °C, assumed). The fuel energy comes from the same pure function as the meter (`ConsumptionModel::fuelGps`), not from the meter; cold enrichment, which does not burn, heats nothing.
- **Friction.** P_f = friction torque × ω: a = 45% goes to the oil (bearings, valvetrain, pump, roughly the share of those terms in Patton et al.), the rest to the liners.
- **Wax thermostat.** It starts opening at 87 °C (one of the three usual values: 82, 87, 92 °C) and needs 8 K to open fully, like the Land Rover Freelander 2's (83 to 91 °C, workshop manual, seen quoted). The play in the wax closes it 2 K lower: this is the hysteresis (assumed). Its bleed hole passes 0.5% of the flow (assumed). The pump delivers 0.25 L/s per litre of displacement and per 1000 rpm (assumed: about 2 L/s at 6000 rpm on a 1.4 L).
- **Radiator.** ε-NTU, cross-flow, both fluids unmixed: ε = 1 − exp(NTU^0.22/C_r·(exp(−C_r·NTU^0.78) − 1)). Air crosses the core at 30% of the car's speed, plus 3 m/s from the fan and 0.3 m/s of natural draft (assumed); UA ∝ ṁ_air^0.6 (louvred fins). It is sized the way an engine designer would for its hardest case, assumed here: 60% of the maximum fuel energy at 50 km/h, 40 °C outside, coolant at 110 °C inlet, ε = 0.6. The fan follows a thermoswitch at the bottom of the radiator: on at 95 °C, off at 84 °C (VW T3, single stage, Mecatechnic catalogue).
- **Masses.** Assumed, per litre of displacement: 30 kg of metal at 600 J/(kg·K) on the coolant side, 15 kg at 500 J/(kg·K) on the oil side; 1.2 L + 1 L/L of coolant in the engine, 1 L + 0.6 L/L in the radiator (glycol mix, 3.64 kJ/(L·K)); 2.5 L + 0.8 L/L of oil. The coolant stops at 129 °C: it boils under the cap, and the network no longer follows.

### Oil and friction

**Viscosity, Vogel's law.** μ = a·exp(b/(T + c)), a = 3.477·10⁻⁵ Pa·s, b = 1345.6 K, c = 144.9 °C, fitted exactly on three points of the data sheet of a synthetic 5W-30 (Motorcraft, "Product typicals", read): 60 mm²/s at 40 °C, 10.5 mm²/s at 100 °C, 4250 mPa·s at −30 °C (cold cranking viscosity), density 0.853 and expansion of 6.5·10⁻⁴/K. At 20 °C it is 14 times thicker than at 100 °C; an Italian review quoted says "up to 15 times".

**Hydrodynamic friction.** That of the Patton, Nitschke and Heywood model (SAE 890836): bearings 3.03·10⁻⁴·N·D³·L·n_b/(B²·S·n_c) and skirts 294·v̄_p/B, in kPa, mm, rpm and m/s, with bearings of 0.67 × 0.25 bore (mains) and 0.56 × 0.22 bore (rods). These coefficients are those reproduced by the work that builds on it, not re-read in the paper. Check: Petroff (torque = π·μ·ω·D³·L/(4c), radial clearance of 1‰ of the radius, oil at 100 °C) gives 5.7 kPa for the city car's bearings at 2000 rpm, the formula 5.9 kPa. They are proportional to engine speed.

**Scaling.** As in Sandoval and Heywood (SAE 2003-01-0725), the hydrodynamic terms follow the viscosity: here (μ/μ₁₀₀)^0.5. The exponent is quoted from memory; their abstract says cold oil gives "about twice" the friction of a warm engine. Here: 1.57 times at 20 °C and 2000 rpm, 2.15 times at 0 °C (`frictionFactor`). Above 100 °C nothing changes: the original friction model is calibrated warm, and thinner oil slides towards the mixed regime (Stribeck), which would compensate.

### What a cold start changes

After a cold start (`--start cold`, the `startTemp` setting), everything starts at the ambient air temperature, and three deviations from the warm reference act, all zero once warm:

- **Friction.** Cold oil adds its friction to the torque, cranking and stalled included: the starter turns more slowly, the engine stalls more easily. Combustion itself stays that of the warm engine.
- **Idle target.** The Saab 9-5's ECU (Trionic, online workshop manual, read) targets 1200 rpm at −30 °C coolant and 825 rpm once warm: +45% at −30 °C, in a straight line up to 70 °C (end assumed), so +23% at 20 °C. A carburettor does the same with its fast-idle cam.
- **Enrichment.** The fuel that stays liquid on cold walls and leaves unburnt. Assumed: λ ≈ 0.87 at 20 °C coolant with an ECU, 0.77 with a choke, which has no sensor, in a straight line up to 70 °C. It is paid for in consumption, with no torque or heat. Nothing on a diesel, which has neither throttle nor choke.

**After a warm start, nothing feeds back.** The network still runs, for the gauges, but neither torque, driving nor sound reads it, whatever temperature it computes: the 29 fingerprints are bit-identical, and a test makes the temperatures diverge (20 °C and −30 °C outside) without anything else moving. The spec sheet (0–100 km/h, top speed) is measured warm, even after a cold start.

**The sound.** Nothing was added to the sound: it follows the real engine speed and load. The idle starts higher and comes down as the coolant warms, in about ten minutes at idle at 20 °C, in four and a half minutes on the WLTC. At −10 °C the V8's idle breathes more: its level varies by 2.4 dB (standard deviation, 50 ms windows) against 1.3 dB warm, since the idle control opens further to hold nearly twice the friction. Listening remains to be done.

**On the electric car, none of this applies**: no network, no gauge, and the functions return the ambient air or zero.

### Against published figures

Measured with `revforge-render --out none --preset X --scenario wltc|idle --start cold|warm --fuel 1` (and `--seconds 589` for the first phase), at 20 °C.

| Preset | Low phase, warm → cold (L/100 km) | Full WLTC, warm → cold | Coolant at 70 °C | Thermostat open | Oil at 80 °C | 15 min idle, warm → cold (L/h) | Coolant at 70 °C at idle |
|---|---|---|---|---|---|---|---|
| `city_14` | 8.69 → 9.66 (+11%) | 6.76 → 6.89 (+3 g/km) | 264 s | 373 s | 371 s | 0.56 → 0.67 (+20%) | 632 s |
| `hot_hatch_turbo` | 12.80 → 14.09 (+10%) | 8.90 → 9.08 (+4 g/km) | 219 s | 293 s | 317 s | 0.84 → 1.00 (+19%) | 582 s |
| `v8_cross_plane` | 23.98 → 26.83 (+12%) | 13.90 → 14.28 (+9 g/km) | 277 s | 385 s | 401 s | 1.87 → 2.21 (+18%) | 605 s |
| `diesel_four` | 8.56 → 9.15 (+7%) | 7.58 → 7.66 (+2 g/km) | 289 s | 427 s | 402 s | 0.46 → 0.56 (+20%) | 725 s |
| `carb_twin_cam` | 13.51 → 15.22 (+13%) | 8.94 → 9.17 (+5 g/km) | 232 s | 337 s | 341 s | 0.85 → 1.05 (+23%) | 573 s |

On `diesel_four`, the driver restarts the engine once during the WLTC, warm and cold alike (see "Consumption"). At −20 °C, the city car idling takes 17 minutes to reach 70 °C coolant, and burns 48% more than warm.

| Measure | Model | Published | Source |
|---|---|---|---|
| Idle target, coolant at −30 °C | 1.45 times the warm idle | 1200 rpm for 825 | Saab 9-5, Trionic (read): the model is set on it |
| Idle at 20 °C, `city_14` | 985 rpm for 815 warm (1.21), 1105 just after starting | 1100 for 950 at 20 °C (1.16) in a tuning example; "1200 to 1800" at start-up | HP Academy, engineerfix.com (seen quoted) |
| Fast idle duration, in neutral at 20 °C | 9.5 to 12 min, until the coolant reaches 70 °C | "a minute or two" for the peak; catalyst heating for up to 5000 ignitions, about 2 min | engineerfix.com (seen quoted), Saab WIS (read) |
| Coolant at 70 °C, cold WLTC | 3.7 to 4.8 min | the cold period of real-driving tests (RDE) ends at 70 °C coolant, and lasts 5 min at most | Regulation (EU) 2017/1151 (seen quoted) |
| Oil behind coolant | about 6 K behind the coolant during the WLTC warm-up, then above | "about 10 °C behind" on a heavy-duty diesel | *Applied Sciences* 2020 (MDPI, seen quoted) |
| Penalty, first phase | +7% (diesel) to +13% (carburettors) over 589 s | +20% in the cold FTP phase (505 s) against the hot one, at 24 °C; +40 to +80% at −6.7 °C | Taylor and Stewart, SAE 2001-01-0221 (abstract read) |
| Penalty, full WLTC | +2 to +9 g/km of CO₂ (+1 to +3%) | +6.1 g/km on average over 20 cars | Tsokolis et al., *Applied Energy* 2016, JRC102610 (abstract read) |
| Cold friction | ×1.57 at 20 °C, ×2.15 at 0 °C, at 2000 rpm | "about twice" | Sandoval and Heywood, SAE 2003-01-0725 (abstract seen quoted) |

"Read": the page or document itself. "Seen quoted": a search excerpt or a reprint. The model's fast idle lasts longer than the peak drivers describe, which is catalyst heating: the model does not simulate it, and keeps the ECU's coolant-based idle, which comes down with it.

### What is missing

- **The cold-start penalty reads about half the published figures.** The model counts only the engine: no gearbox and axle oil, no cold tyres, no catalyst heating (spark retarded to about 10° after top dead centre at Saab, for up to 5000 ignitions), no poorer combustion on cold walls (of the order of 1 to 2%), no cabin heater. Nothing was tuned to close this gap.
- **Assumed**: the end of fast idle and enrichment at 70 °C, the enrichment itself, the masses, the conductances, the radiator's design case, the air speeds, the share of friction going to the oil, the mean gas temperature.
- **No oil-to-coolant heat exchanger**: the oil reaches 115–120 °C at the end of the WLTC, and about 140 °C towing a trailer at 45 °C.
- **Overheating**: with a working fan, idling at 45 °C does not overheat (the coolant holds around 97 °C); neither the air-conditioning condenser nor hot air recirculating under the bonnet is simulated. With a failed fan the coolant exceeds 115 °C (test). Beyond 129 °C the coolant boils: no coolant loss, no ECU protection.
- **After a warm start, the network only drives the gauges**: a warm engine idling for an hour at −30 °C cools on screen without its friction changing.
- The rotary has only its bearings in the hydrodynamic friction (no skirts), with the same quarter less as its friction (assumed). The on-screen gas and the page's heat (`Motion.heat`) do not read this network, and the steam of a cold exhaust is not drawn.
- The radiator air is at 101,325 Pa: altitude is not in it yet.

### What other subjects read and are given

Read: the ambient temperature (`EngineConfig::ambientC`, 20 °C by default). Exposed, as pure functions: `oilViscosityPaS`, `hydrodynamicNmPerRpm`, `coldFrictionNm`, `frictionFactor`, `coldIdleRise`, `coldEnrichment`, `wallHeatShare`; and in `DrivingModel::thermal()`: `coolantC`, `oilC`, `metalC`, with `coldIdleRpm`, `coldEnrichment`, `coldFrictionNm` for what acts. For an engine started warm to stay bit-exact, an effect plugged into these temperatures goes, as here, through a deviation from the warm reference (`kWarmCoolantC`, `kWarmOilC`) that acts only after a cold start (`thermalCoupled()`).

## Combustion

Three settings in the engine panel, "Combustion" section: the **compression ratio** (0 = the stock one), the **fuel grade** (RON 95,
RON 98, race fuel 102, E85, methanol), and what follows from them, **knock**. The code is in `core/src/combustion.cpp` (pure functions
and `KnockMap`), `DrivingModel::stepKnock` (the sensor) and `core/src/knock_sound.cpp` (the sound). At the defaults none of it runs:
`combustionStock` returns true, `fullLoadTorqueNm` keeps its original path, no knock map is built, the sound draws nothing more, and the
29 fingerprints stay bit-identical.

### The compression ratio

Today's engine assumes 10.5:1 naturally aspirated, 9.5:1 boosted, 18:1 diesel, 9.4 or 8.5:1 on a rotary (`stockCompressionRatio`, the
starter's values). The setting is bounded to what exists (`compressionRatioOf`): a diesel goes no lower than 14:1, the lowest
production diesel (Mazda Skyactiv-D), and a rotary no higher than 11.9:1, the highest production rotary (Mazda 8C, 2023), as the rotor
runs out of pocket (assumed); petrol goes from 6 to 24.

Indicated efficiency follows the fuel-air cycle, then what the walls take:

```
η(r) = η_fa(r) · (1 − k · (2 (r − 1) · B/S + 4))
η_fa = 1 − r^(1−γ)                                   Otto, γ = 1.3 (Heywood, ch. 5)
η_fa = 1 − r^(1−γ) · (β^γ − 1) / (γ (β − 1))         Diesel, cutoff ratio β = 2 (assumed)
```

The second factor is the chamber surface at top dead centre, scaled by the bore: it grows with the ratio, and the heat lost grows with
it. k = 0.00533 puts the optimum at 17:1 on a square engine, as Caris and Nelson measured (1959, cited by Heywood). From 10.5 to 12.5:1
efficiency gains 2.5%; an 18:1 diesel is already close to its optimum, and 16:1 costs it only 0.75%, which is what recent diesels lowered
to 15.5 or 16:1 show. On a rotary the same calculation goes through its elongated chamber (`rotaryCycleEfficiency`, see "The rotary
engine").

This ratio `η(r)/η(r₀)` scales the gas work, not friction, in `fullLoadTorqueNm`; consumption scales its efficiency by the same ratio, so
fuel per kilogram of air does not change. The ratio also changes the starter (`compressionLoadNm` reads the real ratio) and the expansion
at exhaust opening: the Otto pressure `p₄/p₁ = 1 + q/(c_v·T₁·r^(γ−1))` falls as the ratio rises, and the strength of the sound's pulses
follows (`releasePressureShare`). The gearbox stays the stock engine's, as with the air filter: its final drive does not move.

### Fuels

| | LHV (MJ/kg) | Stoichiometric AFR | kg/L | RON / MON | Vaporisation (kJ/kg) | Energy per kg of air |
|---|---|---|---|---|---|---|
| RON 95 (reference) | 42.9 | 14.7 | 0.745 | 95 / 85 | 350 | 2.92 MJ |
| RON 98 | same | same | same | 98 / 88 | same | same |
| Race fuel 102 | same | same | same | 102 / 90 | same | same |
| E85 | 29.3 | 9.75 | 0.782 | 107 / 89 | 831 | 3.01 MJ (+3.0%) |
| Methanol | 19.9 | 6.47 | 0.792 | 109 / 89 | 1168 | 3.08 MJ (+5.4%) |
| Diesel | 43.1 | 14.5 | 0.832 | — | 250 | — |

Petrol keeps exactly the figures of the original consumption model. RON 98 and 102 differ only by octane (EN 228 for 98; common values,
assumed, for 102). E85 is not typed in: it is computed from 85% ethanol by volume and 15% hydrocarbons (Heywood, appendix D; NIST for the
heats of vaporisation at 25 °C), except its octane numbers, which are measured and do not blend linearly (common values, within a few
points). Molar masses and boiling points give the vapour pressure by Clausius-Clapeyron.

Vaporisation cools the charge: `evaporationCoolingK` takes the latent heat of the share of fuel that evaporates at the expense of the air,
until its vapour saturates at the temperature reached. With port injection and carburettors, that share is 0.15 (assumed: most of it
evaporates on the hot valve and port): 3.6 K for petrol, 12.7 K for E85. Fully evaporated into air at 320 K, E85 would saturate at 40 K
and methanol at 49 K, well before the 84 and 182 K their latent heat would allow. The colder charge is denser, but its vapour displaces
a little air: `fuelTorqueShare` multiplies the energy per kilogram of air by this density, against the reference petrol, in
`fullLoadTorqueNm`. Fuel use in litres follows each fuel's density and LHV.

None of this applies to a diesel: no octane number (it ignites by compression, so the choice is hidden in the panel), no spark knock,
no evaporative cooling (diesel is injected near top dead centre, after intake closing). Its ratio does count: efficiency, starter, and
clatter. The clatter follows the share of fuel that burns at once, hence the ignition delay of Hardenberg and Hase (1979), computed at top
dead centre of a polytropic compression (n = 1.35) from 330 K and 1 atm: at 15:1 the delay is 26% longer, at 22:1 11% shorter
(`dieselKnockScale`). The pressure rise, hence the clatter (`kDieselKnock`) and the combustion noise of the block (Anderton's law), follows
in proportion: +2 dB at 15:1, −1 dB at 22:1.

### Knock

**The chamber.** `endGasCycle` steps a single-zone combustion degree by degree, from intake closing (50° after bottom dead centre, the
sound's stock timing) to exhaust opening: a connecting rod 3.2 times the crank radius, γ = 1.3, Wiebe combustion (a = 5, m = 2) over 45°
after a 0.35 ms flame kernel, and 80% of the fuel's heat to the gas (the rest to the walls and unburnt fuel). The charge arrives from the
manifold, warmed by the walls (20% of the gap to the coolant plus 40 K, less when it is dense: h ∝ (ρv)^0.8), cooled by vaporisation,
and mixed with 4% residual gas at 1000 K. Its mass is what the torque model's filling gives (0.92 × breathing), at the temperature it
really has. At 3000 rpm and full load, this cycle gives a 60 bar peak and 12 bar of indicated mean pressure on a 10.5:1 naturally
aspirated engine: the order of magnitude of real ones.

**The end gas.** Fresh gas is compressed by the piston, then by combustion, at γ = 1.30 (wall losses included):
`T_u = T_IVC · (p/p_IVC)^((γ−1)/γ)`. Its auto-ignition delay is Douaud and Eyzat's (1978):

```
τ = 17.68 · (ON/100)^3.402 · p^−1.7 · exp(3800/T)     ms, p in atm, T in K, ON the research octane number
I = ∫ dt / τ                                          Livengood and Wu, up to 90% burnt
```

The mean cycle knocks if the integral reaches 1 before the flame has burnt 90% of the mixture.

**The map.** The calculation is too slow for every 1 ms step: `KnockMap` does it once per setting, on 10 speeds and 15 advances, and keeps
it (`knockMapOf`, four engines at a time, no allocation). Pressure is not tabulated: at equal temperature, the whole pressure trace is
proportional to the intake-closing pressure, and the integral follows its 1.7th power. At a charge pressure `k` times full load, the
limit is the advance where the integral equals `k^−1.7`. Best torque (MBT) burns half the mixture 8° after top dead centre (Heywood).

**No two cycles alike.** Combustion phasing varies from cycle to cycle (assumed: 1.5° standard deviation, a bell bounded at three standard
deviations, the sum of three uniform draws). The **knock limit** is the advance at which one cycle in fifty knocks, two standard
deviations below the mean cycle's.

**The stock map** is set as a calibrator would: best torque, except where it would knock, where it stays 2° under the stock engine's
limit, on RON 95, in reference air. Even its fastest cycle then stays below the mean cycle's limit: **no preset knocks at its own
settings**, and the presets' calibrated torque is that map's. It stays the same when the ratio or the fuel changes: it is the ECU's map,
not a retuned engine's.

| Preset | Ratio | Smallest margin | at | Largest retard from MBT | at | MBT reached from |
|---|---|---|---|---|---|---|
| `city_14` | 10.5 | 2.0° | 1000 rpm | 9.4° | 1000 rpm | 2250 rpm |
| `four_cyl_sport` | 10.5 | 2.0° | 1000 | 10.1° | 1000 | 2250 |
| `three_cyl_turbo` | 9.5 | 2.0° | 1000 | 22.1° | 2000 | 6000 |
| `hot_hatch_turbo` | 9.5 | 2.0° | 1000 | 22.3° | 2250 | 6500 |
| `hot_hatch_na` | 10.5 | 2.0° | 1000 | 10.0° | 1000 | 2250 |
| `five_cyl_turbo` | 9.5 | 2.0° | 1000 | 24.3° | 2250 | never |
| `inline_six` | 10.5 | 2.0° | 1000 | 10.0° | 1000 | 2250 |
| `v6_gt` | 10.5 | 2.0° | 1000 | 10.6° | 1000 | 2500 |
| `flat_six` | 10.5 | 2.0° | 1000 | 9.8° | 1000 | 2250 |
| `boxer_four_turbo` | 9.5 | 2.0° | 1000 | 20.7° | 2250 | 6500 |
| `v8_cross_plane` | 10.5 | 2.0° | 1000 | 11.1° | 1000 | 2500 |
| `v10_open` | 10.5 | 2.0° | 1000 | 8.0° | 1000 | 2000 |
| `v12` | 10.5 | 2.0° | 1000 | 8.7° | 1000 | 2000 |
| `retro_coupe` | 10.5 | 2.0° | 1000 | 10.2° | 1000 | 2500 |
| `v8_supercharged` | 9.5 | 2.0° | 1000 | 17.6° | 1000 | 5000 |
| `carb_twin_cam` | 10.5 | 2.0° | 1000 | 10.6° | 1000 | 2500 |
| `twin_rotor_13` | 9.4 | 2.0° | 1000 | 20.6° (shaft) | 1000 | 2500 |
| `diesel_four`, `electric` | | no spark | | | | |

Steady full load, from 1000 rpm to the redline, by `revforge-render --out none --preset X --scenario steady --seconds 1 --knock 1`, which
prints the map speed by speed; the test "aucun préréglage ne cliquette à ses réglages" checks it every 50 rpm from idle. The naturally
aspirated engines are knock-limited below 2000 to 2500 rpm, as real 10.5:1 engines are on RON 95; the 9.5:1 turbos at 1.2 to 1.5 bar are
limited all the way up, with half the mixture burnt around 29° after top dead centre at 2000 rpm on the GTI, the order of magnitude of
boosted engines at 20 bar mean pressure (from memory: 25 to 35°, no source re-read).

What the model gives at 1500 rpm full load on the city car: −4.2° per point of compression, +0.69° per octane number, −0.85° for 10 °C
more air. That is about 6 octane numbers per point of compression, where the engine builders' rule says 3 to 5 (from memory, no source
re-read): the model is probably a little too sensitive to the ratio.

**The sensor.** Each detected knock takes 1.5° from its cylinder, the advance returns at 1° per second once it is quiet, never more than
20° taken (assumed, orders of magnitude of production ECUs). On average: `dρ/dt = 1.5° × share of knocking cycles × combustions per second
per cylinder − 1°/s`. It settles around 2% of knocking cycles, at the limit. **A carburettor has no sensor**: it knocks. Gas work follows
the applied advance against the stock map's, through the same cycle (`KnockMap::torqueShare`), on the torque produced at each step; the
spec sheet takes the steady state. Fuel follows the air: retarding wastes fuel, it does not save it.

**What it gives**, with `revforge-render --out none --preset X [setting] --scenario wltc --fuel 1`:

| Setting | Peak torque | Peak power | 0-100 km/h | WLTC | Best point |
|---|---|---|---|---|---|
| `city_14` | 111.4 Nm | 56.6 kW | 13.34 s | 6.76 L/100 | 245 g/kWh |
| at 12.5:1 | 114.6 (+2.9%) | 58.5 (+3.4%) | 12.96 s | 6.61 (−2.2%) | 238 |
| on E85 | 119.0 (+6.8%) | 60.9 (+7.6%) | 12.47 s | 9.48 (+40%) | 356 |
| on methanol | 128.1 (+15%) | 66.0 (+17%) | 11.68 s | 13.88 (+105%) | 522 |
| `hot_hatch_turbo` | 357.0 Nm | 194.4 kW | 6.86 s | 8.90 L/100 | 240 g/kWh |
| on race fuel 102 | same | same | same | same | same |
| on E85 | 380.1 (+6.5%) | 208.1 (+7.0%) | 6.50 s | 12.49 (+40%) | 350 |
| at 10.5:1 | 355.2 (−0.5%) | 193.9 (−0.3%) | 6.85 s | 8.76 (−1.6%) | 235 |
| `carb_twin_cam` | 171.5 Nm | 89.5 kW | 8.65 s | 8.94 L/100 | 253 g/kWh |
| at 12:1 (knocks, no sensor) | 175.6 (+2.4%) | 91.8 (+2.6%) | 8.51 s | 8.78 (−1.8%) | 247 |
| `diesel_four` | 315.1 Nm | 113.5 kW | 12.94 s | 7.58 L/100 | 196 g/kWh |
| at 16:1 | 312.5 (−0.8%) | 112.5 | 13.02 s | 7.63 (+0.7%) | 198 |
| at 22:1 | 314.6 | 113.3 | 12.95 s | 7.59 | 196 |
| `twin_rotor_13` | 226.7 Nm | 167.3 kW | 7.13 s | 13.35 L/100 | 287 g/kWh |
| at 11:1 | 237.9 (+4.9%) | 176.7 (+5.6%) | 6.92 s | 12.88 (−3.5%) | 274 |

A higher ratio pays down low, where the sensor takes advance away (up to 7.4° on the city car at 13:1, flat out in fourth from 1800 rpm),
and gains up high. A better octane changes nothing while the map stays stock: it buys margin, which the ECU's advance offset can take.
E85 and methanol gain by their energy per kilogram of air and their colder charge, and cost 40% and 105% more litres, the ratio of their
energy per litre. These gains at equal advance are a little above what is usually read for E85 (3 to 5%, no source re-read): the share
evaporated into the air is assumed.

### The sound of knock

When the end gas ignites by itself, pressure jumps and the chamber rings on its acoustic modes (Draper, 1938):

```
f_mn = α_mn · c / (π · bore)          α = 1.841 (1st circumferential), 3.054 (2nd), 3.832 (1st radial)
c = √(γ R T)                          burnt gas: γ = 1.25, R = 289.7 J/(kg·K), T from the cycle at 90% burnt (≈ 2580 K)
```

That is 6.59 kHz for an 86 mm bore, 6.75 kHz for 84 mm, 7.55 kHz for 75 mm. Against published measurements:

- the tuners' rule, 1800/(π × bore in mm) kHz, gives 6.66 kHz for 86 mm, within 1% of the model (HP Academy forums, seen quoted);
- Mittal (SAE 2018-01-1666) finds the first circumferential mode near 6 kHz on a current engine, Eng (SAE 2002-01-2859) between 5 and
  6 kHz (abstracts read);
- a 126 mm bore gas engine (Physics, 2021, excerpt seen quoted) shows bands at 4.3-5.4, 6.4-7.7, 8.9-9.6, 10.3-11.4 and 12.2-13.1 kHz:
  the model puts its first five modes at 4.50, 7.46, 9.36, 10.26 and 12.99 kHz, one in each (a test checks it).

On a rotary the chamber is a slab between two apexes: its first mode runs lengthwise, `c/(2L)`, L the chord (√3 times the generating
radius), 2.7 kHz on the 1.3 L twin-rotor; then across, then the second lengthwise.

Each combustion draws, from its own generator (`KnockSound`), its phasing within the cycle-to-cycle spread; if it passes the limit, the
chamber rings 15° after top dead centre. The oscillation's peak (MAPO) is 0.3 bar at the limit and grows by a factor e every 3° beyond,
10 bar at most (assumed; it is the shape tests show). It dies out in 1 ms (assumed), on three modes weighted 0.6, 0.25 and 0.15, and its
pitch falls while it rings: the gas expands, `f ∝ V^−(γ−1)/2`. The block radiates it into the engine bay (`Source::Bay`), with 100 dB of
attenuation from chamber to 1 m (order of magnitude of Priede's structural attenuation curves, assumed): 1 bar of oscillation gives 1 Pa
at 1 m, 94 dB. It does not go through the body, which does not radiate such highs.

Under the bonnet, the carburettors at 12:1 in fourth, flat out from 1900 rpm, knock 249 times in 16 s, two thirds of the combustions near
2000 rpm; the 5.5 to 8 kHz band rises 10 dB above the stock engine, then falls back around 3400 rpm, where it stops. A spectrogram in
3 ms windows shows short strokes at 6.7, 11 and 14 kHz, leaning downwards. **Listening is still to be done**:

```
./build/revforge-render --out knock.wav --preset carb_twin_cam --compression 12 --scenario turbo --seconds 20 --listener bay
./build/revforge-render --out stock.wav --preset carb_twin_cam --scenario turbo --seconds 20 --listener bay
./build/revforge-render --out sensor.wav --preset city_14 --compression 13 --scenario turbo --seconds 20 --listener cabin
./build/revforge-render --out diesel15.wav --preset diesel_four --compression 15 --scenario idle --seconds 10 --listener bay
```

### What it reads, and what it exposes

Read, at its reference value until the other topics are wired (`KnockMap::Inputs`): ambient air (293.15 K, 101,325 Pa), coolant
(363.15 K), intake closing (50° after bottom dead centre) and residual gas (4%), charge cooling by direct injection or water (0), the
advance map offset (0). The charge pressure and temperature of a boosted engine are the current model's at equilibrium
(`TurboSteady::coolerOutK`, added, and `chargePa`; `SuperchargerSteady`). Full-load mixture is the consumption model's (0.88 naturally
aspirated, 0.82 boosted).

Exposed: `fuelProperties`, `energyPerAirJPerKg`, `evaporationCoolingK` (with saturation), `compressionRatioOf`, `indicatedEfficiency`,
`compressionEfficiencyShare`, `fuelTorqueShare`, `douaudEyzatDelayS`, `endGasCycle`, `mbtAdvanceDeg`, `knockingShare`, `draperHz`,
`dieselIgnitionDelayDeg`; `KnockMap::at` gives, for a speed and a charge pressure, best torque, the **stock map** (`stockDeg`, to use as
the reference requested advance) and the knock limit; `DrivingModel` gives the applied advance, the limit, the advance taken away and the
share of knocking cycles (`rf_spark_advance`, `rf_knock_limit`, `rf_knock_retard`, `rf_knock_share`). The map is only built when combustion
is no longer stock (`knockMapOf`): once wired, air, coolant, cam or requested advance will need to trigger it too.

### What is assumed, and what is missing

- The single-zone cycle, its constants (heat to the gas, flame kernel, Wiebe duration, wall heating, residual gas) and the cycle-to-cycle
  spread are assumed, set to give pressures and advances of the order of real ones. Douaud and Eyzat fitted their law on reference fuels
  (isooctane and heptane): the research octane number stands in for the index, without the sensitivity (RON − MON) that favours alcohols
  on boosted engines, and the law is extrapolated above 100.
- Knock only costs torque through the advance taken away: no extra heat to the walls, no piston wear, no failure.
- The advance the sensor takes away neither heats the exhaust nor spools the turbo, as a real one would: the ECU topic will give exhaust
  temperature against advance.
- E85 and methanol burn no faster than petrol in the model, and their own efficiency is no better.
- The share of fuel evaporated into the air (0.15), the structural attenuation (100 dB), the oscillation's peak and duration are assumed:
  listening will decide.
- The gas view follows the compression ratio (its clearance volume), not the fuel nor knock; the 2D and 3D views do not change.
- A diesel does not refuse to start cold at too low a ratio: the setting stops at 14:1.

## The engine management

The engine management (`core/src/ecu.cpp`, `ecu.hpp`) has two parts. Its **map**, spark advance and mixture, is a set of pure functions read by the torque, the fuel figures, the turbine and the sound. What it **does at each step** of `DrivingModel`, the rev limiter, the launch control, the flat shift and the slip control, comes out through `EcuAction`, and reaches the sound through `EcuDrive`, with its own random generator. At the defaults none of this runs: the fingerprints and `check_wasm` are unchanged, and a test checks that a whole lap without any setting never sees the engine management act.

### The spark map

Indicated torque follows a parabola around the best-torque timing (MBT): `T/T_MBT = 1 − (Δ/68°)²`, Δ the crank-angle offset from MBT, roughly symmetric on both sides (Heywood, *Internal Combustion Engine Fundamentals*, ch. 9: a flat top, a few percent at ±10°). It is the anti-lag's parabola: 2.2 % at 10°, 8.7 % at 20°. **Assumed:** the stock map sits at MBT. Knock (the "combustion" topic) will pull it away: `knockRetardDeg` is already a parameter of `demandedAdvanceDeg`, `mapRetardDeg` and `ecuFullLoadShare`, at 0 until it is wired. The ±10° `--advance` offset can therefore only lose torque here; advance only gains by getting out of knock.

The requested advance, in degrees before top dead centre (`demandedAdvanceDeg`), starts from `mbtAdvanceDeg`: the spark that puts half the burn about 8° after top dead centre. From spark to that half, 34° at 3000 rpm full throttle, × (N/3000)^0.25 (turbulence follows the piston: the duration in degrees grows little), × (1 + 0.6 (1 − fill)) at light load. That gives 26° at 3000 rpm full throttle, 32° at 6000, 31° at half load at 1500; published maps of naturally aspirated engines give 25 to 35° at full load. An assumed order of magnitude: only the spec sheet reads it so far.

The work the piston does not get goes into the gas, minus 20 % into the walls: `ΔQ_gas = η_i (1 − T/T_MBT) × 0.8` of the fuel energy, η_i = 0.405 (0.375 boosted), the efficiency of `ConsumptionModel`. On the advanced side that work goes into the walls, not the gas. **Limit:** near MBT the rise in exhaust temperature is probably underestimated, since the lower wall losses with retard are not counted (Heywood, ch. 12): +17 K for 10° of retard at full load, where a few tens are often quoted.

### The mixture

`--lambda` sets the full-load mixture; 0 keeps the stock one, 0.88 (0.82 boosted), that of `ConsumptionModel`. Torque per kilogram of air follows the curve the carburettor already reads (`torqueOfRichness`, after Heywood, ch. 5): a flat top around λ 0.87, then −6.3 % at λ 1, −12.7 % at λ 1.1 and −3.4 % at λ 0.70 against λ 0.88. The mixture only leaves closed loop (λ 1) above 85 % fill, the fuel model's ramp: part-load torque does not change. A carburettor meters through its jets, a diesel always burns with excess air: the setting does nothing to them, and the site hides it.

Gas heating follows the adiabatic flame temperature of a hydrocarbon in air at 298 K (usual tables, order of magnitude): a peak around φ = 1.05, −2 % at λ 0.88, −6 % at 0.82, −14 % at 0.70. **Limit:** on a real engine, enrichment cools the exhaust more, partly because it allows more advance without knock: that part will belong to knock.

### What the map changes

Full-load torque (`fullLoadTorqueNm`, hence the curves, the spec sheet and the driving) is scaled outside friction: `(T + T_f) × share − T_f`. The fuel follows the air: for the same air, a shifted spark gets less from each gram, so the efficiency of `ConsumptionModel` is scaled by the same share, and the mixture sets the fuel needed per gram of air. The turbine gets hotter or cooler gas (`TurboSpec::ecu`, the turbo staying the stock one), and the sound opens the valve on that much more pressure (`evoRatio`, the anti-lag's balance).

Measured on `hot_hatch_na` (2.0 L, 7300 rpm); fuel on a warm WLTC:

| | Stock | Advance −10° | −5° | +5° | λ 1.0 | λ 0.80 | λ 0.75 | λ 1.1 |
|---|---|---|---|---|---|---|---|---|
| Peak torque (Nm) | 194.1 | 189.2 | 192.9 | 192.9 | 179.7 | 194.0 | 191.3 | 165.5 |
| Peak power (kW) | 117.8 | 114.5 | 117.0 | 117.0 | 108.2 | 117.7 | 115.9 | 98.6 |
| g/kWh at peak power | 304 | 313 | 306 | 306 | 291 | 334 | 362 | 290 |
| Gas at the cylinder outlet, peak power | 863 °C | +17 K | +4 K | 0 | +20 K | −41 K | −69 K | −18 K |
| WLTC (L/100 km) | 9.07 | 9.30 | 9.13 | 9.13 | 9.05 | 9.07 | 9.07 | 9.03 |
| 0-100 km/h (s) | 8.72 | 8.82 | 8.75 | 8.75 | 9.14 | 8.72 | 8.80 | 9.60 |

On `hot_hatch_turbo`: λ 1 removes 6.9 % of torque (0-100 in 7.15 s instead of 6.86), 10° of retard 2.3 %, and costs 1.9 % fuel on the WLTC (9.07 L/100 km instead of 8.90).

λ 0.80 barely changes the torque, but costs 10 % more fuel at peak power; λ 1 saves 4 %, for 8 % less power. The WLTC rarely reaches full load: the mixture barely changes it there, the advance does.

### The rev limiter

- **Stock (`stock`), the behaviour from before this engine management, unchanged.** At the redline, combustion is cut, fuel and spark (a carburettor keeps metering), for at least 30 ms, and only comes back 2.6 to 5.4 % lower: 4 % × (0.65 + 0.7 u), u drawn from a congruential generator at each restart, so that no two bounces are alike. The throttle stays open: little pumping, no turbo dump. In the sound, the pulse pressure drops by 95 %, and everything that follows the flow drops with it (see "The rev limiter" above).
- **Soft (`soft`).** Over the last 3 % below the redline, the spark retards up to 15° (5 % torque); beyond, a growing share of the cylinders is cut, all of them at +2 %: an injection cut, firing by firing, in firing order (in the sound, an accumulator that skips the asked share). On a 2.0 L in neutral, flat out, it holds at +1.4 % without a single hard cut, with 40 % of the firings.
- **Ignition cut (`ignition_cut`).** The same bounce as stock, but only the spark is cut: the fuel is still injected. The cut cylinder's mixture reaches the manifold, where 45 % of it burns if the gas there is above 800 K, all of it at 1100 K: the anti-lag's cylinder-cut figures. On a turbo, that heat goes to the turbine; on a naturally aspirated engine, the gas at the cylinder outlet, tracked with the same wall inertia as the turbine's (0.35 s to heat, 0.9 s to cool), decides whether it ignites. In the sound, each opening of a cut cylinder has 45 % × ignition chance to bang, at the amplitude √(E/90 J) of an ordinary pop, E the energy that burns: about 760 J per cylinder on a 2.0 L at 7200 rpm, twice that of a cylinder cut by the anti-lag. Three blips to the limiter on a 2.0 L: 23 bangs against none, and 47 mL of fuel against 25 in 14 s. **Not done:** the on-screen gas shows this cut like the stock one, pulses that have not burnt, without the manifold flame.

A diesel has no spark: its ignition cut goes back to the stock fuel cut, and the soft limiter only cuts injection. The limiter speed goes through `limiterRpmFor`: the redline, or 2 % below valve float if it comes first (the "pushrods" topic, 0 until wired).

### The launch control

It arms flat out (90 %), car stopped, engine decoupled: clutch pedal down, or an automated gearbox that keeps the clutch open while the foot stays on the brake. It holds the chosen speed with the ignition cut (same bounce), with 25° of retard (assumed: the order of magnitude of tuned engine managements). The retard and the mixture burning in the manifold heat the turbine: at a standstill, boost builds. It lets go as soon as the car rolls (1 m/s) or the clutch has caught the engine: the clutch then closes from that speed, on all the torque. The spec sheet's 0-100 uses it: three seconds of brake and full throttle, timed from the brake release. `--scenario launch_control` makes the same start and prints its figures.

| Spec-sheet 0-100 km/h, timed from the brake release | Without | Launch control |
|---|---|---|
| `hot_hatch_turbo` (2.0 L turbo, front drive), at 4500 rpm | 6.86 s | 6.13 s |
| 7.0 L naturally aspirated V8, rear drive, 1700 kg, at 3500 rpm | 6.18 s | 6.03 s |

At a standstill, `hot_hatch_turbo` at 4500 rpm: 4406 rpm and 1.03 of the target boost at the brake release, the gas before the turbine up to 1129 °C (899 without launch control), 27 bangs in three seconds.

**Limit:** without launch control, the automated gearbox already lets the clutch slip against the brakes (at 2350 rpm on `hot_hatch_turbo`), which also spools the turbo: that forced start, unrealistic for a dry clutch, even gives 5.94 s from the release, against 6.10 with launch control. It is the behaviour from before, kept. Likewise, with the brake held, the driven wheels spin (the brake acts on the body): slip control cuts the engine there.

### The flat shift

On upshifts, foot above 80 %, on a synchro or dog box driven by the robot: the foot is no longer lifted. The engine management cuts the spark, the fuel still flowing, while the engine is above the next gear's speed: it holds it there during the shift. Throttle open, no vacuum: the blow-off valve stays shut, and the mixture burning in the manifold keeps the turbine spinning. On a sequential box, the stock cut also lets the fuel through. A dual-clutch or a torque converter already shift under load: nothing changes there. On `hot_hatch_turbo`, flat out: the lowest boost during the shifts stays at 0.99 to 1.02 of the target instead of dropping to 0.16 to 0.23; 150 km/h in 13.3 s instead of 13.8; 0-100 in 6.54 s instead of 6.86; one to four bangs per shift.

### The slip control

A proportional-integral control of the driven-wheel slip that `DrivingModel` already computes (m/s), towards the share of torque to remove: `u = 0.1 e + ∫ 1.0 e dt`, gains tuned by trial so that the control does not beat against the wind-up of the drive shafts (2 to 3 Hz in first), hence assumed. The integral takes at most 1 m/s of error, and does not grow in the direction where the command saturates (anti-windup): without that, once the wheels gripped again, it held the engine cut down to 550 rpm. Retard first, up to 25° (13.5 % torque), then cut cylinders, at the injectors. On an electric car, the motor torque directly. It only acts with the wheels held by the engine: a slipping clutch only transmits its capacity, and cutting the spark would only stall the engine.

**The target, and what the tyre makes of it.** The model's tyre, `F = μN s/√(1+s²)`, s in m/s, **has no peak**: the more it slips, the more it pushes. The target is therefore read on its curve, as the share of grip the mode keeps in reserve for cornering: 90 % in sport (2.1 m/s), 80 % in full (1.3 m/s), assumed. The mean slip in first, once the clutch is closed, holds these targets within 15 % (tested). A real tyre peaks around 10 % slip and loses grip beyond, and slip control gains time there; here it can only hold the wheelspin back, and pay for it. Changing the tyre would change the defaults: it is not done.

| Spec-sheet 0-100 km/h | Off | Sport | Full |
|---|---|---|---|
| 7.0 L V8, rear drive, the model's tyre | 6.18 s | 6.20 s | 6.86 s |
| The same, tyre with a peak (trial on a copy, not shipped) | 6.38 s | 5.86 s | 6.10 s |
| `hot_hatch_turbo`, front drive | 6.86 s | 6.86 s | 6.86 s |

The trial's peaked tyre follows the Pacejka shape, `sin(1.4 atan(0.92 s))`: its peak at 2.25 m/s, 81 % when fully sliding. With it, sport slip control gains half a second, and full three tenths: that is what wiring a real tyre would change, to be done for all cars at once. The front-drive 2.0 L turbo never slips up to its targets in an ordinary start.

### The sound, and what is left to listen to

Everything goes through `setEcu`: the pressure at the opening of the fired cylinders (retard, mixture), the skipped firings of the partial cut, and the bangs of a cylinder whose spark only is cut, through the `pops` path. Measured on blips (2.0 L): at the limiter, the 30-120 Hz band gains 2.6 dB with the ignition cut; the soft limiter erases the bounce's stripes on the spectrogram. At launch control, the bursts of cut show as a row of vertical bands, at a standstill. **Nothing has been listened to:** the chance to bang, the energy that burns and the 90 J reference come from the anti-lag, and only listening will tell whether these bangs sound like a real ignition cut. Commands: `revforge-render --out x.wav --cylinders 4 --cc 2000 --bore 86 --redline 7200 --exhaust sport --limiter ignition_cut --scenario blips`, `--preset hot_hatch_turbo --launch 4500 --scenario launch_control`, `--preset hot_hatch_turbo --flat-shift 1 --scenario launch --seconds 16`.

### What it reads, and what it exposes

| Quantity | Function | Value until wired |
|---|---|---|
| Read: advance removed by knock (combustion) | parameter `knockRetardDeg` | 0 |
| Read: valve float speed (pushrods) | `limiterRpmFor(redline, valveFloatRpm)` | 0: the redline |
| Read: coolant temperature (thermal) | `warmupRichness(coolantC)`, `demandedLambda(…, coolantC)` | 90 °C: no enrichment |
| Read: ambient air (atmosphere) | gas at the cylinder outlet, `kPortIntakeK` | 293 K |
| Exposed: requested advance (° BTDC) | `demandedAdvanceDeg(e, rpm, load, knock)`, `mbtAdvanceDeg` | |
| Exposed: requested mixture | `demandedLambda(e, load, coolantC)`, `fullLoadLambdaOf` | |
| Exposed: torque and heat by advance and λ | `sparkTorqueShare`, `sparkExhaustGain`, `lambdaTorqueShare`, `lambdaHeatShare`, `ecuFullLoadShare` | |
| Exposed: what it does at each step | `DrivingModel::ecuAction()`, `ecuAdvanceDeg()`, `ecuLambda()` | |

Telemetry: `rf_ecu_advance`, `rf_ecu_lambda`, `rf_ecu_fired`, `rf_ecu_limiter`, `rf_ecu_launch`, `rf_ecu_flat`, `rf_ecu_traction` (warning lights and the readings table); spec sheet: `rf_ecu_full_share`, `rf_ecu_spark_deg`, `rf_ecu_exhaust_c`. `--trace` writes them as columns.

## Fuel injection

`FuelSystem::Direct` (petrol direct injection), `DieselInjection` (mechanical pump, the reference, or common rail), `egr` and `particulateFilter`. `core/src/injection.cpp`, `core/src/particulate_filter.cpp`, `core/src/injector_sound.cpp`. At the defaults (port injection, mechanical pump, no recirculation, no filter), none of what follows runs: the 29 original fingerprints do not move. Everything is written as pure functions that take the coolant temperature, the fuel and the charge air as parameters, at their reference values until their own agents are wired in.

**Charge cooling.** Vaporising the fuel takes its latent heat: `ΔT = (m_f/m_a)·L_v / (c_p,air + (m_f/m_a)·c_p,vap)`, with `m_f/m_a = 1/(AFR·λ)`. For RON 95 petrol (L_v = 305 kJ/kg, Heywood, appendix D), 18.5 K at λ = 1, 20.8 K at λ = 0.88. The question is who pays for it. Port injection aims at the closed valve: most of the heat comes from the valve and the port (Anderson et al., SAE 962018), 20 % from the air (assumed). Direct injection sprays into the cylinder air: 70 % (assumed, about 30 % of the spray reaches the piston and the liner, Zhao, Lai and Harrington, 1999). The d² law says whether the droplets have time: Sauter diameter 13 µm at 200 bar (as p^−0.25), K = 5·10⁻⁸ m²/s, 170° between start of injection and intake closing. At any car engine speed, everything evaporates before the valve closes. Hence, at full load, 14.5 K less at intake closing (4.2 K with port injection), and 29 K at top dead centre for an effective compression of 9 (`chargeCooling`, for knock). Filling gains `T/(T − ΔT) − 1` on a charge at 323 K, i.e. 3.3 %: Anderson et al. measure 2 to 3 % against a production port-injected engine, Wyszynski et al. (SAE 2002-01-0839) 9 % on a single-cylinder engine. `fullLoadTorqueNm` applies it to the gas work, not to friction.

**The high-pressure pump.** A cam-driven piston pump: `T = Q_fuel·p_rail/(η·ω)`, η = 0.8 (assumed). The rail goes from 40 bar at idle to 200 bar at full load (a Bosch HDEV5 generation). 0.38 Nm at 4000 rpm on a 2 L: combustion pays it like friction, and the spec curve subtracts it.

**The tick.** Each injector strikes twice per injection: the armature on its stop when opening (60 % of the force, assumed), the needle on its seat when closing, pushed by its spring and the rail, with an amplitude in √(p/200 bar). Injection starts 60° after intake top dead centre; it lasts `m/(ρ·Cd·A·√(2Δp/ρ))`, with an injector of 15 cm³/s at 100 bar for a 500 cm³ cylinder: 0.4 ms at idle, 2.5 ms at full load and maximum revs. The pump clicks at each stroke of its cam (as many lobes as cylinders up to four, three on a six, four beyond), timed to finish 30° before injection. Impacts of 0.1 ms (noise, never twice the same) on two modes of the head, 6.3 and 9.8 kHz, and a 0.3 ms half-sine on two modes of the rail, 1.6 and 3.4 kHz: without this softer contact, the pump spilled into the injector band. The bands come from SAE 2011-01-0930: countermeasures on a direct-injection engine removed 3 dB above 5 kHz (suspended injectors) and 5.8 to 7.5 dB(A) around 1.6 kHz (pump). The levels are calibrated on that, against the block floor at idle, in the engine bay: +5.8 to +7.0 dB in the 1.6 kHz third-octave, +2.5 to +3.6 dB above 5 kHz, on three presets. It is an engine-bay source (`stem(Source::Bay)`), with its own generator, cut with the fuel on overrun and at the limiter. It keeps its place: behind the car, under 0.1 dB anywhere; at 3000 rpm in the bay, under 1.4 dB; the 8-16 kHz over 500-2000 Hz ratio moves by −0.9 to +1.5 dB at idle in the bay. LAeq in the bay at idle, `hot_hatch_na`: 64.2 dB(A) with port injection, 67.1 with direct.

**The port-injection film.** Aquino's x-τ model (SAE 810494), shared with the carburettor (`WallFilm`). The deposited share `X = 0.55·(1 − E(T_eq))`: 55 % of the spray hits the valve and the port (assumed), minus what flashes off the wall. `E` is the distillation curve of an EN 228 petrol (E70 33 %, E100 56 %, E150 88 %, mid summer grades), read at the temperature the wall would need at atmospheric pressure for the same evaporation: `1/T_eq = 1/T_wall + ln(p/p₀)/B`, B = L_v·M/R = 3668 K (Clausius-Clapeyron, M = 0.1 kg/mol). The wall is at coolant plus 15 K (assumed). The film leaves by evaporating, `τ_ev = 0.25 s·exp(B(1/T − 1/T_ref))·p/p₀`, or dragged by the air, `τ_air = 2 s·(50/v)^0.75`: `1/τ = 1/τ_ev + 1/τ_air`. Warm at full load, X = 0.22 and τ = 0.23 s; at 20 °C, X = 0.53 and τ = 1.3 s; under vacuum (0.3 bar), X falls to 0.05. The ECU compensates with the inverse model, `injected = (wanted − estimated_film/τ̂)/(1 − X̂)`, with X̂ and τ̂ from its 90 °C calibration. At 90 °C it is exact on tip-in; on lift-off it cuts everything but cannot remove what evaporates from the wall. When cold, the mixture goes lean on tip-in (under 0.85 of the wanted richness) and rich on lift-off (over 1.1); torque follows `torqueOfRichness`, the carburettor's. `DrivingModel` reads the coolant temperature through `setCoolantC`, 90 °C until the thermal agent provides it: at 90 °C the film does not run, and the model stays bit-exact. The carburettor gets the same cold correction (`coldFilmShare`), nothing at 90 °C. Direct injection has no film.

**Diesel.** `mechanical` is the original diesel, bit-exact. Combustion is computed point by point (`dieselBurn`). Ignition delay from Hardenberg and Hase (1979, Heywood eq. 10.37): `τ(°CA) = (0.36 + 0.22·S̄p)·exp[E_A(1/(R̃T) − 1/17190) + (21.2/(p − 12.4))^0.63]`, `E_A = 618,840/(CN + 25)`, at top dead centre of a polytropic compression (n = 1.35, ε = 18), EN 590 diesel (cetane 51). The mechanical pump delivers through its cam, the same volume per degree at any speed; its pressure follows, from 150 to 850 bar (assumed). What is injected during the delay burns at once, as a 0.8 ms triangle (Heywood: 5 to 10°). The common rail delivers through an orifice, from 250 to 1600 bar with load and speed; a 1.5 mg pilot (for 500 cm³, 1 to 2 mg published) burns 12° earlier, and the main injection arrives in its flame: only the physical delay of a breaking jet remains, `t = 28.65·ρ_l·d/√(ρ_a·Δp)` (Hiroyasu). The rest burns by diffusion (Wiebe, m = 1), over the injection plus a mixing time of 30°·√(800 bar/p), in degrees because swirl follows engine speed (Heywood: 40 to 60° at any speed). The pressure rise is `(γ − 1)·(dQ/dθ)/V` at each peak (pilot, premix, diffusion), where it falls around the combustion centroid, set at 8° after top dead centre. At idle on the stock 2 L: 5.2 bar/° with the pump, 2.5 bar/° with common rail (5 to 10 and 2 to 5 bar/° published), i.e. 6.4 dB less. The knock (`knockEnv`) and Anderton's diesel block law take this difference, drawn from 81 load and speed points at each engine change. LAeq at idle: 78.0 → 73.3 dB(A) in the bay (−4.7), 45.3 → 42.5 behind (−2.8); PSA claims 3 dB for the pilot, Denso 6.5 dB at idle for its 1800 bar rail. At full load, the faster diffusion gives 2 to 4 dB more than the pump: a model result, not verified. Efficiency follows the degree of constant volume, `η(θc) + ½·η''(θc)·σ²`, σ² the spread of the combustion. Full-load torque follows the smoke limit, `λ_min = 1 + c/√p`, with λ = 1.43 at 600 bar: the excess air of the original full load (8.5 bar BMEP plus 2 bar friction at 46 % indicated efficiency).

| `diesel_four`, 2 L turbo | Torque | Power | 0-100 | WLTC | Best BSFC | Idle, bay |
|---|---|---|---|---|---|---|
| Mechanical pump | 315.1 Nm | 113.5 kW | 12.94 s | 7.58 L | 196 g/kWh | 78.0 dB(A) |
| Common rail | 364.6 Nm | 124.1 kW | 12.08 s | 7.54 L | 189 g/kWh | 73.3 dB(A) |
| Recirculation | 315.1 Nm | 113.5 kW | 12.94 s | 7.55 L | 196 g/kWh | |
| Particulate filter | 313.4 Nm | 112.4 kW | 13.04 s | 7.62 L | 196 g/kWh | |

Common rail gives +29 % torque at 1000 rpm, +22 % at 1500, +9 % at 4000: Denso claims +35 % torque and +24 % power over its previous system, PSA went from 196 Nm (1.9 TD) to 250 Nm (2.0 HDi 110). At part load, the pilot burns early: 1.5 to 3.6 % more specific consumption, at equal timing. The model does not credit the timing the ECU optimises, the main source of the real gain.

**Recirculation.** A share of exhaust gas in the charge, with load and speed: up to 15 % on a petrol engine (cooled external EGR), between 30 and 55 % load, none at idle nor at full load; up to 30 % on a diesel at light load, none beyond 75 %; none above 85 % of maximum revs (assumed, within published ranges). On a petrol engine, laminar flame speed follows Metghalchi and Keck (1982), `S_L ∝ 1 − 2.06·x^0.77` (0.52 at 15 %), combustion lengthens as `S_L^−1/3` (×1.24), and combustion noise drops accordingly (−1.9 dB). The diluted flame is cooler by `x·ΔT_ad`: wall losses drop following Woschni (`h ∝ T^−0.53`, 22 % of the energy to the walls at part load), and for the same fresh air the manifold is fuller by `1/(1 − x)`: less pumping. Specific consumption at 2000 rpm and 2 bar: −2.7 % (2 to 5 % published); WLTC of the city car: 6.76 → 6.56 L (−3 %). On a diesel, only the missing oxygen (`1 − x/λ`) cools the flame and slows the diffusion: consumption barely moves (−0.3 %), noise drops by 0.4 to 0.7 dB. A rotary has none: it keeps its burnt gas from one chamber to the next.

**Particulate filter.** On a diesel or a petrol direct-injection engine. A monolith of 1.25 times the displacement, 15 cm long, 0.8 m² of walls per litre. Back-pressure follows Darcy in the wall (0.3 mm, k = 10⁻¹²) and in the soot cake (100 kg/m³, k = 3·10⁻¹⁴, Konstandopoulos et al.), plus the channel entry: 3.7 kPa clean, 16 kPa at mean soot, 26 kPa full, at full load and 4000 rpm (2 to 5 and 15 to 30 kPa published). The piston pays it, `Δp·V/4π`: −1.7 Nm and −1.1 kW at the peak; combustion does not see it. Soot follows an emission index that rises towards the smoke limit (0.4 to 6 g/kg); a diesel ECU requests regeneration at 7 g/L and stops it at 1 g/L. Post-injection burns in the oxidation catalyst and brings the gas to 620 °C: `ṁ_fuel = ṁ_gas·c_p·ΔT/(LHV·0.95)`; soot burns per Arrhenius (150 kJ/mol). At 2000 rpm and 35 % load, a regeneration lasts 4 min 43 s and costs 190 g of diesel; that point fills the filter in about ten hours: around 0.5 % average extra fuel (1 to 3 % published). This fuel goes through `extraFuelGps`, like the anti-lag's: consumption reads it, nothing comes back. A petrol filter regenerates on its own, during fuel cut on a hot filter. The filter starts halfway between two regenerations, its average: a regeneration does not show in an ordinary trip. In the line, it sits after the catalyst: its channels lose by viscosity and conduction (Kirchhoff, 0.0675 Np/m/√Hz at 0.55 mm radius), and its walls are a series resistance, 0.20 ρc/S at mean soot. −2.9 dB from 100 to 500 Hz, −4.9 from 500 to 2000, −5.7 from 2 to 8 kHz. The original diesel already has 6 dB less at the exhaust (`kDieselFilter`), attributed to "a turbine and a filter": with this setting, part of it may be counted twice. To be settled by listening and integration; the default does not change.

**What I read, what I expose.** I read the coolant temperature (`setCoolantC`, `PortFilmInput::coolantC`, `CarburetorInput::coolantC`, 90 °C by default), the fuel (`InjectedFuel`, `DieselFuel`, RON 95 and EN 590 by default), the intake air (`intakeAirK`) and the charge pressure (`DieselPoint::intakeBar`). I expose `chargeCooling` (cooling at intake closing and at top dead centre, for knock), `vaporizationDropK`, `directFillGain`, `dieselBurn` (delay, premix, pressure rise, efficiency), `smokeLimitLambda`, `egrRate`, `egrEfficiencyShare`, `exhaustStateOf` (gas flow, temperature and richness) and `ParticulateFilter` (soot, back-pressure, regeneration).

**Simplifications and limits.** No stratified direct injection nor split injection; no cold film on the piston of a direct-injection engine; no NOx, no soot on screen, no oil diluted by post-injection. The common rail has neither multiple pilots nor rail-pressure control; its injectors' tick is not simulated, its knock covers it. The mechanical pump does not distinguish in-line, distributor or unit injector. The filter does not change the pressure at the turbine outlet. The on-screen gas and views show neither injector, nor filter, nor recirculation. Sources are cited from memory: Heywood (1988, chapters 7, 9 and 10), Zhao, Lai and Harrington (*Prog. Energy Combust. Sci.*, 1999), Anderson et al. (SAE 962018), Wyszynski et al. (SAE 2002-01-0839), Aquino (SAE 810494), Hardenberg and Hase (SAE 790493), Hiroyasu and Arai (1990), Metghalchi and Keck (*Combust. Flame*, 1982), Konstandopoulos et al. (SAE 2000-01-1016), SAE 2011-01-0930; the PSA and Denso figures come from their communication. To be cross-checked before citing them elsewhere. Listening remains to be done.

## The cam

The cam profile, variable valve timing and what they do to the filling, the idle and the sound: `core/src/cam.cpp`, copied for the drawing and the on-screen gas in `web/src/lib/cam.ts`. The stock cam goes through none of these calculations (`camIsStock`): the 29 fingerprints did not move. A rotary and a two-stroke have no cam, and its settings are ignored there.

**The lift law.** L(θ) = Lmax · g(x), with x = (θ − centreline)/half seat-to-seat duration and g(x) = (1 − x²)²(1 − x⁴) = 1 − 2x² + 2x⁶ − x⁸. It is the 3-4-5-6 polynomial of a single-dwell cam, (1 − x²)³ (Norton, *Cam Design and Manufacturing Handbook*, from memory), filled out by (1 + x²): lift, velocity and acceleration are zero at the seat, hence continuous with the base circle. This filling gives a duration at 1.27 mm over duration at 0.15 mm of 0.84 to 0.86, like American catalogues (0.83 to 0.84, from memory), and a nose deceleration of 64 % of the peak opening acceleration. g″(0) = −4, hence the nose deceleration `camNoseDecelerationMs2` = 4·L·ω²/half-duration², with the square of the speed: 313 g at 7000 rpm on the stock cam of a 2 L. A 0.25 mm lash (assumed) is taken up before the valve leaves its seat.

**The profile.** The settings read like a European cam card, at 1 mm of lift: duration, valve lift, lobe separation. The intake centreline is the lobe separation after top dead centre (cam ground "straight up"), the exhaust as much before. The stock cam, 240° at 1 mm, 110° of lobe separation, lift 0.12 times the bore (that of `valvetrain.cpp`), opens the exhaust at 130°, closes it at 370°, opens the intake at 350° and closes it at 590°: exactly the sound's `kEvo`…, which keep their value. Seat to seat, it lasts 308°. A lift too small for a duration too long cannot hold: the lobe, seat to seat, is capped at 420° so that it never covers firing top dead centre, and the 1 mm duration is read from what remains (260° at most with 5 mm of lift). Below the 0.25 mm lash, the valve does not leave its seat.

**The filling, calculated.** A zero-dimensional cylinder (mass, energy, burnt share), over three cycles, in half-degree steps below 3000 rpm and one-degree steps above:

- the valves as compressible orifices, effective area Cd·(1/A_curtain² + 1/A_throat²)^(−1/2), Cd = 0.7, curtain π·d·L, throat 0.636·d², the diameters of `valveTrainOf`;
- the intake: a cast port of 0.15 times the unit displacement, then an air column of the throats' area, with inertia, ρ·L·du/dt = p_plenum − p_port − ½ρK·u|u| − ζρc·u, K = 1 and ζ = 0.15 (assumed); what flows back against the valve stays there as a slug and returns first;
- the exhaust: 0.3 times the unit displacement, then a 0.62 m primary, ζ = 0.25, against a back pressure of 8 kPa at rated speed, with the square of the speed (assumed, stock line);
- combustion, all at once at top dead centre: 0.75 × 2.9 MJ per kg of trapped air, i.e. 5 bar at exhaust opening, as in `tools/valve_filling.py`.

The runner is tuned for the stock cam: its length comes from the Helmholtz resonance with the cylinder at mid-stroke, f = c/2π·√(A/(L·V)), placed at 2.5 times the rotation frequency at the base model's torque peak (Engelman, from memory: 2 to 3). Nothing in it is adjusted to the cam. The cycle's work, ∮p·dV, gives the net indicated mean pressure, which counts the trapped air, the expansion lost by an early opening and the pumping. `breathing` receives its ratio to the stock cam, friction kept apart, as for the carburettor: ((bmep + fmep)·r − fmep)/bmep. Fourteen speeds, from 600 rpm to 1.25 times the redline, each starting from the state the previous one left; an engine costs 10 to 80 ms natively, 40 to 130 ms in WebAssembly (measured on a loaded machine), and four are kept in memory. The turbo, the supercharger and the gearbox stay those of the stock cam, as for the filter.

**Intake closing places the best filling.** Closed late, the valve lets back out what came in when the piston rises slowly, and keeps what the column still pushes when it rises fast. **The effective compression** is read at the same point: ε_eff = 1 + (ε − 1)·V(IVC)/V_d, with a rod of 3.2 crank radii: 9.25 on the stock cam at 10.5, 8.3 on a race cam (closing 66° after bottom dead centre). **The overlap** lets burnt gas back in, at idle above all.

| 2 L, 86 × 86, 6800 rpm, 1200 kg compact | Peak torque | Power | 0 to 100 km/h | WLTC (`hot_hatch_na`) | Idle: manifold, residuals, COV |
|---|---|---|---|---|---|
| Stock, 240°/10.3 mm/110° | 173.4 Nm at 4100 | 96.8 kW at 6250 | 9.53 s | 9.07 L | 38.9 kPa, 15 %, 3.0 % |
| Fast road, 265°/11/110° | 162.8 Nm at 4850 | 95.5 kW at 6550 | 10.10 s | 8.96 L | 44.5 kPa, 21 %, 5.2 % |
| Race, 280°/12/106° | 150.8 Nm at 4850 | 91.7 kW at 6800 | 10.60 s | 8.85 L | 49.4 kPa, 31 %, 20 % |
| Stock, limiter at 8000 | 193.1 Nm at 4800 | 126.7 kW at 7350 | 8.14 s | | |
| Race, limiter at 8000 | 172.1 Nm at 5650 | 129.3 kW at 8000 | 9.05 s | | |
| Phaser | 183.0 Nm at 3650 | 97.2 kW at 6300 | 9.37 s | 9.12 L | as stock |
| Two profiles, switch at 5200 | 184.0 Nm at 3600 | 96.8 kW at 6800 | 9.52 s | 9.14 L | 34.7 kPa, 14 %, 2.7 % |
| Phaser and two profiles | 190.1 Nm at 3600 | 97.3 kW at 6800 | 9.47 s | 9.17 L | 34.7 kPa, 14 %, 2.7 % |

A race cam gives nothing without revs: at 6800 rpm it loses 13 % of torque and 5 % of power; with a limiter at 8000, it gains 2 % of power and moves the torque peak by 850 rpm. That is the known direction (a "fast road" cam moves the peak by 500 to 1000 rpm and costs low-end, according to tuners' catalogues, from memory), without having been fitted to it. **No published curves of one engine, stock cam against race cam, could be cross-checked**: the figures above are those of the calculation.

**Residual gas and idle.** The residual gas fraction comes from the correlation of Fox, Cheng and Heywood (SAE 931025, from memory): x = 1.266·(OF/N)·(p_i/p_e)^−0.87·√|p_e − p_i| + 0.632·(φ/ε)·(p_i/p_e)^−0.74, pressures in kPa, with the overlap factor OF = (1.45/B)(107 + 7.8Δθ + Δθ²)(L·ΣD/B²), in mm and degrees at 1 mm, ΣD the sum of the intake valve diameters (an assumed reading for four valves). The idle pressure is solved: the calculated cycle, at half its heat (the spark is retarded to keep a torque reserve, assumed), must supply the engine's friction and 0.3 bar of accessories. The stock cam holds 39 kPa, i.e. 18 inHg of vacuum (17 to 21 in the workshop, from memory); the race cam 49 kPa, 15 inHg, where workshops quote 8 to 12 inHg for the wildest cams: **the direction is right, the gap smaller than in reality**.

**Combustion spread.** Burnt gas slows the laminar flame: S_L = S_L0·(1 − 2.06·x^0.77) (Metghalchi and Keck, 1982). The spread of the indicated mean pressure follows its inverse: COV = 1.04 %·(S_L0/S_L)^1.62. Both constants are set on two orders of magnitude, 3 % at a stock idle and 10 % (the driveability limit) near 27 % burnt gas (Heywood, chapter 9, from memory); beyond, misfires. A diesel ignites by itself: none of this applies to it.

**The sound.** The cam changes the timing of the puffs (the sound's `evo`…, the stock ones as long as the cam is), their duration (τ = V/(Cd·A·a), with the mean area of the first 60 degrees), and the pressure at opening, read from the calculated cycle speed by speed. The idle lopes: at each firing, the pressure is drawn from a normal law (sum of three uniform draws) with the spread the cam adds to the stock one, √(COV² − COV₀²), the residuals taken at the current filling; below 5 % of the expected pressure, it is a misfire. A weaker firing kicks the crankshaft less, in the order of its idle jolt, and the governor erases it in 0.25 s: the speed wanders. All of it draws from its own generator, and shifts no draw of the rest. Rendered at standstill, `hot_hatch_na` at idle: puff crests spread by 26 % on the stock cam (the sound's own spread, not a measurement), 28 % on 265°/11 mm, 34 % on 280°/12/106°, and the interval between puffs goes from ± 0.8 ms to ± 0.9 ms. On 300°/13/104°, a cam nobody would leave idling at 775 rpm, the calculated spread exceeds 50 %: one firing in two or three is missing, and the speed wanders by ± 5 ms per interval.

**Variable valve timing.** The intake phaser has ± 20 crank degrees around the base timing (40° in all, of the order of a VVT-i, from memory). Its best full-load timing comes from the same cycle: three timings, then the top of the parabola through them. It advances the intake low down (+ 16 to + 20° below 4000 rpm) and retards it up high (− 9° at 7000, − 18° at 8000). Off throttle and at idle, it stays against the retard stop, the least overlap possible; it turns at 150°/s (assumed). A two-step cam brackets the chosen profile: 25° and 15 % of lift less for the mild one, as much more for the wild one (assumed, of the order of a VTEC). The switch happens where their torque curves cross, calculated, with ± 2 % of hysteresis, and only above 60 % filling (assumed). The data-sheet torque is the better of the two; within the hysteresis band they differ by less than 1 %, and the driving model does not count it. At the switch, each cylinder locks its rocker at its next intake closing, back on the base circle: a 4.5 kHz tick, with half the energy of the seat's (a 5 g pin at 1 m/s against a 40 g valve at 0.5 m/s, assumed), from its own generator.

**What the cam exposes to other topics**, as pure functions of `cam.hpp`: `intakeClosingAbdcDeg`, `effectiveCompressionRatio`, `valveOverlapDeg`, `overlapFactor` and `residualGasFraction` (which take the pressures as parameters), `combustionCov`, `camDurationAtLiftDeg`, `camProfileOf` (effective lift and duration), `camNoseDecelerationMs2`. It reads the valve float speed as a parameter of `camSwitchRpm`: a second profile that would only help beyond it does not switch. It also reads `compressionRatio`.

**What is not modelled.** The primary is not tuned to the cam: scavenging by the exhaust wave stays that of a damped pipe. The cycle has a single γ and no heat transfer. Consumption only sees the torque: a race cam gains 2.4 % there, because its lower peak torque makes the engine work closer to full load, whereas in reality its residuals would rather make it lose. The on-screen gas and the drawing follow the base profile, without the phaser or the switch live, and the gas does not follow the lift. Valve float has no consequence here: it belongs to the pushrods topic. **Nothing has been listened to**: to do, `revforge-render --out idle.wav --preset hot_hatch_na --scenario idle --seconds 8 --cam-duration 300 --cam-lift 13 --lsa 104`, the same without the cam options, and `--vvt two_step --scenario launch --listener bay` for the switch.

## Pushrods and valve float

`Camshaft::Ohv` no longer passes for a single cam: one camshaft in the block for the whole engine, hydraulic lifters, pushrods, 1.6 rocker arms and two valves in line under a wedge head. Valve float applies to every valvetrain. Everything lives in `core/src/valve_float.cpp` (pure functions) and `core/src/valvetrain_sound.cpp` (the two noises, each with its own generator). `revforge-render --valves 1` prints the valvetrain of a setting.

**The parts, seen from the valve.** The spring sees the equivalent mass m = m_valve + m_retainer + m_spring/3 + m_bucket + I_rocker/L² + (m_lifter + m_pushrod)/R², R being the rocker ratio: what sits on the cam side moves R times slower. A valve weighs like a solid head 6.5 mm thick (44 g at 33 mm, 103 g at 50.8 mm, the order of real valves, from memory); retainer 18 %, spring 90 % (a third moves), bucket 70 %, single-cam rocker 80 %, stamped rocker 60 %, lifter 85 % of the valve (assumed from common parts). An arm pivoting between its ends gives I/L² = m (R² − R + 1)/(3R²). The pushrod is 2.1 strokes long (183 mm on a small block, 188 on an LS), a tube 0.155 times the head (6 mm at least) bored to 60 %. Stiffnesses add in series, seen from the valve: stem E·A/L, cam and bucket 30 kN/mm, rocker arm 12 kN/mm, the lifter's oil column 40 kN/mm and the pushrod E·A/L, these last two multiplied by R² (assumed orders of magnitude). Hence f_n = √(k/m)/2π:

| 2 L, 86 × 86 | Moving mass | Natural frequency | Spring seated, open |
|---|---:|---:|---:|
| Twin cam | 94 g | 2.2 kHz | 231 N, 579 N |
| Single cam | 117 g | 1.15 kHz | 287 N, 717 N |
| Pushrods | 147 g | 800 Hz | 361 N, 903 N |
| LS1, 99 × 92, 6000 rpm | 203 g | 716 Hz | 489 N, 1222 N (110 and 275 lb) |
| Small block 350, 101.6 × 88.4, 5500 rpm | 215 g | 708 Hz | 445 N, 1113 N (100 and 250 lb) |

Published springs (from memory): 80 and 200 lb on a stock small block, 76 to 85 and 220 to 240 lb on an LS1, 105 and 280 lb on an LS6. The model sits 20 to 25 % above the small block, between the two LS. Published natural frequencies are 2 to 3 kHz for direct acting and 500 to 900 Hz for pushrods.

**Nose deceleration.** The lift law accelerates as a half sine on each flank, then decelerates as a half sine over the middle 60 % of the event (assumed: a road cam decelerates over half to two thirds of its event, less hard than it accelerates). Lift is L = v_max·T·s with s = (1 − f)/4 + f/π, f = 0.6, and the nose deceleration a = π·v_max/(f·T), so a = π·L·ω²/(f·s·D²) ≈ 18·L·ω²/D², D being the duration in crank radians. It follows the square of engine speed. The stock cam lifts 0.12 bore (`valvetrain.cpp`) over 240° (the sound, `kEvo` to `kEvc`): 613 g at the nose at 7200 rpm on an 86 mm bore, the order of road cams.

**The stock spring.** The maker sizes it kinematically for the stock cam: at full lift, F = 1.25·m·a(redline), and the seated force is 2.5 times less (80 and 200 lb, 105 and 280 lb). The valvetrain's compliance is not in it: that is what separates the valvetrains. A higher redline means a stronger spring: the preset describes the engine as its maker revved it.

**Valve float speed.** The valve leaves the cam when inertia, amplified by the valvetrain, exceeds the spring at the nose. To first order, the deceleration seen by the valve is a/(1 − (f_d/f_n)²), f_d = 1/(2·f·T) being the frequency of the deceleration half sine. Hence m·α·N²/(1 − β·N²) = F, so N² = F/(m·α + β·F), with a = α·N² and (f_d/f_n)² = β·N². With the stock spring, N_f = N_r·√(1.25/(1 + 1.25·β·N_r²)): the maker's margin, minus what the valvetrain takes from it. `valveFloatRpm(const EngineConfig&)` gives it; the rev limiter and the cam will read it.

| Preset, stock springs | Redline | Valve float | Margin |
|---|---:|---:|---:|
| `city_14` | 6200 | 6921 | +11.6 % |
| `four_cyl_sport` | 7200 | 8027 | +11.5 % |
| `three_cyl_turbo` | 6000 | 6698 | +11.6 % |
| `hot_hatch_turbo` | 6500 | 7252 | +11.6 % |
| `hot_hatch_na` | 7300 | 8140 | +11.5 % |
| `five_cyl_turbo` | 7000 | 7807 | +11.5 % |
| `inline_six` | 8000 | 8913 | +11.4 % |
| `v6_gt` | 6500 | 7251 | +11.5 % |
| `flat_six` | 7800 | 8682 | +11.3 % |
| `boxer_four_turbo` | 6800 | 7578 | +11.4 % |
| `v8_cross_plane` | 6000 | 6692 | +11.5 % |
| `v10_open` | 8500 | 9467 | +11.4 % |
| `v12` | 8900 | 9901 | +11.2 % |
| `diesel_four` | 4500 | 5026 | +11.7 % |
| `retro_coupe` (single cam) | 6400 | 7099 | +10.9 % |
| `v8_supercharged` | 6400 | 7133 | +11.5 % |
| `carb_twin_cam` | 6400 | 7140 | +11.6 % |
| `twin_rotor_13`, `electric` | — | no valves | — |

All stay 11 to 12 % above their redline, where the limiter cuts: nothing changes by default, and the 29 fingerprints held. That is the order of known float speeds, 5 to 15 % above the limiter; the LS1 comes out at 6584 rpm for 6000, its stock springs being quoted good to about 6300 to 6500. On pushrods, the valvetrain takes one or two more points of the margin (+7.9 to +10.9 % on the same engines): they can rev high, but with much stronger springs, which rub.

A sharper cam on its stock springs lowers valve float: on `hot_hatch_na`, 12.5 mm of lift over 200° bring it down to 6490 rpm, below the redline; stiff springs raise it back to 7394. The adjustable cam reads these figures through `camLobeOf` (`camLiftMm`, `camDurationDeg`, 0 for stock); the spring stays the one sized for the stock cam.

**Stiff springs.** 30 % more force, seated as open (24 to 50 % on common aftermarket springs): valve float rises by about √1.3, 13 to 14 %. Cam follower friction follows the load, so 30 % more of it: +0.03 bar at 6000 rpm on a 2 L with an 86 mm stroke, 1 to 4 % of total friction, more at idle where the cam weighs most. Like the rest of the valvetrain friction, it is taken from brake torque in `breathing` (against 12 bar of indicated mean pressure, like `sohcBreathingRatio`) and added to `frictionTorqueNm`, which driving and fuel read.

**Beyond valve float.** The force needed exceeds the spring by a ratio ρ = (N/N_f)²·(1 − β·N_f²)/(1 − β·N²). The valve now decelerates at only 1/ρ of what the cam asks: it stays in the air ρ times longer, and the intake closes late by Δθ = f·D·(ρ − 1), capped by the closing flank ((1 − f)·D/2, 48° stock). The piston rises meanwhile and pushes the charge back: the cylinder keeps V(closing + Δθ)/V(closing), with a rod of 1.6 strokes and the stock compression ratio. The valve lands at v_flank·(1 − 1/ρ), v_flank = L·ω/(s·D), instead of the closing ramp speed (0.013 mm per degree, assumed), and bounces: each bounce keeps e = 0.5 of the speed (assumed) and lasts 2·e·m·v/F₀. Bouncing is chaotic: each cycle keeps its charge within ± the mean loss. On `hot_hatch_na`, 10 % beyond: 30° late, 79 % of the charge kept, landing at 1.3 m/s for 0.7 m/s on the ramp. The torque curve dives (`valveFloatShare`, exactly 1 below), and the sound follows.

**Pushrod friction and inertia.** Twin-cam follower friction follows Patton, Nitschke and Heywood (C = 133, see "The single cam"); the single cam has 50 % more. Since the spring is sized on the moving mass, and the cam sees it multiplied by the ratio, pushrods have (m·R)_pushrods/(m·R)_single cam times the single cam's: 2.2 times the twin cam on the 2 L, 0.144 bar at the redline against 0.067 and 0.100. That is 3 to 10 % of total friction, most at idle. One camshaft in the block for the whole engine instead of two per bank: −0.1 to −0.3 % inertia. Lifters and pushrods reciprocate: their energy, referred to the crank, weighs less than 10⁻⁵ kg·m², neglected.

**Pushrod breathing.** Two valves in line along the crankshaft are bounded by the same edge and the same bridge as two valves across the bore: the same section as the single cam, so the same high-rpm drop, the same lower tuning, the same longer blowdown. What really sets a pushrod head apart is not modelled: ports pinched by the pushrods, the wall shrouding in-line valves, the hemispherical head of a Hemi.

| Engine | Valvetrain | Peak torque | Peak power | 0–100 km/h | WLTC | Valve float |
|---|---|---|---|---|---|---|
| 2 L, 86 × 86, 6500 rpm, 1200 kg compact | twin | 168.5 Nm at 3900 | 89.9 kW at 5950 | 9.95 s | 8.40 L | 7251 |
| | single | 166.9 Nm at 3050 | 79.7 kW at 5700 | 10.38 s | 8.48 L | 7205 |
| | pushrods | 166.2 Nm at 3050 | 79.5 kW at 5700 | 10.40 s | 8.58 L | 7140 |
| | pushrods, stiff springs | 165.6 Nm at 3100 | 79.2 kW at 5700 | 10.44 s | 8.69 L | 8099 |
| `hot_hatch_na` | twin | 194.1 Nm at 4500 | 117.8 kW at 6750 | 8.72 s | 9.07 L | 8140 |
| | pushrods | 188.2 Nm at 3400 | 95.8 kW at 6100 | 9.44 s | 9.16 L | 7985 |
| | pushrods, stiff springs | 187.5 Nm at 3400 | 95.5 kW at 6100 | 9.46 s | 9.25 L | 9047 |
| `v8_cross_plane` | twin | 428.9 Nm at 3700 | 214.0 kW at 5550 | 6.95 s | 13.90 L | 6692 |
| | single | 424.0 Nm at 3000 | 194.5 kW at 5400 | 6.78 s | 14.13 L | 6646 |
| | pushrods | 422.3 Nm at 3000 | 193.8 kW at 5400 | 6.78 s | 14.35 L | 6592 |
| | pushrods, stiff springs | 420.7 Nm at 3000 | 193.1 kW at 5400 | 6.78 s | 14.56 L | 7478 |

Against the single cam, pushrods only lose their friction: −0.4 % torque, +1 to +1.6 % on the WLTC. Against the twin cam, the two-valve section costs most. A real pair: the 1992 Corvette's LT1 pushrod V8 (101.6 × 88.4, limiter near 5700), 224 kW at 5000 and 461 Nm at 3600; the ZR-1's twin-cam LT5 (99 × 93, 7000), 280 kW at 5800, later 302 kW (figures from memory). The model gives 208 kW at 5200 and 472 Nm at 2850 for the first, 315 kW at 6450 and 542 Nm at 4350 for the second: a ratio of 1.52 instead of 1.25 to 1.35. The direction is right, the gap too large: the LT5's higher redline also raises its mean pressure (`naturalBmep`), and the LT1 had its own ports and cam. Nothing was tuned to match these figures. Two more pushrod engines, set up the same way (from memory): the Ford Windsor 302 HO (101.6 × 76.2), 168 kW at 4200 and 407 Nm at 3200, which the model gives as 173 kW at 5300 and 386 Nm at 2850 (assumed 5800 redline), valves floating at 6376 rpm; the 1275 cc BMC A-series of the Mini Cooper S (70.6 × 81.3), 57 kW at 5800 and 107 Nm at 3000, which the model gives as 51 kW at 5700 and 106 Nm at 3100 (6500 redline), 97 g moving at each valve, a valvetrain ringing at 964 Hz, valves floating at 7179 rpm.

**Lifter noise.** At each opening, the lifter body, pushed by the cam on its ramp, takes up the plunger's leak-down travel and strikes everything above it: pushrod, rocker and valve, seen from the lifter multiplied by R². The impulse is μ·v, μ being the reduced mass of both sides and v the ramp speed divided by the ratio. Against the single cam's rocker taking up its lash on the stem (`kRockerShare`, the only assumed level in this chain), it is 1.79 times stronger on the V8: +5 dB. The blow rings the pushrod in bending, at the second mode of a pinned beam, 4·(π/2L²)·√(EI/ρA): 2.1 kHz on the V8, 1.9 to 2.9 kHz on published engines. It radiates through the valve cover, like the rocker, from its own generator. Measured against the single cam at idle (800 rpm): relief of the blow's band in 1 ms windows +2.6 dB under the hood, −0.04 dB outside, and the 8–16 kHz band against 500–2000 Hz does not move outside (−66.91 dB against −66.91). The blow does not get in front of the exhaust. A mechanical take-up (solid lifters with lash) would strike harder; the model assumes hydraulic lifters, whose oil film is not modelled otherwise.

**The sound of valve float.** Less charge means weaker blowdowns; chaotic bouncing, uneven cycles; a valve landing fast, a seat that clatters. The clatter follows the landing speed (impulse m·v), against the seat's stock tick, set at idle where the valve lands at that speed's ramp velocity, from its own generator; it finishes its bounces when revs drop. Measured on `hot_hatch_na` against the same engine whose valves would never float: 15 % beyond (9361 rpm), −2.4 dB(A) outside and a cycle-to-cycle energy spread of 0.33 instead of 0.18; 25 % beyond, +1.7 dB under the hood between 6 and 12 kHz. Below valve float, the sound is identical to the bit (test). When driving, the limiter stops the engine before stock valve float: it is only heard with a sharper cam, or a downshift that over-revs the engine.

**What other subjects read and what this exposes.** Exposed: `valveFloatRpm(const EngineConfig&)` (0 without valves), and for a given setting `valvetrainParts`, `valveSpringOf`, `noseDecelerationMs2`, `valvetrainProfileOf`, `floatOverload`, `floatTrappedShare`, `valveFloatShare`. Read as parameters: `camLiftMm` and `camDurationDeg` (`camLobeOf`, 0 for stock), from which intake closing follows symmetrically about the cam axis (50° after bottom dead centre stock); `compressionRatio` (0: the starter's rule). A rotary and a two-stroke have no valves (`hasValves`): none of this applies to them.

**What is assumed, and what is missing.** A one-degree-of-freedom valvetrain, amplified to first order: no pushrod bounce, no hydraulic lifter pump-up when the valve floats (it would hold the valve open), no camshaft bending. The 1.25 margin, the lift law, the part shares and the stiffnesses are assumed, set from a few parts known from memory. The lighter exhaust valve is not computed separately: the intake floats first. Residual gas from a late exhaust closing is not counted. The view always draws a wedge head with in-line valves.

**On screen.** `engineLayout` places the cam in the block from the layout: at the bottom of the V, above the counterweights, or on the intake flank of an inline engine (`blockCamPosition`). The rockers reuse the single cam's lever, flipped: the stem leans towards the exhaust, the pushrod rises on the intake side, and the lifter axis aims at the pad at mid-lift. The lifter rises by the lift divided by the ratio (`lifterTop`), the pad follows the rocker's exact angle (`rockerPad`), and the pushrod joins them: its length varies by less than 3 % of the bore over a cycle (`culbuteurs.spec.ts`). The on-screen gas keeps the two-valve section (`twoValves`, `valves.ts`). The valvetrain panel shows the valve float speed, the moving mass and the springs computed by the C++ (`rf_valve_sheet`).

**Listening still to do.** Lifter tick: `revforge-render --out idle_pushrods.wav --preset v8_cross_plane --cam ohv --scenario steady --rpm 800 --listener bay`, against `--cam sohc` and `--cam dohc`, then outside (`--listener outside`). Valve float: `revforge-render --out float.wav --preset hot_hatch_na --cam-lift 12.5 --cam-duration 200 --scenario lap`, against `--springs stiff`, and `--scenario steady --rpm 9000 --listener bay` against 7500.

## Boost control

Three ways to hold a turbo's pressure (`boostControl`, `--boost-control`), in `core/src/boost_control.cpp`. `TurboCharger` only gains one input, `TurboInput::gateDemand`: negative, it is the spring, and nothing changes, bit for bit. On a supercharger the setting has no meaning: it has no wastegate, its pressure follows the crankshaft. It is ignored there bit for bit, and the panel only offers it on a turbo.

### The spring (`spring`), the original model

A diaphragm, pushed by the charge pressure above the outside air, opens the wastegate against a spring. In `TurboCharger::step`:

  g* = clamp((p + τa·dp/dt − p₀ − (1 − 0.2·b)·Δp) / (b·Δp), 0, 1),  then  dg/dt = (g* − g)/τg

with Δp the preset's boost, b = 0.12 the spring's band (its stiffness over the diaphragm), τa = 0.12 s an anticipation, τg = 0.05 s the rod's lag. The preload cracks it open at 97.6 % of Δp, and it is wide open 12 % higher. `springGateDemand` redoes this as a pure function, checked bit for bit against the turbo's step.

**Creep at high revs.** At equilibrium, p − p₀ = Δp·(0.976 + 0.12·g): the faster the engine turns, the more gas there is to dump, the wider the wastegate must open, and the more pressure the diaphragm must see. Full throttle at a held speed, deviation from Δp:

| | 2500 rpm | 3500 | 4500 | 5500 | 6500 |
|---|---:|---:|---:|---:|---:|
| `hot_hatch_turbo` (1.3 bar) | −0.9 % | +0.7 % | +1.5 % | +2.0 % | +2.1 % |
| `three_cyl_turbo` (1.2 bar) | +0.7 % | +2.2 % | +3.0 % | +3.3 % | +3.4 % (6000) |
| `five_cyl_turbo` (1.5 bar) | −1.3 % | +0.4 % | +1.3 % | +1.8 % | +2.1 % (7000) |
| `diesel_four` (1.8 bar) | −1.4 % | −0.7 % | −0.5 % (4500) | | |

The other creep, that of a wastegate too small that saturates, does not occur on the presets: it never opens beyond 0.49 (`wastegateK` is twice the turbine).

**Overshoot.** There is almost none: under 0.1 % on a tip-in at a held speed, thanks to the 0.12 s anticipation, which a purely pneumatic diaphragm does not have. Measured without it (a copy of `turbo.cpp` outside the repository): 2 to 19 % overshoot, then an oscillation that does not die out within 8 s, shaft and valve bouncing off each other. Removing the anticipation from the default to get this overshoot would change every turbo fingerprint, and leave the pressure oscillating: the spring stays as it is.

### Electronic (`electronic`)

This is an ECU's chain: absolute pressure sensor, control task, pulsed solenoid valve, diaphragm. As on vacuum-actuated wastegates (BMW N54 and N55, from memory), the diaphragm is held open by its spring and closed by the vacuum the solenoid valve sets: a failure opens the wastegate.

- **The target.** An *absolute* pressure: p_c = p_ref + Δp, p_ref = 101,325 Pa, the atmosphere of the calibration. It falls with rpm where the compressor would exceed the speed its wheels can take: `turboSteady` gives, at each rpm, the pressure ratio where the 520 m/s limit bites, and the target is brought back to it (r_max·p_ambient). On the presets, at sea level, this limit does not bite: the target is flat.
- **The solenoid valve.** 25 Hz, 5 ms to open, 3 ms to close (assumed orders of magnitude). The duty cycle is latched at the start of each period. Effective duty: 0 if the pulse is shorter than the opening time (duty < 12.5 %), 1 if the pause is shorter than the closing time (> 92.5 %), otherwise D + (t_c − t_o)·f. The diaphragm's vacuum follows within 40 ms (its hose, assumed), the opening is 1 − vacuum, then the same 50 ms lag as the spring.
- **The PID.** A 10 ms task, ideal form: u = u_ff + Kc·e + I + D, bounded to [0, 1]. The derivative acts on the measurement, not the error (no kick when the target jumps), filtered at τD/10. The feedforward u_ff is the duty that holds `turboSteady`'s equilibrium opening at this rpm, interpolated between the target and half the boost, in pressure ratio.
- **Anti-windup.** Conditional integration (Åström and Hägglund, *Advanced PID Control*, 2006): the integral stops when the valve is at its stop and the error pushes it there, and when the pressure is reaching the target on its own before τI (|dp/dt|·τI > |e|). Well below the target, when feedforward and proportional action saturate on their own, the loop is open: the integral restarts from zero, like an ECU leaving closed-loop control. Without this, after part-throttle climbs, the fourth-gear tip-in kept the earlier integral: a +27 % crest instead of +8 % on `three_cyl_turbo`.

**Tuning the gains: a bench, then SIMC.** At configuration, a `TurboCharger` is held at full throttle at 40, 55, 70 and 88 % of the redline, wastegate set to its equilibrium opening, then closed by one point. The pressure's response gives a first-order-plus-dead-time model, δp = K·e^(−θs)/(τs + 1)·δu, by Smith's two-point method (28.3 % and 63.2 %: τ = 1.5·(t₆₃ − t₂₈), θ = t₆₃ − τ); the final value is extrapolated by Aitken's Δ². The effective delay adds half a task period and half a PWM period. Skogestad's SIMC rule (*Journal of Process Control* 13, 2003) with τc = θ gives Kc = τ/(2Kθ), τI = min(τ, 8θ), and τD = 40 ms cancels the diaphragm's hose. The gains are interpolated in rpm, like an ECU's tables. Measured by the test, on the complete linearised loop (delays, hose, filtered derivative):

| | K (bar per unit opening) | τ | θ | Kc (per bar) | τI | Phase margin | Gain margin |
|---|---:|---:|---:|---:|---:|---:|---:|
| `hot_hatch_turbo`, 2600 rpm | 20.2 | 1.08 s | 0.076 s | 0.28 | 0.84 s | 57° | 3.0 |
| `hot_hatch_turbo`, 5720 rpm | 7.6 | 0.65 s | 0.098 s | 0.37 | 0.69 s | 61° | > 100 |
| `five_cyl_turbo`, 2800 rpm | 31.1 | 1.27 s | 0.071 s | 0.22 | 0.81 s | 55° | 3.0 |
| `diesel_four`, 3150 rpm | 14.6 | 0.68 s | 0.10 s | 0.20 | 0.72 s | 61° | > 100 |

The bench gain matches, within 25 %, the one given by two neighbouring `turboSteady` equilibria.

**Against the spring**, full-throttle tip-in after 8 s at half load, held rpm (`tests/boost_control_test.cpp`):

| | Steady-state error | 90 % of the pressure | Overshoot |
|---|---|---|---|
| Spring, `hot_hatch_turbo` 3500 / 5980 rpm | +0.8 % / +2.1 % | 0.68 s / 0.51 s | < 0.1 % |
| Electronic, same | −0.03 % / −0.007 % | 0.62 s / 0.45 s | 3.9 % / 1.9 % |
| Spring, `three_cyl_turbo` 3300 / 5520 rpm | +2.0 % / +3.3 % | 0.48 s / 0.43 s | < 0.1 % |
| Electronic, same | −0.01 % / −0.002 % | 0.40 s / 0.34 s | 2.7 % / 5.3 % |

No more creep, a 6 to 21 % shorter rise (the solenoid valve keeps the wastegate shut until the end, where the spring starts opening at 97.6 %), but an overshoot: 1 to 6 % above 3000 rpm, 9 % at 2500 rpm. In the car, in fourth from 1800 rpm (`--scenario turbo`), rpm rising: a +5.9 % crest on `hot_hatch_turbo`, +8.4 % on `three_cyl_turbo`, against +1.8 and +2.5 % for the spring, which are its creep. Without anti-windup: +137 % at 2500 rpm, +102 % at 3500, and still 1 to 3 % too high after 12 s.

**Dead zones.** When the wastegate has almost nothing to dump, at the bottom of the range where the target becomes reachable, the wanted opening falls between 0 and 12.5 %, which the valve cannot hold: it alternates between both ends, and the pressure oscillates by ±2 % around 2.7 Hz (`five_cyl_turbo` and `diesel_four` around 2500 rpm). This is the limit of a pulsed solenoid valve; a lower frequency or a faster valve reduces it.

**Altitude.** The spring's diaphragm breathes the outside air: it holds Δp above it, so less absolute pressure, less air, at altitude. The ECU aims at an absolute pressure: at 2000 m (79,495 Pa in the standard atmosphere), it asks for 0.218 bar more above the outside air, so a bigger pressure ratio, a faster shaft, more lag, up to the wheels' limit, which is a pressure ratio and lowers the absolute target. Ambient pressure is a parameter (`BoostControlInput::ambientPa`, `boostTargetPa`), at 101,325 Pa until the atmosphere is wired in; `turboSteady` and the turbo itself still keep their fixed atmosphere.

### By gear (`by_gear`)

The same control, with each gear's target bounded by the torque it can pass, taken from the car and not from a table:

- the force the driven tyres pass, weight transfer included (the acceleration it gives loads the rear): F = μ·m·g·f/(1 + μ·h/L) for front drive, μ·m·g·(1 − f)/(1 − μ·h/L) for rear drive, μ·m·g for all-wheel drive, with μ = 1.05 (that of `DrivingModel`), f the share of weight on the front, h the height of the centre of gravity, L the wheelbase;
- the crankshaft torque that gives it, C = F·r/(i·η), bounded by the gearbox's admissible torque;
- the pressure that gives this torque, by inverting `fullLoadTorqueNm` by bisection: the filling model run backwards, like a torque-structured ECU.

The engaged gear is read in `DrivingModel`; in neutral, first gear. On `three_cyl_turbo` (front drive, 1150 kg), first gear passes 137 Nm, against 192 Nm at the engine: the target falls there from 2.21 to 1.46-1.71 bar absolute depending on rpm, second gear (260 Nm) is not bounded. From a standing start, the peak slip in first falls from 5.6 to 1.9 m/s. But 0-100 km/h goes from 10.03 to 10.22 s: the model's tyre has no grip peak, more slip always gives a little more force, and limiting torque can only reduce it. On all-wheel drives (`hot_hatch_turbo`, 388 Nm in first for 357 Nm), nothing changes.

**What does not change.** The spec sheet (`torqueCurveNm`) stays the spring's: at sea level, electronic control holds `turboSteady`'s equilibrium exactly, and the gearbox stays designed for this engine. WLTC consumption does not move: 8.90 L/100 km in both cases on `hot_hatch_turbo`, 6.34 and 6.32 on `three_cyl_turbo` by gear.

**Telemetry.** `rf_boost_target` and `rf_boost_charge` (bar absolute), `rf_boost_duty` (−1 on a spring). The turbo boost chart shows the target next to the real pressure, and the manifold gauge the solenoid's duty cycle. `--trace` writes `target_abs_bar`, `charge_abs_bar` and `duty`.

**What is assumed, and what is missing.** The valve's and hose's times are orders of magnitude, not measurements. The PWM is averaged: the diaphragm's 25 Hz ripple is not computed, nor the valve's click. No part-load target: the throttle keeps its role, and pressure builds ahead of it as with the spring. No surge margin in the target. A diesel keeps a wastegate here; variable geometry is another setting. Listening still to do: `--scenario turbo` and `--scenario launch`, with and without `--boost-control electronic`.

## Two turbos, twincharger and variable geometry

`turboLayout` (`single`, `twin_parallel`, `twin_sequential`, `twincharger`) and `variableGeometry` choose the layout. The single wastegated turbo stays `TurboCharger`, bit for bit: `TwinTurbo` (`core/src/twin_turbo.cpp`) only runs for the other layouts, and for a single turbo with vanes (`usesTwinTurbo`). It uses the laws of `turbo.cpp` (Moore-Greitzer compressor, Stodola turbine, bearings, windage, intercooler, blow-off valve) through a forwarding header, `turbo_laws.hpp`, and those of the supercharger through `supercharger_laws.hpp`: nothing is copied.

### Each shaft, one shared volume

Each shaft integrates its own power balance, like the single turbo:

    J_i · ω_i · dω_i/dt = P_turbine,i − P_compressor,i − P_bearings,i

Each compressor blows through its own duct, with the inertia of its air column (`ductInertance`), into one volume before the throttle:

    dp_charge/dt = R·T / V_charge · (Σ ṁ_compressor,i − ṁ_engine − ṁ_blow-off)

This shared volume couples the shafts: the slower one delivers less, and can surge. The filter and the intercooler are shared, their losses follow the total flow; so is the blow-off valve.

### Parallel

Each turbo is the one of half an engine (`turboSpecFor` on half the displacement): half the rated flow. At the same pressure and tip speed, flow goes as D², so the wheel is √2 smaller, and the shaft spins √2 faster.

**Inertia as D⁵.** With similar geometry, mass goes as D³ and the squared radius of gyration as D²: J ∝ D⁵, the law the sizes of `turbo.cpp` already use. Each shaft has 2^(−5/2) = 0.177 of the single turbo's inertia. Spool time goes as J·ω²/P, that is D^(n−4) for J ∝ Dⁿ: with n = 5, two small turbos spool in 0.71 times the time of a large one; with n = 3, they would be 1.41 times slower, which contradicts all twin-turbo experience. n must exceed 4; a real small rotor is a bit heavier than similarity says (shaft, nut, bearings), so between 4 and 5. We keep 5.

Two air paths, each with its compressor and hot pipe: the volume before the throttle is that of two halves, 3 L + 2.5·V instead of 1.5 L + 2.5·V. That is 1.5 L more to fill, which takes back part of the gain.

**Two shaft speeds.** Each turbine gets its bank; an inline engine is split front and rear. Gas loses heat along the header, as exp(−k·L) of its excess over ambient, with k = 0.27 /m (about 15 % over 0.6 m between the valve and the turbine, assumed). A bank with longer headers sends slightly cooler gas to its turbine: on a V6, 110,495 and 110,090 rpm one second after the stab at 2500 rpm. These 0.4 % are enough for the two whistles to beat.

Inline six, 3 L, 1 bar:

| | single turbo | parallel twins |
|---|---|---|
| wheel | 60.7 mm | 42.9 mm each |
| inertia | 8.39·10⁻⁵ kg·m² | 1.48·10⁻⁵ kg·m² each |
| 90 % of the boost, `--scenario turbo` | 2.55 s | 2.07 s (−19 %) |
| 90 % of the boost at a held 2500 rpm | 1.62 s | 1.19 s (−27 %) |
| steady boost, torque | — | within 3 % |

On a small engine, two half turbos hit the smallest wheel made (`kSmallestWheel`, 34 mm): on the 2 L diesel they lose the inertia advantage and keep the volume of two paths, and boost arrives later (3.9 s against 2.6 s). This is why small diesels went sequential or variable-geometry.

### Sequential

Two identical turbos, as on the Toyota 2JZ-GTE, the Porsche 959 (two KKK) and the Mazda 13B-REW (two Hitachi). Three stages:

- **0, primary alone.** Both headers are joined; the secondary's exhaust valve, downstream of its turbine, is shut: all the gas goes through the primary, a small turbo on a big engine, which spools very fast.
- **1, pre-spool.** A small valve lets some gas to the secondary turbine (16 % of the section, the ratio of a 20 mm valve to a 50 mm one, assumed), its wastegate shut. Its compressor blows into a small volume (0.3 L per 2 L, assumed), returned to its inlet by a recirculation valve.
- **2, both.** The exhaust valve opens in 0.12 s (vacuum actuator, assumed): gas splits between the turbines by their sections, through the ellipse law at a common pressure. The recirculation valve closes, and the intake check valve opens by itself once the secondary pushes harder than the charge.

The switch follows engine air flow, as a share of one turbo's rated flow: pre-spool at 1.25, opening at 1.42, back to the primary below 1.10, as the primary nears its choke. The threshold is set on the 2JZ-GTE (second turbo's exhaust valve near 3800 rpm, from published Aristo logs), then checked without retouching on the other two:

| | published | model, full throttle in second |
|---|---|---|
| 2JZ-GTE (2997 cm³, 0.8 bar) | near 3800 rpm | 3670 rpm, pre-spool at 3230 |
| Porsche 959 (2849 cm³, 1 bar) | near 4000 rpm | 4030 rpm |
| Mazda 13B-REW (2 × 654 cm³, 0.7 bar) | 4000 to 4500 rpm | 4340 rpm |

The boost dip is not written in: it comes from the switch. The primary loses half its gas at once while the secondary is only at three quarters of its speed: on the 2JZ, boost goes from 1.02 to 0.91 of the target. At a held 2500 rpm, 90 % of the boost arrives in 0.51 s, against 1.19 s in parallel.

### Twincharger

As on VW's 1.4 TSI (1390 cm³, 125 kW, 240 Nm from 1750 rpm; Self-Study Programme 359): an Eaton Roots ahead of the turbo, belt-driven through an electromagnetic clutch, and a flap that bypasses it. Air → filter → supercharger or flap → volume between the stages → turbo → intercooler → throttle. That volume, tied to the inlet by the open flap, is stepped implicitly: explicit, the 1 ms step oscillated.

- **The clutch.** VW keeps it engaged under load up to 2400 rpm, only when needed up to 3500 rpm, and open beyond. Here, `turboAloneRpm` is where the turbo alone holds the target in steady state; below it, the clutch closes as soon as boost is asked for; above, only when more than 10 % is missing; it opens once the flap has held nothing back for 0.5 s, and always beyond `clutchOffRpm`, in VW's ratio (3500/2400). 10 % hysteresis.
- **The supercharger** is sized to hold the target alone at 40 % of the cut-off speed, like `superchargerSpecFor`, and its ratio brings its rotors to 16,000 rpm (an Eaton Roots) at the cut-off. Rotor inertia goes as V^(5/3) (4·10⁻⁴ kg·m² for 0.5 L per turn, assumed); the clutch carries 1.5 times their torque at the sizing point (assumed). While it slips, the crank pays that torque times the ratio.
- **The flap** returns to the inlet what exceeds the target; while the supercharger is engaged, the turbo's wastegate stays shut (unless 10 % over) to spool it.

On the 1.4: the turbo holds alone from 2000 rpm, cut-off at 2920 rpm, ratio 5.5, 0.51 L per turn (VW: cut-off near 3500 rpm per Autozine, 3000 per Top Gear; the model's turbo, sized by `turboSpecFor`, is a little quicker than VW's). At a held 1600 rpm, 90 % of the boost in 0.41 s; the turbo alone never gets there. The data-sheet equilibrium multiplies the two stages' ratios, giving the turbo the ratio it would have alone: an approximation, as the denser air actually gives it a little more gas.

### Variable geometry

Vanes ahead of the turbine change its section: K_eff = K·v, with no wastegate. The widest opening is the one that holds the target at redline, plus 15 % (assumed); the travel spans 1 to 4 in section (order of magnitude of modern vane turbines, assumed). Efficiency is best near 60 % of the travel:

    η(v) = η₀ · (1 − 0.4 · (x − 0.6)²),   x = (v − v_min) / (v_max − v_min)

that is 14 % less with the vanes shut, 6 % wide open (assumed, from the shape of published maps). The vanes follow the same law as the single turbo's wastegate, with its anticipation, and a 0.08 s actuator.

| | closed vanes, as a share of the fixed turbine | 90 % of the boost |
|---|---|---|
| 2 L diesel, 2000 rpm | 0.53 | 1.21 s against 2.10 s (−42 %) |
| inline-six petrol, 2500 rpm | 0.95 | 2.21 s against 1.62 s |

On the diesel, steady boost at 1500 rpm rises from 0.36 to 0.80. On petrol, hotter gas and a wider flow range need a much larger widest opening: shut, the vanes are no smaller than the small wastegated fixed turbine, and less efficient. They only win at the top, through lower back pressure. That is what their rarity on petrol says: the Porsche 911 Turbo (997, 2006) was the first production petrol engine with vane turbines (BorgWarner), with materials for exhaust gas around 1000 °C. Variable geometry does not show in the on-screen gas, whose turbine stays fixed.

### Sound

The second shaft has its whistle, at 2 and 1 times its speed, with its own filters and its own generator (`setTurboB`): the first is not shifted, and nothing runs without it. Two small compressors together make the noise of one large one: each whistle is scaled by √(ṁ_turbo / ṁ_single turbo) (`TurboDrive::levelShare`, exactly 1 for a single turbo). A twincharger's supercharger goes through `SuperchargerSound`, at its rotors' real speed, silent when declutched. **Listening is still to be done**: `revforge-render --out twin.wav --cylinders 6 --layout v --cc 3000 --bore 86 --redline 6800 --aspiration turbo --boost 1 --exhaust sport --turbo-layout twin_parallel --scenario turbo`, then `twin_sequential`, `--preset hot_hatch_turbo --turbo-layout twincharger --scenario launch`, `--preset diesel_four --vgt 1 --scenario turbo`.

### On-screen gas

With two turbos, `GasNetwork` has two headers (an inline engine split front and rear), a turbine at the end of each, pipes that merge before the line, and two air paths from airbox to plenum, each with its compressor and throttle. Their total volume is the C++ one. The second compressor's ratio follows the square of the shaft speed ratio (same wheel, head as U²). Sequential: a pipe joins both headers, the secondary turbine is throttled by its valve, and its air path stays shut while its check valve is. The single turbo keeps its own code path.

### Not modelled

- Anti-lag and the remote turbo pipe, single turbo only: the panel hides them on the other layouts.
- Pulse energy is the same per turbine (`kPulsePa`): the gain of split headers with better-spaced pulses is not counted.
- A single turbo on a V is still drawn one per bank in the views (earlier choice).

### From elsewhere

- Target boost and wastegate opening are parameters (`TwinTurboInput::targetBar`, `wastegate`): negative, they are today's (the spring). Boost control will provide them.
- Ambient air goes through `turbo_laws::constants()` (`atmosphere`, `ambientK`), the same as `turbo.cpp`.
- Banks and header lengths come from `EngineConfig::toProfile` (`bank`, `headerM`).
- `DrivingModel::turboState()` and `turboStateB()` expose each shaft, with the shared charge pressure and temperature; `twinTurboSteady` the equilibrium.

## Charge air

`core/src/charge_air.cpp`: the intercooler (air-to-air, air-to-water, none), water injection and nitrous oxide, as pure functions. What changes while driving (the water loop, the bottle) is the `ChargeAir` state, advanced by `DrivingModel` at each 1 ms step through `revforge::portable`. At the defaults (air-to-air, no water, no nitrous), `chargeAirActive` is false and none of this runs: the presets and the 29 original fingerprints do not move by a bit. On a diesel, water and nitrous have no effect (the setting only appears on a spark-ignition engine); on a naturally aspirated engine, there is no intercooler.

**One principle throughout.** The base torque (`fullLoadTorqueNm`) assumes the original intercooler. Whatever changes the charge changes the mass of air admitted at equal pressure: `C = ((C₀ + f)·T_ref/T·air_share − f)`, where `f` is friction, which does not follow density. On the sheet, `torqueCurveNm` applies it at equilibrium, at 100 km/h; while driving, `DrivingModel` multiplies the indicated torque by the same ratio, read from the current state. The gearbox and clutch remain those of the original engine (`chargeAirStock`), as for the air filter: an intercooler does not redesign a gearbox, and a clutch does not grow stronger for nitrous.

### The intercooler

- **Original air-to-air.** Unchanged: `ε = 0.86 − 0.14·ṁ/ṁ_rated`, a 1.5 s mass, outside air on the other side, whatever the speed.
- **Air-to-air by ε-NTU, with road air** (`airToAirCoolerFor`, `airToAirOutletK`): ready to plug in, it does not replace the original, because it cannot reproduce it to the bit. `UA = 1/(1/hA_charge + 1/hA_road)`, `hA_charge ∝ ṁ^0.8` (Dittus-Boelter), `hA_road ∝ ṁ_road^0.5` (louvered fins, laminar), `ṁ_road = ρ·A·(0.3·v + 1 m/s)` (a grille lets through only part of the speed; the fan, at a standstill; both assumed). Cross-flow, both fluids unmixed: `ε = 1 − exp[(NTU^0.22/Cr)·(exp(−Cr·NTU^0.78) − 1)]` (Incropera). Face of 0.85 m² per kg/s (600 × 300 mm for a 2 L), both sides of equal conductance, fitted to give the original at the rated point at 100 km/h. What it would change to torque at peak power, at equal pressure:

| Preset | Original | 0 km/h | 50 km/h | 100 km/h | 150 km/h |
|---|---|---|---|---|---|
| three_cyl_turbo | 52.4 °C | 92.0 °C, −10.8 % | 60.6 °C, −2.4 % | 52.2 °C, +0.1 % | 48.0 °C, +1.4 % |
| hot_hatch_turbo | 53.6 °C | 94.5 °C, −11.1 % | 62.0 °C, −2.5 % | 53.4 °C, +0.1 % | 49.0 °C, +1.4 % |
| five_cyl_turbo | 56.8 °C | 101.7 °C, −12.0 % | 66.1 °C, −2.7 % | 56.6 °C, +0.1 % | 51.8 °C, +1.6 % |
| boxer_four_turbo | 51.9 °C | 90.7 °C, −10.7 % | 59.9 °C, −2.4 % | 51.7 °C, 0.0 % | 47.5 °C, +1.4 % |
| diesel_four | 61.3 °C | 111.6 °C, −13.1 % | 71.6 °C, −3.0 % | 61.0 °C, +0.1 % | 55.6 °C, +1.7 % |
| v8_supercharged | 40.1 °C | 64.8 °C, −7.3 % | 45.3 °C, −1.6 % | 40.1 °C, 0.0 % | 37.4 °C, +0.9 % |

  The gap only really matters at a standstill, flat out: on a dyno without a fan, or pulling away from a light. Plugging it in would change the 0–100 km/h of the turbo presets, hence their fingerprints: that is a separate decision.
- **Air-to-water.** A brick cooled by a glycol-water loop, its electric pump and its low-temperature radiator at the grille:

  ```
  C_loop·dT_water/dt = ε_b·ṁ·cp·(T2 − T_water) − K_rad(v)·(T_water − T_air) + G·(T_bay(v) − T_water)
  T3 = T2 − ε_b·(T2 − T_water)
  ```

  The brick: ε-NTU, water side four times the air side (assumed), 85 % at the rated point (assumed). The pump: 2 kg of coolant per kg of air at the rated point (24 L/min for a 2 L); at 1 kg/kg, the coolant heated by more than 20 K in the brick, and no radiator of this size could return the original point. The radiator: 0.7 m² per kg/s, ε-NTU against road air, its conductance fitted so that equilibrium at the rated point, at 100 km/h, gives the original air-to-air: the same intercooler, cooled differently. The loop: 3 L of coolant and 3.5 kg of aluminium for 0.21 kg/s, i.e. 68 kJ/K per kg/s. The engine bay: at a standstill, its air is halfway between the outside air and the engine coolant (55 °C on a 20 °C day, warm engine), half that rise at 36 km/h; it gives 70 W/K per kg/s to the loop. All these sizes are assumed, in the range of production systems.

  `--scenario soak` (hot_hatch_turbo, a minute at a standstill, then three 8 s standing starts with 20 s stops between them):

  | | Standstill (60 s) | End of start 1 | 2 | 3 | Water at start 1/2/3 |
  |---|---|---|---|---|---|
  | Air-to-air | 19.9 °C | 48.6 °C | 48.6 °C | 48.7 °C | — |
  | Air-to-water | 21.3 °C | 43.9 °C | 45.6 °C | 46.2 °C | 21.3/24.9/26.2 °C |

  The loop's mass makes the first start cooler than the air-to-air; it then keeps what it took, and the radiator, without speed, only gives it back in about 90 s. At a standstill alone, the loop gains only 1.4 K in a minute (3 K at equilibrium): what a sensor in a hot manifold reads at very low flow comes mostly from the manifold, which is not modelled, for any intercooler. Published order of magnitude: a front-mounted air-to-air cooler holds the charge about 15 K above ambient at full power (Behr, patent US 7,779,791 B2), 20 to 30 K according to other patents from the same maker; here, 28.6 K at the end of a start and 33.6 K at equilibrium at peak power.
- **None.** The compressor outlet goes straight to the throttle: `T3 = T2`. The air path (`ChargePath`, copied by `fluidGas.ts`): hot pipe 0.45 m, core 0.45 m at four times the section, cold pipe 0.5 m for a turbo's air-to-air; a 0.25 m brick and a 0.2 m cold pipe for air-to-water; a single 0.8 m pipe without intercooler. The volume follows: 6.5, 3.9 and 1.9 L on a 2 L. The loss follows `ρv²`: Darcy (`f = 0.02`) and 0.3 per bend; the core carries about 80 % of the original 0.12 bar, a brick half that (assumed). Lag hardly improves: 90 % of the boost at 3000 rpm in 0.85, 0.80 and 0.76 s, because shaft inertia dominates. The turbo and supercharger stay those of the original intercooler.

| | Peak torque | Power | 0–100 km/h | Charge at peak power |
|---|---|---|---|---|
| hot_hatch_turbo, air-to-air | 357 Nm at 2850 | 194.4 kW | 6.86 s | 53.6 °C |
| air-to-water | 363 Nm at 2850 | 195.0 kW | 6.60 s | 52.6 °C |
| none | 274 Nm at 4350 | 150.4 kW | 7.79 s | 131.7 °C |
| water injection | 378 Nm at 2850 | 209.5 kW | 6.59 s | 27.8 °C |
| nitrous 75 kW | 598 Nm at 3000 | 271.5 kW | 5.57 s | 18.4 °C |
| v8_supercharged, original | 767 Nm at 3650 | 393.6 kW | 6.99 s | 40.1 °C |
| air-to-water | 772 Nm at 3550 | 393.7 kW | 6.96 s | 40.1 °C |
| none | 651 Nm at 3850 | 338.7 kW | 7.16 s | 87.4 °C |
| hot_hatch_na | 194 Nm at 4500 | 117.8 kW | 8.72 s | 19.9 °C |
| water injection | 203 Nm at 4550 | 123.7 kW | 8.42 s | 6.4 °C |
| nitrous 75 kW | 417 Nm at 3000 | 192.3 kW | 7.18 s | −27.5 °C |

Without an intercooler, the engine loses 23 % of its power from density alone: knock, which an ECU would avoid by pulling timing, is not in this calculation (see what knock reads, below).

### Water injection

Water, at 30 % of the fuel mass (assumed; according to the press, BMW's M4 GTS asks for its 5 L of water to be refilled at every fuel stop on track, where injection only works at full load), sprayed into the manifold from 80 % fill, all of it beyond 95 %. It evaporates until the air is saturated, or until the last drop:

```
cp_air·(T_in − T) = x_water·c_water·(T − T_tank) + x_vap·h_fg(T),   x_vap = min(x_water, 0.622·p_s(T)/(p − p_s(T)))
ln(p_s/p₀) = (a/R_v)·(1/T₀ − 1/T) − (b/R_v)·ln(T/T₀),   h_fg = a − b·T
```

Clausius-Clapeyron integrated with a linear latent heat (Kirchhoff): `a = 3.1615 MJ/kg`, `b = 2386 J/(kg·K)`, from boiling at 1 atm; within 1 % of the tables at 20 and 60 °C. The temperature is found by bisection. The air admitted is `(p − p_vap)/p·T_in/T` of the dry air: colder, but the vapour takes its place. Dry air at 20 °C goes down to 6.4 °C, its adiabatic saturation temperature (6 to 7 °C on a psychrometric chart): +4.5 % torque on hot_hatch_na. On a turbo, the warm air from the intercooler carries much more water: 53.6 → 27.8 °C, +7 % air. What stays liquid is exposed (`liquidWaterPerAir`): it will evaporate during compression, which is the knock model's business. Outside air is assumed dry; there is no tank to empty.

### Nitrous oxide

- **Saturated nitrous**: ESDU 91022 correlations (pressure, liquid and vapour densities, enthalpies), from 183 to 309 K. 50.5 bar, 786 and 158 kg/m³, 169 kJ/kg at 20 °C; 31.3 bar at 0 °C (NIST: 3.12 MPa).
- **Combustion.** `2 N₂O → 2 N₂ + O₂`: 36.4 % oxygen by mass, against 23.1 % in air, and 82.05 kJ/mol released, i.e. 1.86 MJ/kg, in the flame. The oxygen burns `O₂/(14.7·0.2314) = 0.107` kg of petrol per kg of nitrous; `P_ind = η·(m_fuel·LHV + ṁ_N₂O·Δh)`, `η = 0.39` (between the 0.375 and 0.405 of `consumption.cpp`), i.e. 2.52 MJ indicated per kg. A 75 kW kit flows 29.8 g/s, 0.30 kg in 10 s for 100 hp: the tuners' rule says 0.8 lb, 0.36 kg.
- **The jets.** The fuel jet is at regulated pressure: 1 kg per 6 of nitrous at the design point (a common recommendation, richer than the stoichiometric 9.4), and it does not follow the bottle; the mixture richens as it empties. Its whole flow is paid at the pump, burnt or not. The nitrous jet passes liquid (`ṁ = Cd·A·√(2ρ_l·Δp)`), sized with a full bottle at ambient temperature and the manifold at full load; vapour alone goes through a choked throat (`ṁ = Cd·A·0.667·√(ρp)`, γ = 1.3).
- **The bottle.** 10 lb (4.536 kg), at the regulatory filling ratio of 0.68 kg per litre (6.67 L), 6.8 kg of aluminium (assumed). Liquid leaving frees a volume that liquid must boil to fill: `ṁ_vap = ṁ·ρ_v/(ρ_l − ρ_v)`, and `C·dT/dt = −ṁ_vap·h_fg + hA·(T_air − T)` (liquid at 2.5 kJ/(kg·K), wall at 2 W/K, assumed). For 75 kW: −0.8 bar in 10 s; no liquid left after 133 s, the bottle at 8.8 °C; 5 s later, vapour alone gives 4.5 g/s, 85 % less. Forums speak of 80 to 120 s at 100 hp for this bottle, Holley of 7 to 10 quarter miles for a 125 hp kit. But the bottle is a single lump at equilibrium here: the clear pressure drop by the third pass that tuners report comes from a stratifying liquid, and is not in it. It refills at every engine change, or with the gauge's button.
- **Activation.** Wide-open-throttle switch (pedal at 95 %), rpm window from max(3000, 0.4·redline) to 97 % of the redline (makers advise against opening below 3000 rpm), combustion running. A constant flow gives a constant power, hence a torque in `1/ω`: 417 Nm at 3000 rpm on a 2 L that makes 194. The clutch stays the original one, at 1.6 times the original torque: it can slip.
- **The charge.** The liquid vaporises at the bottle temperature, then the vapour and the fuel take the mixture's: `ṁ_air·cp·(T3 − T) = ṁ_N₂O·[h_fg(T_b) + c_v·(T − T_b)] + ṁ_fuel·[L + c_fuel·(T − T_b)]`, with the fuel's latent heat read as a parameter. At equal pressure, the cylinder is shared between the moles: nitrous and fuel take air's place, and the cold gives some back. On hot_hatch_na at 6750 rpm, −27.5 °C, and the air admitted stays at 100 %: the two effects cancel, and the kit gives its power (+74.5 kW). Tuners claim 60 to 75 °F (33 to 42 K) less at the intake; here 47 K, for a 75 kW kit on a 2 L, large for this engine. The vapour's departure from an ideal gas (around 60 kJ/kg) is ignored: the cooling is rather underestimated.
- **Sound.** `EngineSound::setCharge` adds to the exhaust opening pressure what the charge adds to torque, at the ratio of `boostFill` (0.4 for 0.9 of torque). On `--scenario launch`, hot_hatch_na: 89.6 → 93.2 dB(A) LAeq, the car also going faster. **No listening has been done.**

### What is read, what is exposed

Read as parameters, at the reference values while the other subjects are not plugged in: `ChargeConditions` (`ambientK`, `ambientPa` from the atmosphere, `engineCoolantK` from the thermal model) and `ChargeFuel` (`latentJPerKg`, `stoichiometricAfr`, `lowerHeatingJPerKg` from combustion, `fullLoadLambda` from the ECU). Exposed for knock: `ManifoldCharge` (pressure and temperature at the manifold, water injected and still liquid, nitrous, oxygen in the oxidiser, air share), through `DrivingModel::manifoldCharge()` while driving and `steadyManifoldCharge(p, rpm)` on the sheet; and the pure functions `mixCharge`, `waterSpray`, `waterSaturationPa`, `nitrousSaturation`, `chargeCoolerSteadyK`. On the site: `rf_charge_c`, `rf_charge_water_c`, `rf_water_gps`, `rf_nitrous_bar`, `rf_nitrous_kg`, `rf_nitrous_flow`, `rf_nitrous_refill`, `rf_nitrous_activation_rpm`; the bottle gauge and the charge under the manifold gauge. Tests: `tests/charge_air_test.cpp`, `web/tests/air-de-charge.spec.ts`; fingerprints `golden_charge_*`.

## The two-stroke

`EngineCycle::TwoStroke`: one firing per turn and per cylinder, ports in the liner instead of valves, and a sealed crankcase that draws in the fresh charge and then pushes it into the cylinder. `core/src/two_stroke.cpp`, copied for the drawing and the gas in `web/src/lib/twoStroke.ts`. The four-stroke goes through nothing in this section: the 29 earlier fingerprints did not move. `isTwoStroke(config)` exists in C++ and on the site; a rotary keeps its own cycle.

**The cycle: the only quantity that changes the counts.** Like the rotary, the two-stroke goes through `sweptCcPerTwoTurns` (twice the displacement) and `firingsPerTwoTurns` (twice the cylinders): torque (`bmepToNm`, p·V/2π), air flow, carburetor, consumption and torque ripple follow. In the sound, `firing` stays a two-turn cycle, with two firings per cylinder 360° apart: firing i and i + N belong to the same cylinder.

**Ports, by their height.** Piston travel follows the rod and crank, x/S = ½(1 − cos θ) + λ(1 − √(1 − sin²θ/4λ²)), with a rod two strokes long (λ = l/S = 2, assumed). A port of height h opens when x/S = 1 − h and closes symmetrically after bottom dead centre; the crankcase inlet, uncovered by the skirt, opens when `inletTravel` of the stroke remains before top dead centre. Heights grow with the redline, which needs the same flow area in less time (Blair's "time-area"): from 0.28 to 0.42 of the stroke for the exhaust, 0.17 to 0.24 for the transfers, between 4500 and 9500 rpm (assumed bounds, the order of a touring and a racing engine). On the preset (5000 rpm): exhaust from 107° to 253°, 146°; transfers from 124° to 236°, 112°; inlet 68.5° either side of top dead centre. Widths: 0.62 of the bore for the exhaust (the limit of a window a ring crosses without catching), 0.95 for all transfers, 0.6 for the inlet (assumed). The trapped ratio, counted from exhaust closing, is 1 + (1 − h)(ε − 1): 5.6 for a geometric 7.5 (Saab 96: 7.3; Trabant 601: 7.8).

**The integrated cycle.** `twoStrokeCycle` integrates one cylinder at a fixed 40 µs step over a few turns, with no filling constant: a zero-dimensional crankcase (ratio 1.35, assumed), its inlet column with its own inertia (30 cm: port, manifold and carburetor), transfers and exhaust port in compressible flow (discharge coefficients 0.65 and 0.7, assumed), the cylinder as mass, energy and fresh-air share, and the firing at top dead centre (the heat of the trapped fresh air, 3.0 MJ/kg of which 66 % stays in the gas, as on screen). At the port, the exhaust line: each constant-area stretch is a forward and a backward delay, and at each area change the wave splits, r = (A₁ − A₂)/(A₁ + A₂) (Kelly and Lochbaum); the open end returns it inverted (−0.95). The port pressure, p = p₀ + 2p⁻ + (c/A)·ṁ, and the flow, which depends on it, are solved together at each step (false position). The line is the sound's (`lineStretches`), with boundary-layer and turbulent friction losses (0.05 Np/m) instead of the wider ones of the sound response. Each engine keeps a 16-point table, from a tenth of the redline to 1.2 times it, computed once (13 to 30 ms) and cached for four engines.

**Scavenging.** Benson and Brandham (1969, seen quoted): a share x of the cylinder swept by perfect displacement, the rest by perfect mixing, and a share y of the flow going straight to the exhaust. In volume and at a single temperature, SE = (1 − y)Λ while (1 − y)Λ ≤ x, then SE = 1 − (1 − x)·exp(x − (1 − y)Λ). The integrated cylinder has a single temperature, so the displacement phase is counted on the burnt gas pushed out (a share x of what the cylinder held when the transfers open), not on the cold, dense air that came in. x = 0.5, as a 1991 SAE study recommends (911249, seen quoted); y = 0.12, assumed. What left last is nearest the port: when the returning wave pushes gas back into the cylinder, that comes first, with the fresh air it holds (memory degree by degree).

| Saab 96, 3 cyl. 841 cc | 1600 rpm | 3067 | 3800 | 4533 | 5267 |
|---|---:|---:|---:|---:|---:|
| Delivery ratio, stock line | 0.47 | 0.84 | 0.74 | 0.58 | 0.49 |
| Trapping efficiency | 0.67 | 0.53 | 0.56 | 0.60 | 0.66 |
| Trapped purity | 0.75 | 0.78 | 0.75 | 0.71 | 0.68 |
| Charging efficiency (fresh air / ρV) | 0.33 | 0.44 | 0.42 | 0.35 | 0.35 |
| Delivery ratio, expansion chamber | 0.54 | 0.94 | 0.91 | 0.82 | 0.73 |
| Charging efficiency, expansion chamber | 0.38 | 0.48 | 0.46 | 0.45 | 0.40 |

These efficiencies fall within the ranges published for loop scavenging (delivery ratio 0.5 to 0.9, trapping 0.5 to 0.8: Heywood, chapter 6; Blair), trapping at the low end.

**From charging to torque.** Indicated mean pressure = η × charging efficiency × ρ × LHV / (A/F)ₛ, with ρ = 1.2 kg/m³, 42.9 MJ/kg and 14.7 (a rich mixture burns no more than the air allows). η is the naturally aspirated four-stroke's indicated efficiency (0.405, the consumption's) times the ratio of the Otto cycles at γ = 1.3: 5.6 against 10.5, 0.80. Friction per turn is a four-stroke's of the same displacement without the valvetrain, a quarter less (assumed; 20 to 30 % in Heywood); engine braking is the crankcase alone pumping against the throttle, half (assumed). The flywheel is sized on the displacement, not the swept volume: twice as many firings, half as strong. On a carburetor, the throat loss takes density from the air entering the crankcase, and its pumping work.

**Part load and idle: misfires.** The throttle only passes a share `fill` of the full-load flow; scavenging follows the ratio of the two Benson-Brandham models, which keeps the full-load calculation at full load. At low flow the trapped charge is mostly burnt gas: below a purity of 0.30, with a triangular cycle-to-cycle spread of ± 0.12 (assumed), the spark no longer lights it. After a misfire, the cylinder keeps its mixture and the next scavenging tops it up, π' = 1 − (1 − SE)(1 − π): the next turn fires. This is the "four-stroking" of two-strokes at light load, and at idle one firing every two or three turns, the "ring-ding". `twoStrokeFiring` gives its average through the Markov chain of successive misfires; `DrivingModel` multiplies the torque by what the fresh air really gives (`twoStrokePartLoad`), and the idle control opens further.

| Scavenging purity | 0.75 (full load) | 0.40 | 0.27 | 0.15 (idle) |
|---|---:|---:|---:|---:|
| Turns that fire | 100 % | 100 % | 58 % | 36 % |

**Consumption.** When the petrol comes with the air (carburetor, port injection), what goes straight to the exhaust and what leaves after a misfire are lost: fuel = burnt / (trapping × share that burns). Injected into the cylinder (`Direct`), it arrives after the port closes: only misfires cost. The oil is mixed into the petrol at 1 to 50 by volume (Trabant 601) and leaves with it: no mixture flowing, no oil. CO₂ counts all the fuel, including the unburnt part, which would really be hydrocarbons: it is overestimated.

**Sound.** Ports replace the valves: the uncovered area follows piston travel, with a 3° chamfer at the edge (assumed), and the blowdown lasts the volume at opening over the port area (1.0 ms on the preset). Scavenging pushes towards the exhaust, over the transfer duration, the hot volume of what comes in (1.3 times the fresh air, assumed), instead of the piston expelling its displacement over 240°. The pressure at opening comes from the integrated cycle, relative to a four-stroke's at valve opening: 0.72 (3.4 bar against 4.8). No valve seats; no rocker; the mechanical hash loses the valvetrain (−1.5 dB). Block at −2 dB (peak pressure of 35 to 45 bar instead of 70, −5 dB, but twice the firings, +3 dB, assumed), idle floor at −3 dB as on the rotary. Misfires draw from their own generator: the rest of the engine does not move. Each cylinder keeps the purity of its charge; a misfire opens the port on 28 % of the pressure, with no knock in the block.

Measured at held speed, preset, outside: 65 % misfires at 830 rpm and 0.15 load, 44 % at 0.3, none at 0.5. On the idle spectrogram, in 40 ms windows, the firing rate (41.5 Hz) comes with lines between its own, absent at 0.6 load; the stock line of the 80s body has a bump near 83 Hz that dominates the idle of both cycles. At 3000 rpm full load, the lines fall at 150, 300 and 450 Hz, an octave above the same engine as a four-stroke (75 Hz). LAeq outside at 3000 rpm full load: 68.6 dB(A), 65.8 as a four-stroke, 69.7 with the expansion chamber. **Listening remains to be done.**

**The expansion chamber (`Exhaust::Expansion`).** Its geometry, per cylinder: a header 1.1 times the port's equivalent diameter, a diffuser up to 2.75 times it (Blair: 2.5 to 3.25), a belly, a baffle cone down to a stinger 0.6 times the header (Jennings: 0.58 to 0.62) and 12 of its diameters long, then a small packed silencer of ten times its area and a tail (assumed: a car needs a silencer). The tuning comes from the speed of sound of the hot gas, c = √(γRT) = 501 m/s at 650 K (assumed; Jennings counts 1700 ft/s, 518 m/s): the blowdown peaks when the transfers open, and the middle of the baffle cone must return it while the exhaust port is still half open, 2L = c × travel / 6N, at rated power speed (0.88 of the maximum). Jennings's rule, which returns the wave at closing itself, put the peak 25 % below the target in the integrated cycle: the almost-closed port lets nothing be pushed back. The length shares (header 0.27, diffuser 0.38, belly 0.17, cone 0.36, the latter counted for half) are assumed. The diffuser sends back a suction during scavenging, which draws more air into the crankcase; the baffle cone, an overpressure that pushes the fresh charge back before closing. On the preset: 0.27 m of header, 0.39 of diffuser, 0.17 of belly, 0.37 of cone, 38, 104 and 23 mm diameters.

The sound response is computed from this geometry with the same transfer matrices as the other lines (`ExhaustLine::computeExpansion`), the cones in eight and six steps; N chambers in parallel make one tube of N times their area, same lengths, hence same tuning. On a four-stroke (only the CLI allows it), the chamber is tuned on the valve, from 20° after opening to 30° before closing, and breathes like a sport line: the valves close the cylinder before the wave pushes anything back. Pops leave it like a sport line (`POP_JOULES`, the turbo's line factor).

| Preset (Saab 96 as 3 cyl. 841 cc, 813 kg) | Torque | Power | 0 to 100 km/h | 90 km/h | WLTC | Oil, 90 km/h |
|---|---|---|---|---|---|---|
| Stock line | 56.5 Nm at 3050 | 20.2 kW at 3800 | 33.3 s | 7.73 L/100 | 11.83 L/100 (22.9 km of 23.3) | 1.5 L/1000 km |
| Expansion chamber | 60.9 Nm at 3050 | 26.1 kW at 4900 | 26.9 s | 8.00 L/100 | 12.35 L/100 | 1.6 L/1000 km |
| In-cylinder injection | 59.2 Nm at 3050 | 21.5 kW at 3800 | 30.2 s | 4.67 L/100 | 6.21 L/100 | 0.9 L/1000 km |
| Same engine as a four-stroke | 62.9 Nm at 2600 | 24.5 kW at 4550 | 26.1 s | 4.93 L/100 | 6.24 L/100 | 34 mL/1000 km |
| Saab 96, 1960-1966 (Saab museum, read) | 79 Nm at 3000 | 38 hp (28 kW) at 4250 | 0 to 97 km/h in 25.6 s | 32.3 mpg tested | | |

The model reads 28 % below the Saab in torque and power, and below the same engine as a four-stroke, when real two-strokes of the time did better per litre. The computed trapping at peak torque (0.53) is at the low end of a loop-scavenging range; the trapped ratio of 5.6, the 0.12 short-circuit and the port widths are assumed; nothing was tuned to match the Saab. The best point of the map (492 g/kWh, at low speed, 267 as a four-stroke) and the 90 km/h consumption (7.7 L/100, against 32.3 mpg, 7.3 L/100 in US gallons or 8.7 in imperial gallons, the source does not say) show the short-circuit loss: injected in the cylinder, the same petrol goes 40 % further, of the order direct-injection two-strokes claimed (Orbital, from memory). In the WLTC, the preset does not keep up with the end of the cycle (22.9 km of 23.3): 20 kW cannot hold 131 km/h. Idle: 1.0 L/h.

**Special cases.**
- **The diesel two-stroke** (Detroit Diesel, uniflow-scavenged by a Roots blower with exhaust valves) is not simulated: its scavenging does not go through a crankcase, nor its ports through the piston alone. `coherentTwoStroke` (C++, CLI and WASM) and `coherentConfig` (site) bring a diesel two-stroke back to petrol, as on the rotary.
- **The supercharged two-stroke**: under a turbo alone, nothing would scavenge the cylinder at start-up, the turbine having no energy yet (EMD locomotive diesels drive their turbo through gears for this). A blower pushing the crankcase would change the whole scavenging calculation, with no source to calibrate it. Both go back to natural aspiration, with a note in the panel.
- **At most six cylinders**: each fires twice in two turns, and the sound counts at most 12 firings per cycle. A cross-plane crank means nothing here: it goes back to a V.
- Cam, variable timing, rockers and springs mean nothing: the base already hides the valvetrain, and `valvetrain()` returns the stock double cam, which goes through no valve calculation.

**On screen.** The gas (`GasNetwork`) computes each cylinder and its crankcase, a zero-dimensional volume that follows the piston, with the three ports it uncovers and the same coefficients as the C++ cycle: you see the crankcase rise above atmospheric with the piston down, the mixture pass through the transfers, and fresh mixture reach the outlet. The expansion chamber there is tuned on the engine (`expansionSections`), its cones in a few steps of at least 3 cells; `EXHAUST_LINES.expansion` is only the preset's, for drawing. The gas view draws its cones as slopes. The blue smoke is the diesel's soot tracer, tinted blue: thicker as the leaving gas is fresher (oil only half burns in the trapped charge, assumed). The car draws the chamber at its real size under the floor.

**What stays simplified.** No reed valve or rotary disc at the inlet, only the piston skirt. No freewheel (Saab two-strokes had one, so that oil arrived with the foot off). Oil lubricates nothing in the calculation: without mixture, nothing seizes. The other cylinders' pulses do not travel in the integrated cycle's line: each only has its share of the common line. The wave is linear: the higher speed of large amplitudes is not there. The minimum purity and its spread are assumed, and listening will tell whether the ring-ding comes out. Sources read: the Saab museum sheet (saabmuseum.com, Saab 96 1960-1966), the Westsächsische Hochschule Zwickau sheet (Trabant P 601: 594 cc, 72 × 73, 26 PS at 4240 rpm, 7 L/100 km, 620 kg), English Wikipedia (Trabant 601: 54 Nm at 3000 rpm, ratio 7.8, 1:50 mixture). From memory or seen quoted: Benson and Brandham (1969), Blair (*Design and Simulation of Two-Stroke Engines*, 1996), Jennings (*Two-Stroke Tuner's Handbook*), Heywood (1988), SAE study 911249.
