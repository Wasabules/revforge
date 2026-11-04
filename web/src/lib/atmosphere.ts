/**
 * L'air du jour, comme le C++ le calcule (`core/src/atmosphere.cpp`) : pression de l'atmosphère type (ISA), température dans
 * le kelvin du modèle (293 à 20 °C), gaz parfait. Dans l'air de référence, 20 °C au niveau de la mer, chaque grandeur vaut
 * exactement l'ancienne constante du gaz à l'écran. Rien ici n'agit sur le son : le gaz à l'écran et les jauges seulement.
 */

export const REFERENCE_PA = 101325;
export const REFERENCE_K = 293;
export const REFERENCE_C = 20;
const AIR_R = 287;
const ISA_LAPSE = 2.25577e-5;
const ISA_EXPONENT = 5.25588;

export interface AmbientAir {
  celsius: number;
  altitudeM: number;
  /** Pa. */
  pressurePa: number;
  /** K, kelvin du modèle. */
  temperatureK: number;
  /** kg/m³. */
  densityKgM3: number;
  /** m/s : 343 à 20 °C. */
  soundSpeedMs: number;
}

const clamp = (v: number, low: number, high: number): number => (v < low ? low : v > high ? high : v);

/** Pression de l'atmosphère type à cette altitude (Pa) : 101 325·(1 − 2,25577·10⁻⁵·h)^5,25588. */
export function isaPressurePa(altitudeM: number): number {
  const h = clamp(altitudeM, -500, 11000);
  return REFERENCE_PA * Math.pow(1 - ISA_LAPSE * h, ISA_EXPONENT);
}

/** L'air de ce réglage. Les champs absents (un vieux lien) valent l'air de référence. */
export function ambientAirOf(config: { ambientC?: number; altitudeM?: number }): AmbientAir {
  const celsius = clamp(config.ambientC ?? REFERENCE_C, -60, 60);
  const altitudeM = clamp(config.altitudeM ?? 0, -500, 11000);
  const pressurePa = isaPressurePa(altitudeM);
  const temperatureK = REFERENCE_K + (celsius - REFERENCE_C);
  return {
    celsius,
    altitudeM,
    pressurePa,
    temperatureK,
    densityKgM3: pressurePa / (AIR_R * temperatureK),
    soundSpeedMs: 343 * Math.sqrt(temperatureK / REFERENCE_K),
  };
}
