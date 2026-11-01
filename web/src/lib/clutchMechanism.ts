/**
 * La mécanique de l'embrayage, sans rien dessiner : de la pédale au plateau, et de la butée à sa bague tournante.
 *
 * C'est un embrayage à diaphragme **poussé** à commande hydraulique et fourchette, normalement fermé :
 *
 *   pédale → tige du maître-cylindre → liquide → piston du récepteur → fourchette (sur sa rotule)
 *     → butée (qui coulisse sur le tube-guide, vers le moteur) → doigts du diaphragme (vers le moteur)
 *     → le diaphragme bascule sur ses joncs → son bord s'écarte du moteur → les languettes rappellent le plateau.
 *
 * La simulation ne connaît qu'une part de couple, `motion.clutchShare` : `clutch_ = min(voulu, 1 - pédale)`, assistance
 * comprise. La pédale dessinée s'en déduit (`pedalFromShare`), comme si le pied ou le calculateur la tenait à cet endroit :
 * c'est la seule liaison exacte avec le C++, et elle ne change rien au son.
 *
 * Toutes les longueurs sont en millimètres.
 */

/** Course du pied, et rapport de la pédale : le pied parcourt cinq fois la course de la tige. */
/** Les garnitures fument à partir de cette température (°C) : la vue et la jauge partagent ce seuil. */
export const CLUTCH_SMOKE_C = 260;
export const PEDAL_TRAVEL = 150;
export const PEDAL_RATIO = 5;
/** Alésages courants : 3/4 de pouce au maître-cylindre, 13/16 au récepteur. */
export const MASTER_BORE = 19.05;
export const SLAVE_BORE = 20.6;
/** Hauteurs par rapport à l'axe : rotule, et point d'appui du récepteur sur la fourchette. */
export const FORK_STUD_R = -78;
export const FORK_SLAVE_R = 140;
/** Garde : la butée reste à cette distance des doigts, pédale relâchée. */
export const BEARING_CLEARANCE = 1.2;
/** Course des segments élastiques entre les garnitures : tant qu'ils se détendent, le disque serre encore. */
export const CUSHION_TRAVEL = 0.8;
/** Les doigts ploient : une part de leur course ne fait pas basculer le diaphragme. */
export const FINGER_FLEX = 0.07;
/** Exposant du détachement : les segments relâchent vite le premier dixième, puis la charge s'éteint doucement. */
const CUSHION_LAW = 1.6;
/** Bande sous laquelle la part de couple est nulle à l'œil : le jeu s'ouvre entre le plateau et le disque. */
const SHARE_OPEN = 0.03;
/** Au-dessus, tout le couple passe : la butée peut quitter les doigts sans rien changer. */
export const SHARE_CONTACT = 0.985;

/** Coefficient de friction et pression de surface d'une garniture organique sèche (MPa = N/mm²). */
const LINING_MU = 0.27;
const LINING_PRESSURE = 0.3;
const LINING_RATIO = 0.66;
/** Marge du C++ : `clutchCapacity_ = 1.6 * peakTorque`. */
const CAPACITY_MARGIN = 1.6;

/** Butée de fin de course de l'amortisseur du disque, et couple qui l'y amène (fois le couple maximal). */
export const DAMPER_LIMIT = (8 * Math.PI) / 180;
const DAMPER_STOP_TORQUE = 1.25;
/** Le volant bi-masse accepte bien plus d'angle : ses ressorts en arc tiennent le demi-tour de chaque côté. */
export const DUAL_MASS_LIMIT = (55 * Math.PI) / 180;
const DUAL_MASS_STOP_TORQUE = 1.3;

/** Au contact, le frottement sur les doigts entraîne la bague : quelques dizaines de millisecondes en vrai. */
const RACE_GRIP_TAU = 0.06;
/** Dégagée, seule la graisse la freine. Elle s'arrête en une fraction de seconde. */
const RACE_COAST_TAU = 0.1;
/** Vitesse sous laquelle une bague dégagée est arrêtée. */
const RACE_STOP_RPM = 20;

const clamp = (value: number, low: number, high: number) => (value < low ? low : value > high ? high : value);

export type ClutchKind = 'single' | 'dual_clutch';

/** Les cotes qui ne changent qu'avec le moteur, de quoi construire la coupe, le dessin et le modèle 3D. */
export interface ClutchDims {
  kind: ClutchKind;
  dualMass: boolean;
  /** Diamètres extérieur et intérieur des garnitures, rayon moyen de friction, charge de serrage (N). */
  discDiameter: number;
  liningOuterR: number;
  liningInnerR: number;
  meanFrictionR: number;
  clampLoad: number;
  /** Diaphragme : rayon d'appui sur le plateau, rayon des joncs, rayon de contact de la butée. */
  fulcrumR: number;
  pivotR: number;
  contactR: number;
  leverRatio: number;
  fingers: number;
  /** Rayon extérieur de la couronne, nombre de dents, épaisseur du volant, boulons sur le vilebrequin et du couvercle. */
  flywheelR: number;
  ringTeeth: number;
  flywheelThickness: number;
  crankBolts: number;
  coverBolts: number;
  dampers: number;
  /** Arbre primaire, rayon sur les cannelures, et son nombre de cannelures. */
  shaftR: number;
  splines: number;
  /** Course de la fourchette : ce que le récepteur doit parcourir pour un millimètre de butée. */
  forkRatio: number;
}

export interface ClutchSizing {
  peakTorque: number;
  cylinders: number;
  displacementCc: number;
  boxFlywheel: 'single' | 'dual_mass';
  boxKind: string;
}

/** Diamètre tel que deux faces, à la pression admissible, tiennent la capacité du C++. */
export function discDiameterFor(peakTorque: number): number {
  const capacity = CAPACITY_MARGIN * Math.max(peakTorque, 40) * 1000;
  const k = 2 * LINING_MU * LINING_PRESSURE * ((Math.PI / 4) * (1 - LINING_RATIO * LINING_RATIO)) * ((1 + LINING_RATIO) / 4);
  return clamp(Math.cbrt(capacity / k), 170, 310);
}

/** Dimensionne l'embrayage pour ce moteur. Le couple donne le disque ; le disque, tout ce qu'il faut autour. */
export function clutchDimensions(sizing: ClutchSizing): ClutchDims {
  // Les vrais disques vont de cinq en cinq millimètres ; le modèle 3D ne se reconstruit qu'à ces marches.
  const discDiameter = Math.round(discDiameterFor(sizing.peakTorque) / 5) * 5;
  const liningOuterR = discDiameter / 2;
  const liningInnerR = liningOuterR * LINING_RATIO;
  const meanFrictionR = (liningOuterR + liningInnerR) / 2;
  const capacity = CAPACITY_MARGIN * Math.max(sizing.peakTorque, 40) * 1000;
  const fulcrumR = liningOuterR - 6;
  const pivotR = fulcrumR - 17 * (discDiameter / 228);
  const contactR = 24;
  const flywheelR = liningOuterR + 28;
  // Volant : plus épais quand il y a peu de cylindres, car il doit lisser des à-coups plus forts.
  const perCylinder = sizing.displacementCc / Math.max(sizing.cylinders, 1);
  const smoothing = Math.pow(4 / Math.max(sizing.cylinders, 2), 0.35);
  const flywheelThickness = Math.round(clamp(24 * smoothing * Math.pow(perCylinder / 500, 0.2), 20, 42));
  const dualMass = sizing.boxFlywheel === 'dual_mass' || sizing.boxKind === 'dual_clutch';
  const shaftR = Math.round(clamp(11 * Math.cbrt(Math.max(sizing.peakTorque, 80) / 250), 9.5, 13) * 2) / 2;
  return {
    kind: sizing.boxKind === 'dual_clutch' ? 'dual_clutch' : 'single',
    dualMass,
    discDiameter,
    liningOuterR,
    liningInnerR,
    meanFrictionR,
    clampLoad: capacity / (2 * LINING_MU * meanFrictionR),
    fulcrumR,
    pivotR,
    contactR,
    leverRatio: (pivotR - contactR) / (fulcrumR - pivotR),
    fingers: 18,
    flywheelR,
    ringTeeth: Math.round((2 * flywheelR) / 2.2),
    flywheelThickness: dualMass ? 48 : flywheelThickness,
    crankBolts: sizing.peakTorque > 320 || sizing.cylinders > 6 ? 8 : 6,
    coverBolts: discDiameter >= 250 ? 9 : 6,
    // Un disque sur volant bi-masse n'a pas de ressorts : le bi-masse amortit à sa place.
    dampers: dualMass ? 0 : discDiameter >= 215 ? 6 : 5,
    shaftR,
    splines: Math.round((shaftR * 2 * Math.PI) / 3.2),
    forkRatio: (FORK_SLAVE_R - FORK_STUD_R) / -FORK_STUD_R,
  };
}

/** L'état de la commande à une position de pédale : ce que chaque pièce a parcouru depuis la pédale relâchée. */
export interface ClutchTravel {
  /** Pédale, 0 relâchée, 1 au plancher. */
  pedal: number;
  /** Course de la tige du maître-cylindre et du piston du récepteur. */
  master: number;
  slave: number;
  /** Course de la butée vers le moteur (> 0), et angle de la fourchette (radians). */
  bearing: number;
  forkAngle: number;
  /** Vrai dès que la butée a rattrapé sa garde. */
  contact: boolean;
  /** Enfoncement du bout des doigts vers le moteur, et part de cet enfoncement due à leur flexion. */
  finger: number;
  flex: number;
  /** Angle dont le diaphragme a basculé (radians, positif : les doigts vers le moteur). */
  tilt: number;
  /** Recul du plateau, loin du moteur : 0 serré. */
  lift: number;
  /** Détente des segments (jusqu'à `CUSHION_TRAVEL`), puis jeu libre autour du disque. */
  cushion: number;
  gap: number;
  /** Part du serrage restante, 0 à 1. */
  clamp: number;
}

export const emptyTravel = (): ClutchTravel => ({ pedal: 0, master: 0, slave: 0, bearing: 0, forkAngle: 0, contact: false, finger: 0, flex: 0, tilt: 0, lift: 0, cushion: 0, gap: 0, clamp: 1 });

const hydraulicRatio = (MASTER_BORE * MASTER_BORE) / (SLAVE_BORE * SLAVE_BORE);

/** Course totale de la butée, pédale au plancher. */
export const fullBearingTravel = (dims: ClutchDims) => (PEDAL_TRAVEL / PEDAL_RATIO) * hydraulicRatio / dims.forkRatio;

/** La part de serrage restante pour un recul du plateau : tant que les segments n'ont pas fini de se détendre, il serre. */
export function clampShare(lift: number): number {
  if (lift <= 0) return 1;
  if (lift >= CUSHION_TRAVEL) return 0;
  return Math.pow(1 - lift / CUSHION_TRAVEL, CUSHION_LAW);
}

/** Recule le plateau selon l'enfoncement des doigts : tout le levier du diaphragme, moins la flexion. */
const liftFromFinger = (dims: ClutchDims, finger: number) => (finger * (1 - FINGER_FLEX)) / dims.leverRatio;

/** Toute la chaîne, dans l'ordre où la force la parcourt. Remplit `out`, n'alloue rien. */
export function travelFromPedal(dims: ClutchDims, pedal: number, out: ClutchTravel): ClutchTravel {
  const p = clamp(pedal, 0, 1);
  out.pedal = p;
  out.master = (p * PEDAL_TRAVEL) / PEDAL_RATIO;
  out.slave = out.master * hydraulicRatio;
  out.bearing = out.slave / dims.forkRatio;
  out.forkAngle = out.bearing / -FORK_STUD_R;
  out.finger = Math.max(0, out.bearing - BEARING_CLEARANCE);
  out.contact = out.bearing >= BEARING_CLEARANCE;
  out.flex = out.finger * FINGER_FLEX;
  out.tilt = (out.finger - out.flex) / (dims.pivotR - dims.contactR);
  out.lift = liftFromFinger(dims, out.finger);
  out.cushion = Math.min(out.lift, CUSHION_TRAVEL);
  out.gap = Math.max(0, out.lift - CUSHION_TRAVEL);
  out.clamp = clampShare(out.lift);
  return out;
}

/** La pédale qui donne ce recul de plateau, en remontant le levier, la fourchette et l'hydraulique. */
function pedalForLift(dims: ClutchDims, lift: number): number {
  const finger = (lift * dims.leverRatio) / (1 - FINGER_FLEX);
  return (finger + BEARING_CLEARANCE) / fullBearingTravel(dims);
}

/**
 * La pédale qui correspond à une part de couple de la simulation.
 * Sur une vraie voiture, le point de patinage occupe une petite part de la course : tout ce qui reste au-dessus
 * ferme la garde, tout ce qui reste en dessous ouvre le jeu. La simulation, elle, répartit sa part sur toute la pédale.
 */
export function pedalFromShare(dims: ClutchDims, share: number): number {
  const s = clamp(share, 0, 1);
  const liftOf = (value: number) => CUSHION_TRAVEL * (1 - Math.pow(value, 1 / CUSHION_LAW));
  if (s >= SHARE_CONTACT) return (pedalForLift(dims, 0) * (1 - s)) / (1 - SHARE_CONTACT);
  if (s > SHARE_OPEN) return pedalForLift(dims, liftOf(s));
  const start = pedalForLift(dims, liftOf(SHARE_OPEN));
  return start + ((1 - start) * (SHARE_OPEN - s)) / SHARE_OPEN;
}

/** Vrai si, à cette part de couple, la butée touche les doigts. Même frontière que `pedalFromShare`, sans les cotes. */
export const bearingTouches = (share: number) => share < SHARE_CONTACT;

/** L'angle dont les garnitures devancent le moyeu : le couple comprime les ressorts jusqu'à leur butée. */
export function damperWindup(torque: number, peakTorque: number): number {
  return clamp(torque / (Math.max(peakTorque, 40) * DAMPER_STOP_TORQUE), -1, 1) * DAMPER_LIMIT;
}

/** L'angle dont la masse primaire devance la secondaire, ressorts en arc comprimés par le même couple. */
export function dualMassWindup(torque: number, peakTorque: number): number {
  return clamp(torque / (Math.max(peakTorque, 40) * DUAL_MASS_STOP_TORQUE), -1, 1) * DUAL_MASS_LIMIT;
}

/**
 * Cinématique de la butée : une bille d'un roulement à bague intérieure tournante, sans glisser, fait tourner
 * sa cage à `(1 - γ) / 2` de la bague, avec γ = diamètre de bille / diamètre primitif.
 */
export const BEARING_BALL = 8;
export const BEARING_PITCH_R = 31;
export const CAGE_RATIO = (1 - BEARING_BALL / (2 * BEARING_PITCH_R)) / 2;

/** La bague tournante, ce qu'il en reste de la butée d'une image à l'autre. */
export interface RaceState {
  /** Vitesse de la bague, tr/min. Elle n'est pas celle du volant : elle la rattrape, puis la quitte. */
  rpm: number;
  angle: number;
  cageAngle: number;
}

/**
 * Fait avancer la bague d'un pas de temps : sa propre vitesse en temps réel, ralentie comme tout le reste pour
 * qu'on la voie. Le frottement sur les doigts dure quelques dizaines de millisecondes ; au ralenti, son lancer aussi.
 */
export function advanceRace(state: RaceState, contact: boolean, flywheelRpm: number, seconds: number, slowMotion: number, frozen: boolean): void {
  const target = contact ? flywheelRpm : 0;
  const tau = (contact ? RACE_GRIP_TAU : RACE_COAST_TAU) * slowMotion;
  state.rpm += (target - state.rpm) * (1 - Math.exp(-seconds / tau));
  if (!contact && Math.abs(state.rpm) < RACE_STOP_RPM) state.rpm = 0;
  if (frozen) return;
  const step = ((state.rpm / 60 / slowMotion) * 2 * Math.PI * seconds);
  state.angle = (state.angle + step) % (Math.PI * 2);
  state.cageAngle = (state.cageAngle + step * CAGE_RATIO) % (Math.PI * 2);
}

/**
 * Double embrayage sec : **normalement ouvert**, l'inverse de l'embrayage simple. Si la commande lâche, les deux s'ouvrent.
 * Les vitesses impaires passent par K1 (arbre plein), les paires et la marche arrière par K2 (arbre creux).
 */
export function activeClutch(gear: number): 0 | 1 | 2 {
  if (gear === 0) return 0;
  return gear > 0 && gear % 2 === 1 ? 1 : 2;
}

/** Course d'engagement de chacune des deux butées de double embrayage : serrer, c'est pousser. */
export const DCT_ENGAGE_TRAVEL = 7;
export const dctBearingTravel = (share: number) => DCT_ENGAGE_TRAVEL * Math.pow(clamp(share, 0, 1), 0.7);

/** Tout ce qu'il faut pour dessiner une image : même calcul pour la coupe 2D et le modèle 3D. */
export interface ClutchFrame {
  travel: ClutchTravel;
  /** Angles : vilebrequin, masse qui porte l'embrayage (le volant, ou la secondaire du bi-masse), moyeu, garnitures. */
  crankAngle: number;
  massAngle: number;
  hubAngle: number;
  liningAngle: number;
  /** Retard de la masse secondaire et avance des garnitures sur le moyeu (radians). */
  dualMassWindup: number;
  damperWindup: number;
  raceAngle: number;
  cageAngle: number;
  raceRpm: number;
  /** Double embrayage : part serrée de chacun, course de sa butée, recul de son plateau, angle de son disque. */
  k1: number;
  k2: number;
  k1Travel: number;
  k2Travel: number;
  k1Open: number;
  k2Open: number;
  k1Angle: number;
  k2Angle: number;
}

export const emptyFrame = (): ClutchFrame => ({ travel: emptyTravel(), crankAngle: 0, massAngle: 0, hubAngle: 0, liningAngle: 0, dualMassWindup: 0, damperWindup: 0, raceAngle: 0, cageAngle: 0, raceRpm: 0, k1: 0, k2: 0, k1Travel: 0, k2Travel: 0, k1Open: 1, k2Open: 1, k1Angle: 0, k2Angle: 0 });

/** Ce que la vue lit de l'horloge commune : `Motion`, sans en dépendre. */
export interface ClutchClock {
  clutchShare: number;
  clutchTorque: number;
  flywheelAngle: number;
  discAngle: number;
  clutchK1: number;
  clutchK2: number;
  /** Disque en attente d'un double embrayage : il suit le primaire en service, au rapport des deux vitesses. */
  boxInputTotal: number;
  releaseRace: RaceState;
}

/** Recul de chaque plateau d'un double embrayage ouvert (mm), à l'échelle réelle. */
export const DCT_OPEN_LIFT = 1.4;

/** Lit l'horloge et remplit l'image. N'alloue rien. `idleRatio` : vitesse de l'arbre qui attend, par rapport au primaire en service. */
export function readFrame(dims: ClutchDims, peakTorque: number, clock: ClutchClock, activeK: 0 | 1 | 2, idleRatio: number, out: ClutchFrame): ClutchFrame {
  travelFromPedal(dims, pedalFromShare(dims, clock.clutchShare), out.travel);
  out.dualMassWindup = dims.dualMass ? dualMassWindup(clock.clutchTorque, peakTorque) : 0;
  out.damperWindup = dims.dampers > 0 ? damperWindup(clock.clutchTorque, peakTorque) : 0;
  out.crankAngle = clock.flywheelAngle;
  out.massAngle = clock.flywheelAngle - out.dualMassWindup;
  out.hubAngle = clock.discAngle;
  out.liningAngle = clock.discAngle + out.damperWindup;
  out.raceAngle = clock.releaseRace.angle;
  out.cageAngle = clock.releaseRace.cageAngle;
  out.raceRpm = clock.releaseRace.rpm;
  out.k1 = clock.clutchK1;
  out.k2 = clock.clutchK2;
  out.k1Travel = dctBearingTravel(out.k1);
  out.k2Travel = dctBearingTravel(out.k2);
  out.k1Open = 1 - clampShareInverse(out.k1);
  out.k2Open = 1 - clampShareInverse(out.k2);
  const idle = (clock.boxInputTotal * idleRatio) % (Math.PI * 2);
  out.k1Angle = activeK === 2 ? idle : clock.discAngle;
  out.k2Angle = activeK === 1 ? idle : clock.discAngle;
  return out;
}

/** Plateau de double embrayage : il rejoint le disque dès les premières parts de serrage. */
const clampShareInverse = (share: number) => Math.min(1, share / 0.15);
