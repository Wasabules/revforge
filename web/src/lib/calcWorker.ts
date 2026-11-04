/**
 * Les calculs longs de la fiche, hors du thread de la page : un troisième exemplaire du même WASM.
 *
 * - Le 0 à 100 km/h conduit la voiture jusqu'à 100 km/h, et jusqu'à neuf fois quand les roues patinent (l'essayeur de
 *   `measurePerformance`) : de 0,1 à 0,6 s de calcul.
 * - La fiche acoustique joue le son du réglage, quatre fois : près d'une seconde.
 * Sur le thread de la page, chaque réglage figeait l'écran autant. Ici, la page ne les attend jamais : elle envoie les arguments
 * déjà calculés par `config.ts` (les mêmes que pour la fiche et le son), et reçoit les chiffres. Ce sont ceux du même calcul, au
 * bit près.
 */

interface CalcExports {
  _initialize?: () => void;
  rf_init(sampleRate: number): void;
  rf_set_car(...args: number[]): void;
  rf_set_gear_ratio(gear: number, ratio: number): void;
  rf_set_gearbox(...args: number[]): void;
  rf_set_tuning(...args: number[]): void;
  rf_set_electric(): void;
  rf_set_config(...args: number[]): void;
  rf_zero_to_100(): number;
  rf_sheet_level(item: number): number;
  rf_sheet_line_loss(fromHz: number, toHz: number): number;
}

/** Le réglage, tel que `Engine.applyLocal` le pose. */
export interface CalcSetup {
  car: number[];
  ratios: number[];
  gearbox: number[];
  tuning: number[];
  config: number[];
  electric: boolean;
}

/** Ce que la page envoie : le module une fois, puis une demande à la fois. */
export type CalcRequest = { type: 'load'; bytes: ArrayBuffer } | ({ type: 'sprint' | 'sheet'; bands?: number[] } & CalcSetup);

/** Ce que le worker rend : le 0 à 100 (s), ou les niveaux de la fiche acoustique puis les pertes de la ligne bande par bande. */
export interface CalcAnswer {
  values: number[];
}

const scope = self as unknown as { onmessage: ((event: MessageEvent<CalcRequest>) => void) | null; postMessage(message: CalcAnswer): void };
let rf: CalcExports | null = null;
let ready: Promise<void> | null = null;

scope.onmessage = ({ data }) => {
  if (data.type === 'load') {
    ready = WebAssembly.instantiate(data.bytes, {}).then(({ instance }) => {
      rf = instance.exports as unknown as CalcExports;
      rf._initialize?.();
      rf.rf_init(44100);
    });
    return;
  }
  void ready?.then(() => {
    if (!rf) return;
    // Dans le même ordre que `Engine.applyLocal` : la même voiture, donc les mêmes chiffres.
    rf.rf_set_car(...data.car);
    data.ratios.forEach((ratio, i) => rf!.rf_set_gear_ratio(i + 1, ratio));
    rf.rf_set_gearbox(...data.gearbox);
    rf.rf_set_tuning(...data.tuning);
    if (data.electric) rf.rf_set_electric();
    else rf.rf_set_config(...data.config);
    if (data.type === 'sprint') {
      scope.postMessage({ values: [rf.rf_zero_to_100()] });
      return;
    }
    const levels = [0, 1, 2, 3].map((item) => rf!.rf_sheet_level(item));
    const losses = (data.bands ?? []).map((hz) => rf!.rf_sheet_line_loss(hz / Math.SQRT2, hz * Math.SQRT2));
    scope.postMessage({ values: [...levels, ...losses] });
  });
};
