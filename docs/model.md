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

**What is not modelled:** the detail of pulses in the turbine (only their mean energy counts), variable geometry (a diesel would gain much: this one behaves like a fixed turbo), sequential twin turbos, anti-lag. The large turbo surges slightly at very low revs under full load: a real fault of an oversized turbo, but its level has not been compared with a measurement. On a V, the sound model and the gas see one equivalent turbo, even though the views draw two.

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

**To avoid hunting:** it only upshifts if the engine lands above the floor (2% margin), and if the next gear pulls, 10% below the landing rpm, a share of the road resistance chosen so as not to fall back under the kickdown threshold (1.25 to 2.6 times). It only downshifts to accelerate if the engine stays below 88% of the lower gear's upshift point and if the wheels gain from it, given what the tyres can transmit: one gear less on spinning wheels brings nothing. Right after a shift, it only goes back if the car has really stopped pulling. All this applies to the modes other than Normal, and to Normal as soon as the road is not flat (slope, trailer): on the flat, its original logic has none of these guards, and can still hesitate for a moment between 1st and 2nd at a very light pedal.

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

**The driver** keeps the original calculation (pedal to hold the speed, plus the error to the target), and on the new journeys adds anticipation: the speed not to exceed now in order to reach the next leg's, at the intended deceleration. That is what makes it brake before the corner. `underpowered()` says when, foot to the floor, the car does not reach the target; the site shows it.

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
best point, a low load dearer still, a big engine using more at the same speed, a short gear using more, and a slipping clutch or tyres burning for nothing. **A cold engine is not simulated:** nothing in the
model justifies a temperature.

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

**The model reads high on the road:** about 15 to 25% at steady speed and on WLTC, except the diesel van. The 77 km/h, 98 km/h and custom diesel rows date from before the rolling resistance correction, which lowers them a little; they were not remeasured. Official figures include a cold start (which costs) and often start-stop (which saves), both absent here: they pull in opposite directions, and I assume they weigh little, without having measured it. On the city car's WLTC, the 29% gap splits into:

- a little energy to supply, despite rolling resistance now at 0.0095: the preset weighs 1100 kg and loses 7% in the gearbox, plus its drag;
- about 8% of efficiency: 361 g/kWh over the cycle, against an estimated 335.

The model's automatic gearbox matters little: a driver following longer gears, as the regulation does, only gains 3 to 8%.

**Electric still reads high:** CdA of 0.70 against 0.47 on a Model 3, and a figure at the battery compared with official figures at the plug, which include charging losses and so flatter this comparison. The rolling and motor drag corrections took it from 22.2 to 17.5 kWh/100 km.

**Diesel idle reads low, petrol idles read high,** and all three turn at 850 rpm, higher than some real engines. No alternator or accessory is simulated.

**With the automatic gearbox, the engine can still die** on `diesel_four`, once during the WLTC, rolling at low speed in first with the clutch open: its idle control does not catch it. It now declares a stall, so the site offers a restart, but the cause remains.

**Turbo and gearbox settings count:** these figures follow the model's real torque and pressure, and will move if those parts change.

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
