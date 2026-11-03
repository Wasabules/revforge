import { carArgs, configArgs, gearboxArgs, gearRatios, tuningArgs, type EngineConfig } from './config';
import { t } from './i18n.svelte';

export const Mode = { Off: 0, Neutral: 1, Drive: 2, Lap: 3, Manual: 4 } as const;
export type ModeValue = (typeof Mode)[keyof typeof Mode];

/** Même ordre que `Listener` en C++. */
export const Listener = { Outside: 0, Cabin: 1, EngineBay: 2, Front: 3, Side: 4, Far: 5, Pipe: 6 } as const;
export type ListenerValue = (typeof Listener)[keyof typeof Listener];

export const KW_TO_HP = 1.35962;

export interface Telemetry {
  rpm: number;
  speed: number;
  gear: number;
  throttle: number;
  load: number;
  boost: number;
  /** L'arbre, en part de sa vitesse nominale : 1 au point nominal d'un turbo moyen. */
  turbine: number;
  turboRelease: number;
  /** L'arbre du turbo (tr/min), calculé par le bilan de puissance : la vraie vitesse, pas une estimation. */
  turboRpm: number;
  /** Pressions, en bar au-dessus de l'atmosphère : avant le papillon, après lui, en sortie de compresseur, en entrée, avant la turbine. */
  turboCharge: number;
  turboPlenum: number;
  turboOutlet: number;
  turboInlet: number;
  turboExhaust: number;
  /** Soupape de décharge de la turbine (wastegate) et de l'admission (blow-off) : 0 fermée, 1 ouverte. */
  turboWastegate: number;
  turboBlowoff: number;
  /** Débit du compresseur (kg/s) : négatif quand l'air revient, au pompage. */
  turboFlow: number;
  /** Températures (°C) : sortie du compresseur, sortie de l'échangeur, entrée de la turbine. */
  turboOutC: number;
  turboCoolC: number;
  /** L'air à l'entrée de l'admission, à ce régime : ce que `intakeAirK` donne au couple. */
  intakeAirC: number;
  turboGasC: number;
  /** L'anti-lag travaille en ce moment (0 ou 1) : lu de la simulation, jamais déduit de la pédale. */
  antiLag: number;
  /** Chaleur libérée hors du travail du piston (kW), air envoyé au collecteur (kg/s), couple retiré (Nm). */
  antiLagHeatKw: number;
  antiLagAirFlow: number;
  antiLagLostNm: number;
  /** Le compresseur mécanique : l'arbre (tr/min), ce qu'il prend au vilebrequin (kW), la part du débit rendue à l'entrée. Les pressions et températures sont les mêmes champs que pour un turbo. */
  chargerRpm: number;
  chargerKw: number;
  chargerSpill: number;
  torque: number;
  power: number;
  clutch: number;
  /** Arbre primaire, derrière le disque (tr/min). */
  clutchInputRpm: number;
  /** Puissance dissipée par le glissement (kW), et couple transmis (Nm). */
  clutchSlipKw: number;
  clutchTorque: number;
  wheelRpm: number;
  slip: number;
  pops: number;
  cut: number;
  shifting: number;
  cranking: number;
  stalled: number;
  lugging: number;
  /** Le trajet du pilote automatique : l'étape (position dans `LEG_KINDS`), la vitesse visée, la pente, et la voiture à bout de forces. */
  legKind: number;
  legTarget: number;
  gradePct: number;
  underpowered: number;
  /** Niveau estimé à la position d'écoute, pondéré A, constante de temps « fast ». Ne dépend pas du volume. */
  levelDbA: number;
  /** Où est la tête, en mètres : au sol et au centre de la voiture, x vers l'avant, y vers la gauche. */
  listenerX: number;
  listenerY: number;
  listenerZ: number;
  peak: number;
  /** Arbre de sortie de boîte, avant le pont (tr/min). */
  outputRpm: number;
  meshed: number;
  /** Étape du passage : 0 aucune, 1 débrayage, 2 sortie, 3 course, 4 synchronisation, 5 engagement, 6 rembrayage, 7 relais. */
  shiftStep: number;
  /** Écart de vitesses sous le baladeur qui force (rad/s). */
  grinding: number;
  overload: number;
  boxWear: number;
  boxBroken: number;
  /** Le rapport visé par le passage en cours : 0 aucun ou point mort, -1 marche arrière. */
  boxPending: number;
  /** Écart de vitesses sur le cône pendant la synchronisation (rad/s). */
  boxConeSlip: number;
  /** Force du dernier choc de claquement, et position des quatre roues folles du modèle dans leur jeu (-1 à 1). */
  boxRattle: number;
  boxLoose0: number;
  boxLoose1: number;
  boxLoose2: number;
  boxLoose3: number;
  /** Consommation estimée : débit instantané (L/h), et L/100 km en roulant (0 à l'arrêt : la valeur n'a pas de sens). */
  fuelLph: number;
  fuelLper100km: number;
  /** Depuis la remise à zéro : litres brûlés, moyenne (L/100 km), distance (km), CO₂ moyen (g/km). */
  fuelTripL: number;
  fuelAvgLper100km: number;
  tripKm: number;
  co2GPerKm: number;
  /** Huile brûlée, ordre de grandeur : débit (g/h), total (mL), moyenne (mL/1000 km). */
  oilGph: number;
  oilTripMl: number;
  oilMlPer1000km: number;
  /** Électrique, à la batterie : instantané et moyenne (kWh/100 km), total (kWh, net de la récupération). */
  energyKwhPer100km: number;
  energyAvgKwhPer100km: number;
  energyTripKwh: number;
  /**
   * Les températures du moteur (°C) : eau, huile, métal ; le calorstat (0 à 1) et le ventilateur (0 ou 1) ; la viscosité de
   * l'huile (mPa·s). Le réseau tourne toujours ; il n'agit qu'après un départ à froid, et alors : frottements en plus (Nm),
   * ralenti visé en plus (tr/min), carburant en plus (part), et les frottements du moteur chaud à ce régime (Nm).
   */
  coolantC: number;
  oilC: number;
  metalC: number;
  thermostat: number;
  fan: number;
  oilViscosity: number;
  coldFrictionNm: number;
  coldIdleRpm: number;
  coldEnrichment: number;
  warmFrictionNm: number;
  /** Le cliquetis, quand la combustion n'est plus celle d'origine : avance appliquée et limite (° avant le PMH), avance retirée par le capteur (°), part des cycles qui cliquettent (0-1). Tout à 0 sinon. */
  sparkAdvance: number;
  knockLimit: number;
  knockRetard: number;
  knockShare: number;
  /**
   * Le calculateur, lu de la simulation : l'avance face à la carte d'origine (°, positif plus tôt), le λ demandé (0 sans
   * objet), la part des explosions qui ont lieu (0 à 1), le rupteur, le launch control, le passage à fond (0 ou 1), et la
   * part du couple que retire l'antipatinage (0 à 1).
   */
  ecuAdvance: number;
  ecuLambda: number;
  ecuFired: number;
  ecuLimiter: number;
  ecuLaunch: number;
  ecuFlat: number;
  ecuTraction: number;
}

export const IDLE_TELEMETRY: Telemetry = {
  rpm: 0, speed: 0, gear: 0, throttle: 0, load: 0, boost: 0, turbine: 0, turboRelease: 0, turboRpm: 0, turboCharge: 0, turboPlenum: 0, turboOutlet: 0, turboInlet: 0, turboExhaust: 0, turboWastegate: 0, turboBlowoff: 0, turboFlow: 0, turboOutC: 20, turboCoolC: 20, intakeAirC: 20, turboGasC: 20, antiLag: 0, antiLagHeatKw: 0, antiLagAirFlow: 0, antiLagLostNm: 0, chargerRpm: 0, chargerKw: 0, chargerSpill: 0, torque: 0, power: 0, clutch: 0, clutchInputRpm: 0, clutchSlipKw: 0, clutchTorque: 0, wheelRpm: 0, slip: 0, pops: 0, cut: 0, shifting: 0, cranking: 0, stalled: 0, lugging: 0, legKind: 0, legTarget: 0, gradePct: 0, underpowered: 0, levelDbA: 0, listenerX: 0, listenerY: 0, listenerZ: 0, peak: 0, outputRpm: 0, meshed: 0, shiftStep: 0, grinding: 0, overload: 0, boxWear: 0, boxBroken: 0, boxPending: 0, boxConeSlip: 0, boxRattle: 0, boxLoose0: 0, boxLoose1: 0, boxLoose2: 0, boxLoose3: 0, fuelLph: 0, fuelLper100km: 0, fuelTripL: 0, fuelAvgLper100km: 0, tripKm: 0, co2GPerKm: 0, oilGph: 0, oilTripMl: 0, oilMlPer1000km: 0, energyKwhPer100km: 0, energyAvgKwhPer100km: 0, energyTripKwh: 0,
  coolantC: 20, oilC: 20, metalC: 20, thermostat: 0, fan: 0, oilViscosity: 0, coldFrictionNm: 0, coldIdleRpm: 0, coldEnrichment: 0, warmFrictionNm: 0,
  sparkAdvance: 0, knockLimit: 0, knockRetard: 0, knockShare: 0,
  ecuAdvance: 0, ecuLambda: 0, ecuFired: 1, ecuLimiter: 0, ecuLaunch: 0, ecuFlat: 0, ecuTraction: 0,
};

export interface CurvePoint {
  rpm: number;
  torque: number;
  hp: number;
}

/** Une vitesse de la boîte montée. */
export interface BoxGear {
  gear: number;
  driver: number;
  driven: number;
  ratio: number;
  overall: number;
  /** Rapport de conduite total : plus il est grand, plus l'engrènement est doux. */
  contact: number;
  /** Vitesse au régime maximal (km/h). */
  topKmh: number;
  /** Fréquence d'engrènement à 3000 tr/min. */
  meshHz: number;
  /** Prise directe : pas de denture chargée dans la boîte. */
  direct: boolean;
}

/** La boîte telle qu'elle est construite : tout découle des réglages, rien n'est saisi à la main. */
export interface BoxSpec {
  kind: number;
  gears: BoxGear[];
  finalDrive: number;
  finalPinion: number;
  finalRing: number;
  inputDriver: number;
  inputDriven: number;
  reverseOverall: number;
  limit: number;
  moduleMm: number;
  helix: number;
  center: number;
  inertia: number;
  efficiency: number;
  /** Boîte longitudinale : une paire en prise constante et un arbre intermédiaire. */
  threeShafts: boolean;
  /** Pont à couple conique. */
  hypoid: boolean;
  /** Largeur de denture (mm), dents de la couronne d'engagement, rayon du cône de synchro (mm). */
  faceMm: number;
  dogTeeth: number;
  coneRadiusMm: number;
  /** Marche arrière, en denture droite : pignon, intermédiaire, roue. */
  reverseTeeth: [number, number, number];
}

export const EMPTY_BOX: BoxSpec = { kind: 0, gears: [], finalDrive: 0, finalPinion: 0, finalRing: 0, inputDriver: 0, inputDriven: 0, reverseOverall: 0, limit: 0, moduleMm: 0, helix: 0, center: 0, inertia: 0, efficiency: 0, threeShafts: false, hypoid: false, faceMm: 0, dogTeeth: 0, coneRadiusMm: 0, reverseTeeth: [0, 0, 0] };

/** Fiche technique, calculée par le moteur C++ pour ce réglage. */
export interface Spec {
  idle: number;
  redline: number;
  peakTorque: number;
  peakTorqueRpm: number;
  peakHp: number;
  peakHpRpm: number;
  gears: number;
  ratios: number[];
  /** Le turbo monté : diamètre de la roue (mm), inertie de l'arbre (kg·m²). */
  turboWheelMm: number;
  turboInertia: number;
  /** À la puissance maximale : la perte du filtre (kPa) et la température de l'air admis (°C). */
  intakeDropKpa: number;
  intakeAirC: number;
  /** Le compresseur monté : rapport d'entraînement, volume engendré (L par tour), diamètre de la roue (mm). */
  chargerRatio: number;
  chargerVolume: number;
  chargerWheelMm: number;
  zeroTo100: number;
  topSpeed: number;
  starter: { direct: boolean; kw: number; ring: number; pinion: number; helix: number };
  /** Le carburateur, s'il y en a un : corps, col du venturi, perte de charge à la puissance maximale. */
  carb: { throats: number; throatMm: number; dropKpa: number };
  /** La combustion : le taux réel, son rendement face au taux d'origine, la plus petite marge au cliquetis à pleine charge (°, négative s'il cliquette) et son régime, le capteur. */
  combustion: { ratio: number; efficiency: number; marginDeg: number; marginRpm: number; sensor: boolean };
  /**
   * La carte du calculateur, à pleine charge et au régime de la puissance maximale : l'avance demandée (° avant le point
   * mort haut), le couple face à la carte d'origine, et le gaz à la sortie des cylindres, réglé et d'origine (°C).
   */
  ecu: { rpm: number; sparkDeg: number; fullShare: number; exhaustC: number; stockExhaustC: number };
  curve: CurvePoint[];
  box: BoxSpec;
}

/** La voiture vue de dessus, en mètres (voir `Telemetry.listenerX`), pour la placer sur le plan. */
export interface CarPlan {
  length: number;
  width: number;
  height: number;
  seat: { x: number; y: number };
  seatZ: number;
  engineX: number;
  engineZ: number;
  /** Échappement A et B, admission, compartiment, plancher, route : dans l'ordre de `Source` en C++. */
  sources: { x: number; y: number; z: number }[];
}

/** Ce que ce réglage donnerait à un sonomètre : des estimations, mesurées en jouant le son. */
export interface SoundSheet {
  standstill: number;
  outsideFull: number;
  cabinIdle: number;
  cabinFull: number;
  /** Ce que retient la ligne par rapport à un tube droit (dB), bande par bande. */
  lineLoss: { hz: number; db: number }[];
}

export const EMPTY_SPEC: Spec = { idle: 0, redline: 7000, peakTorque: 0, peakTorqueRpm: 0, peakHp: 0, peakHpRpm: 0, gears: 0, ratios: [], turboWheelMm: 0, turboInertia: 0, intakeDropKpa: 0, intakeAirC: 20, chargerRatio: 0, chargerVolume: 0, chargerWheelMm: 0, zeroTo100: 0, topSpeed: 0, starter: { direct: false, kw: 0, ring: 0, pinion: 0, helix: 0 }, carb: { throats: 0, throatMm: 0, dropKpa: 0 }, combustion: { ratio: 0, efficiency: 1, marginDeg: 0, marginRpm: 0, sensor: false }, ecu: { rpm: 0, sparkDeg: 0, fullShare: 1, exhaustC: 0, stockExhaustC: 0 }, curve: [], box: EMPTY_BOX };

interface RfExports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  rf_init(rate: number): void;
  rf_buffer(): number;
  rf_buffer_right(): number;
  rf_antilag_active(): number;
  rf_antilag_heat_kw(): number;
  rf_antilag_air_flow(): number;
  rf_antilag_lost_nm(): number;
  rf_render(frames: number): void;
  rf_render_stereo(frames: number): void;
  rf_set_listener(listener: number): void;
  rf_set_listener_position(x: number, y: number, z: number): void;
  rf_car_length(): number;
  rf_car_width(): number;
  rf_car_height(): number;
  rf_seat_x(): number;
  rf_seat_y(): number;
  rf_seat_z(): number;
  rf_engine_x(): number;
  rf_engine_z(): number;
  rf_source_x(source: number): number;
  rf_source_y(source: number): number;
  rf_source_z(source: number): number;
  rf_sheet_level(item: number): number;
  rf_sheet_line_loss(fromHz: number, toHz: number): number;
  rf_set_config(...args: number[]): void;
  /** Les réglages du socle moteur : posés, pas appliqués. Juste avant rf_set_config ou rf_set_electric. */
  rf_set_tuning(...args: number[]): void;
  rf_intake_air_c(rpm: number): number;
  rf_intake_drop_kpa(rpm: number): number;
  rf_turbo_wheel_mm(): number;
  rf_turbo_inertia(): number;
  rf_supercharger_ratio(): number;
  rf_supercharger_volume(): number;
  rf_supercharger_wheel_mm(): number;
  rf_set_electric(): void;
  rf_set_car(...args: number[]): void;
  rf_set_mode(mode: number): void;
  rf_set_shift_mode(mode: number): void;
  rf_set_scenario(scenario: number): void;
  rf_scenario_seconds(): number;
  rf_crank(): void;
  rf_set_controls(pedal: number, brake: number, clutch: number): void;
  rf_set_volume(volume: number): void;
  rf_torque_at(rpm: number): number;
  rf_power_at(rpm: number): number;
  rf_peak_torque(): number;
  rf_peak_torque_rpm(): number;
  rf_peak_power(): number;
  rf_peak_power_rpm(): number;
  rf_idle_rpm(): number;
  rf_redline(): number;
  rf_gears(): number;
  rf_overall_ratio(gear: number): number;
  rf_zero_to_100(): number;
  rf_top_speed(): number;
  rf_starter_direct(): number;
  rf_starter_kw(): number;
  rf_carb_throats(): number;
  rf_carb_throat_mm(): number;
  rf_carb_drop_kpa(): number;
  rf_compression_ratio(): number;
  rf_compression_efficiency(): number;
  rf_knock_sensor(): number;
  rf_knock_margin_deg(): number;
  rf_knock_margin_rpm(): number;
  rf_spark_advance(): number;
  rf_knock_limit(): number;
  rf_knock_retard(): number;
  rf_knock_share(): number;
  rf_starter_ring(): number;
  rf_starter_pinion(): number;
  rf_starter_helix(): number;
  rf_set_gearbox(...args: number[]): void;
  rf_set_gear_ratio(gear: number, ratio: number): void;
  rf_reverse_ratio(): number;
  rf_box_ratio(gear: number): number;
  rf_box_driver(gear: number): number;
  rf_box_driven(gear: number): number;
  rf_box_contact(gear: number): number;
  rf_box_mesh_hz(gear: number, inputRpm: number): number;
  rf_box_final(): number;
  rf_box_final_pinion(): number;
  rf_box_final_ring(): number;
  rf_box_input_driver(): number;
  rf_box_input_driven(): number;
  rf_box_direct(): number;
  rf_box_kind(): number;
  rf_box_limit(): number;
  rf_box_module(): number;
  rf_box_helix(): number;
  rf_box_center(): number;
  rf_box_inertia(): number;
  rf_box_efficiency(): number;
  rf_box_three_shafts(): number;
  rf_box_hypoid(): number;
  rf_box_face(): number;
  rf_box_dog_teeth(): number;
  rf_box_cone_radius(): number;
  rf_box_reverse_teeth(which: number): number;
  rf_wheel_radius(): number;
  rf_clutch_input_rpm(): number;
  rf_clutch_slip_kw(): number;
  rf_clutch_torque(): number;
  rf_line_impulse(): number;
  rf_line_impulse_length(): number;
  rf_fuel_lph(): number;
  rf_fuel_l_per_100km(): number;
  rf_fuel_trip_l(): number;
  rf_fuel_avg_l_per_100km(): number;
  rf_trip_km(): number;
  rf_co2_g_per_km(): number;
  rf_oil_gph(): number;
  rf_oil_trip_ml(): number;
  rf_oil_ml_per_1000km(): number;
  rf_energy_kwh_per_100km(): number;
  rf_energy_avg_kwh_per_100km(): number;
  rf_energy_trip_kwh(): number;
  rf_reset_trip(): void;
  /** Les températures du moteur (°C), le calorstat (0 à 1), le ventilateur, la viscosité de l'huile (mPa·s). */
  rf_coolant_c(): number;
  rf_oil_c(): number;
  rf_metal_c(): number;
  rf_thermostat(): number;
  rf_fan(): number;
  rf_oil_viscosity(): number;
  /** Ce que le moteur froid change : frottements en plus (Nm), ralenti visé en plus (tr/min), carburant en plus (part). */
  rf_cold_friction_nm(): number;
  rf_cold_idle_rpm(): number;
  rf_cold_enrichment(): number;
  rf_warm_friction_nm(rpm: number): number;
  /** Le calculateur : ce qu'il fait en ce moment, et sa carte pour la fiche. */
  rf_ecu_advance(): number;
  rf_ecu_lambda(): number;
  rf_ecu_fired(): number;
  rf_ecu_limiter(): number;
  rf_ecu_launch(): number;
  rf_ecu_flat(): number;
  rf_ecu_traction(): number;
  rf_ecu_full_share(): number;
  rf_ecu_spark_deg(rpm: number, load: number): number;
  rf_ecu_exhaust_c(rpm: number, stockMap: number): number;
}

/** Le moteur finit de tourner sur son élan avant qu'on suspende l'audio. */
const RUNDOWN_BEFORE_SUSPEND_MS = 2200;
const CURVE_POINTS = 56;

/** Le moteur : un exemplaire dans l'AudioWorklet pour le son, un autre ici pour les calculs. */
export class Engine {
  analyser: AnalyserNode | null = null;
  private context: AudioContext | null = null;
  private node: AudioWorkletNode | null = null;
  private bytes: ArrayBuffer | null = null;
  private local: RfExports | null = null;
  private suspendTimer: ReturnType<typeof setTimeout> | undefined;
  // Les choix de conduite faits avant le démarrage : le worklet n'existe pas encore, il les reçoit au démarrage.
  private driving = { manual: false, assisted: true, shiftMode: 0, scenario: 0 };

  constructor(private onTelemetry: (t: Telemetry) => void, private onError: (message: string) => void) {}

  async load(): Promise<void> {
    const response = await fetch(`${import.meta.env.BASE_URL}revforge.wasm`);
    if (!response.ok) throw new Error(t('error.wasm', { status: response.status }));
    this.bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(this.bytes, {});
    this.local = instance.exports as unknown as RfExports;
    this.local._initialize?.();
    this.local.rf_init(44100);
  }

  get ready(): boolean {
    return this.local !== null;
  }

  /** À appeler depuis un geste de l'utilisateur : les navigateurs l'imposent pour le son. */
  async start(config: EngineConfig, mode: ModeValue, volume: number): Promise<void> {
    clearTimeout(this.suspendTimer);
    if (!this.context) {
      const context = new AudioContext({ latencyHint: 'interactive' });
      await context.audioWorklet.addModule(`${import.meta.env.BASE_URL}worklet.js`);
      const node = new AudioWorkletNode(context, 'revforge', {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
        processorOptions: { wasm: this.bytes },
      });
      const analyser = context.createAnalyser();
      analyser.fftSize = 4096;
      analyser.smoothingTimeConstant = 0.72;
      node.connect(analyser);
      analyser.connect(context.destination);
      await new Promise<void>((resolve, reject) => {
        node.port.onmessage = ({ data }) => {
          if (data.type === 'ready') {
            node.port.onmessage = ({ data: message }) => this.onMessage(message);
            resolve();
          } else if (data.type === 'error') {
            reject(new Error(data.message));
          }
        };
      });
      this.context = context;
      this.node = node;
      this.analyser = analyser;
    }
    await this.context.resume();
    this.setConfig(config);
    this.setVolume(volume);
    this.send({ type: 'transmission', manual: this.driving.manual });
    this.send({ type: 'assist', assisted: this.driving.assisted });
    this.send({ type: 'shiftMode', mode: this.driving.shiftMode });
    this.send({ type: 'scenario', scenario: this.driving.scenario });
    this.setMode(mode);
    this.send({ type: 'crank' });
  }

  stop(): void {
    this.setMode(Mode.Off);
    clearTimeout(this.suspendTimer);
    this.suspendTimer = setTimeout(() => void this.context?.suspend(), RUNDOWN_BEFORE_SUSPEND_MS);
  }

  private onMessage(message: { type: string; message?: string } & Partial<Telemetry>): void {
    if (message.type === 'telemetry') this.onTelemetry(message as Telemetry);
    else if (message.type === 'error') this.onError(message.message ?? t('error.engine'));
  }

  private send(command: Record<string, unknown>): void {
    this.node?.port.postMessage(command);
  }

  setConfig(config: EngineConfig): void {
    this.send({ type: 'car', args: carArgs(config) });
    this.send({ type: 'gearbox', args: gearboxArgs(config), ratios: gearRatios(config) });
    // Les réglages du socle voyagent avec le moteur : un seul changement, jamais un moteur à moitié réglé.
    if (config.electric) this.send({ type: 'electric', tuning: tuningArgs(config) });
    else this.send({ type: 'config', args: configArgs(config), tuning: tuningArgs(config) });
  }

  setMode(mode: ModeValue): void {
    this.send({ type: 'mode', mode });
  }

  setControls(pedal: number, brake: number, clutch: number): void {
    this.send({ type: 'controls', pedal, brake, clutch });
  }

  setManual(manual: boolean): void {
    this.driving.manual = manual;
    this.send({ type: 'transmission', manual });
  }

  /** Comment l'automatique choisit ses rapports : position dans `SHIFT_MODES`. */
  setShiftMode(mode: number): void {
    this.driving.shiftMode = mode;
    this.send({ type: 'shiftMode', mode });
  }

  /** Le trajet du pilote automatique : position dans `DRIVE_SCENARIOS`. Il change en roulant. */
  setScenario(scenario: number): void {
    this.driving.scenario = scenario;
    this.send({ type: 'scenario', scenario });
  }

  /** Assisté : passer un rapport actionne aussi l'embrayage. Au pied : sans débrayer, ça gratte. */
  setClutchAssist(assisted: boolean): void {
    this.driving.assisted = assisted;
    this.send({ type: 'assist', assisted });
  }

  /** La boîte a cédé : on la remplace. */
  repairGearbox(): void {
    this.send({ type: 'repair' });
  }

  /** Remet à zéro les totaux du trajet : litres, huile, énergie, distance. */
  resetTrip(): void {
    this.send({ type: 'resetTrip' });
  }

  /** Relance le démarreur, après un calage. */
  crank(): void {
    this.send({ type: 'crank' });
  }

  shift(direction: 1 | -1): void {
    this.send({ type: 'shift', direction });
  }

  setListener(listener: ListenerValue): void {
    this.send({ type: 'listener', listener });
  }

  /** Une position libre, dans le repère de la voiture : la tête s'y rend en glissant. */
  setListenerPosition(x: number, y: number, z: number): void {
    this.send({ type: 'listenerAt', x, y, z });
  }

  setWindows(open: boolean): void {
    this.send({ type: 'windows', open });
  }

  setVolume(volume: number): void {
    this.send({ type: 'volume', volume });
  }

  /** Niveau de sortie (RMS) lu à la sortie du graphe audio. */
  level(): number {
    if (!this.analyser) return 0;
    const samples = new Float32Array(this.analyser.fftSize);
    this.analyser.getFloatTimeDomainData(samples);
    return Math.sqrt(samples.reduce((sum, s) => sum + s * s, 0) / samples.length);
  }

  private applyLocal(config: EngineConfig): RfExports | null {
    const rf = this.local;
    if (!rf) return null;
    rf.rf_set_car(...carArgs(config));
    gearRatios(config).forEach((ratio, i) => rf.rf_set_gear_ratio(i + 1, ratio));
    rf.rf_set_gearbox(...gearboxArgs(config));
    rf.rf_set_tuning(...tuningArgs(config));
    if (config.electric) rf.rf_set_electric();
    else rf.rf_set_config(...configArgs(config));
    return rf;
  }

  private boxSpec(rf: RfExports, gears: number, redline: number): BoxSpec {
    const radius = rf.rf_wheel_radius();
    const direct = rf.rf_box_direct();
    return {
      kind: rf.rf_box_kind(),
      gears: Array.from({ length: gears }, (_, i) => {
        const gear = i + 1;
        const overall = rf.rf_overall_ratio(gear);
        return {
          gear,
          driver: rf.rf_box_driver(gear),
          driven: rf.rf_box_driven(gear),
          ratio: rf.rf_box_ratio(gear),
          overall,
          contact: rf.rf_box_contact(gear),
          topKmh: ((redline * 2 * Math.PI) / 60) * (radius / Math.max(overall, 0.01)) * 3.6,
          meshHz: rf.rf_box_mesh_hz(gear, 3000),
          direct: gear === direct,
        };
      }),
      finalDrive: rf.rf_box_final(),
      finalPinion: rf.rf_box_final_pinion(),
      finalRing: rf.rf_box_final_ring(),
      inputDriver: rf.rf_box_input_driver(),
      inputDriven: rf.rf_box_input_driven(),
      reverseOverall: rf.rf_reverse_ratio(),
      limit: rf.rf_box_limit(),
      moduleMm: rf.rf_box_module(),
      helix: rf.rf_box_helix(),
      center: rf.rf_box_center(),
      inertia: rf.rf_box_inertia(),
      efficiency: rf.rf_box_efficiency(),
      threeShafts: rf.rf_box_three_shafts() !== 0,
      hypoid: rf.rf_box_hypoid() !== 0,
      faceMm: rf.rf_box_face(),
      dogTeeth: rf.rf_box_dog_teeth(),
      coneRadiusMm: rf.rf_box_cone_radius(),
      reverseTeeth: [rf.rf_box_reverse_teeth(0), rf.rf_box_reverse_teeth(1), rf.rf_box_reverse_teeth(2)],
    };
  }

  /** Puissance, couple et performances de ce réglage : les mêmes calculs que ceux qui font rouler la voiture. */
  /** La carte du calculateur, au régime de la puissance maximale et à pleine charge : tout vient du C++. */
  private ecuSpec(rf: RfExports): Spec['ecu'] {
    const rpm = rf.rf_peak_power_rpm();
    return { rpm, sparkDeg: rf.rf_ecu_spark_deg(rpm, 1), fullShare: rf.rf_ecu_full_share(), exhaustC: rf.rf_ecu_exhaust_c(rpm, 0), stockExhaustC: rf.rf_ecu_exhaust_c(rpm, 1) };
  }

  spec(config: EngineConfig): Spec {
    const rf = this.applyLocal(config);
    if (!rf) return EMPTY_SPEC;
    const redline = rf.rf_redline();
    const idle = rf.rf_idle_rpm();
    const low = Math.max(idle, 500);
    const curve: CurvePoint[] = Array.from({ length: CURVE_POINTS }, (_, i) => {
      const rpm = low + ((redline - low) * i) / (CURVE_POINTS - 1);
      return { rpm, torque: rf.rf_torque_at(rpm), hp: rf.rf_power_at(rpm) * KW_TO_HP };
    });
    const gears = rf.rf_gears();
    return {
      idle,
      redline,
      peakTorque: rf.rf_peak_torque(),
      peakTorqueRpm: rf.rf_peak_torque_rpm(),
      peakHp: rf.rf_peak_power() * KW_TO_HP,
      peakHpRpm: rf.rf_peak_power_rpm(),
      gears,
      ratios: Array.from({ length: gears }, (_, i) => rf.rf_overall_ratio(i + 1)),
      turboWheelMm: rf.rf_turbo_wheel_mm(),
      turboInertia: rf.rf_turbo_inertia(),
      intakeDropKpa: rf.rf_intake_drop_kpa(rf.rf_peak_power_rpm()),
      intakeAirC: rf.rf_intake_air_c(rf.rf_peak_power_rpm()),
      chargerRatio: rf.rf_supercharger_ratio(),
      chargerVolume: rf.rf_supercharger_volume(),
      chargerWheelMm: rf.rf_supercharger_wheel_mm(),
      zeroTo100: rf.rf_zero_to_100(),
      topSpeed: rf.rf_top_speed(),
      starter: { direct: rf.rf_starter_direct() === 1, kw: rf.rf_starter_kw(), ring: rf.rf_starter_ring(), pinion: rf.rf_starter_pinion(), helix: rf.rf_starter_helix() },
      carb: { throats: rf.rf_carb_throats(), throatMm: rf.rf_carb_throat_mm(), dropKpa: rf.rf_carb_drop_kpa() },
      combustion: {
        ratio: rf.rf_compression_ratio(),
        efficiency: rf.rf_compression_efficiency(),
        marginDeg: rf.rf_knock_margin_deg(),
        marginRpm: rf.rf_knock_margin_rpm(),
        sensor: rf.rf_knock_sensor() === 1,
      },
      ecu: this.ecuSpec(rf),
      curve,
      box: this.boxSpec(rf, gears, redline),
    };
  }

  /** Réponse impulsionnelle de la ligne de ce réglage, à 44,1 kHz : la même que celle qui filtre le son. */
  lineImpulse(config: EngineConfig): Float32Array | null {
    const rf = this.applyLocal(config);
    if (!rf || config.electric) return null;
    const pointer = rf.rf_line_impulse();
    if (!pointer) return null;
    return new Float32Array(rf.memory.buffer, pointer, rf.rf_line_impulse_length()).slice();
  }

  /** La voiture vue de dessus, pour ce réglage. */
  plan(config: EngineConfig): CarPlan | null {
    const rf = this.applyLocal(config);
    if (!rf) return null;
    return {
      length: rf.rf_car_length(),
      width: rf.rf_car_width(),
      height: rf.rf_car_height(),
      seat: { x: rf.rf_seat_x(), y: rf.rf_seat_y() },
      seatZ: rf.rf_seat_z(),
      engineX: rf.rf_engine_x(),
      engineZ: rf.rf_engine_z(),
      sources: Array.from({ length: 6 }, (_, i) => ({ x: rf.rf_source_x(i), y: rf.rf_source_y(i), z: rf.rf_source_z(i) })),
    };
  }

  /** Les niveaux de ce réglage : quelques dizaines de millisecondes de calcul, à ne pas appeler à chaque glissé. */
  soundSheet(config: EngineConfig): SoundSheet | null {
    const rf = this.applyLocal(config);
    if (!rf) return null;
    const bands = [125, 250, 500, 1000, 2000, 4000];
    return {
      standstill: rf.rf_sheet_level(0),
      outsideFull: rf.rf_sheet_level(1),
      cabinIdle: rf.rf_sheet_level(2),
      cabinFull: rf.rf_sheet_level(3),
      lineLoss: bands.map((hz) => ({ hz, db: rf.rf_sheet_line_loss(hz / Math.SQRT2, hz * Math.SQRT2) })),
    };
  }

  /** Rend le moteur en WAV 16 bits et en stéréo, plus vite que le temps réel, depuis cette position d'écoute. */
  renderWav(config: EngineConfig, scenario: 'lap' | 'blips', place?: { x: number; y: number; z: number } | ListenerValue): Blob {
    const rf = this.local;
    if (!rf) throw new Error(t('error.notLoaded'));
    const rate = 44100;
    const tick = rate / 20;
    rf.rf_init(rate);
    this.applyLocal(config);
    rf.rf_set_shift_mode(this.driving.shiftMode);
    rf.rf_set_scenario(this.driving.scenario);
    // Le trajet d'origine se rend sur une minute, comme toujours ; un autre, sur une boucle.
    const seconds = scenario === 'lap' ? (this.driving.scenario === 0 ? 60 : Math.round(rf.rf_scenario_seconds())) : 14;
    rf.rf_set_volume(0.7);
    if (typeof place === 'number') rf.rf_set_listener(place);
    else if (place) rf.rf_set_listener_position(place.x, place.y, place.z);
    rf.rf_set_mode(scenario === 'lap' ? Mode.Lap : Mode.Neutral);
    // Le fichier de coups de gaz s'ouvre sur le démarreur.
    if (scenario === 'blips') rf.rf_crank();

    const pcm = new Int16Array(rate * seconds * 2);
    const frameCount = rate * seconds;
    let step = 0;
    for (let done = 0; done < frameCount; done += tick) {
      if (scenario === 'blips') {
        const inBlip = ((step - 20) % 80 + 80) % 80;
        rf.rf_set_controls(step >= 20 && inBlip < 24 ? 1 : 0, 0, 0);
      }
      const frames = Math.min(tick, frameCount - done);
      rf.rf_render_stereo(frames);
      const left = new Float32Array(rf.memory.buffer, rf.rf_buffer(), frames);
      const right = new Float32Array(rf.memory.buffer, rf.rf_buffer_right(), frames);
      for (let i = 0; i < frames; i++) {
        pcm[(done + i) * 2] = Math.max(-32768, Math.min(32767, Math.trunc(left[i] * 32767)));
        pcm[(done + i) * 2 + 1] = Math.max(-32768, Math.min(32767, Math.trunc(right[i] * 32767)));
      }
      step++;
    }
    rf.rf_set_mode(Mode.Off);
    return wavBlob(pcm, rate);
  }
}

function wavBlob(pcm: Int16Array, rate: number): Blob {
  const header = new DataView(new ArrayBuffer(44));
  const text = (at: number, value: string) => [...value].forEach((c, i) => header.setUint8(at + i, c.charCodeAt(0)));
  text(0, 'RIFF');
  header.setUint32(4, 36 + pcm.byteLength, true);
  text(8, 'WAVEfmt ');
  header.setUint32(16, 16, true);
  header.setUint16(20, 1, true);
  header.setUint16(22, 2, true);
  header.setUint32(24, rate, true);
  header.setUint32(28, rate * 4, true);
  header.setUint16(32, 4, true);
  header.setUint16(34, 16, true);
  text(36, 'data');
  header.setUint32(40, pcm.byteLength, true);
  return new Blob([header, pcm as BlobPart], { type: 'audio/wav' });
}
