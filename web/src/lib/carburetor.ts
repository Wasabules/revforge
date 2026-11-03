/**
 * Le carburateur vu par le dessin et par le gaz à l'écran : la règle de dimensionnement de `carburetorSpecOf` (C++), recopiée.
 * Une erreur ici ne change rien au son ni au couple, seulement ce qu'on voit.
 */
import type { EngineConfig } from './config';

export interface CarbGeometry {
  kind: 'single' | 'multi';
  /** Corps de carburateur. */
  throats: number;
  /** Diamètre au col (m). */
  throatM: number;
  /** Diamètre de la pipe (m). */
  pipeM: number;
}

const RATED_SHARE = 0.88;
const SHARED_CHOKE = 10;
const PER_CYLINDER_CHOKE = 20;
const PIPE_OVER_THROAT = 1.25;

/** `null` : pas de carburateur sur ce moteur (injection, diesel ou électrique). */
export function carburetorOf(config: EngineConfig): CarbGeometry | null {
  if (config.electric || config.fuel === 'diesel' || !config.fuelSystem || config.fuelSystem === 'injection') return null;
  const litres = config.displacementCc / 1000;
  const krpm = (RATED_SHARE * config.redlineRpm) / 1000;
  const shared = config.fuelSystem === 'single';
  const throatM = (shared ? SHARED_CHOKE * Math.sqrt(litres * krpm) : PER_CYLINDER_CHOKE * Math.sqrt((litres / config.cylinders) * krpm)) / 1000;
  return { kind: shared ? 'single' : 'multi', throats: shared ? 1 : config.cylinders, throatM, pipeM: throatM * PIPE_OVER_THROAT };
}
