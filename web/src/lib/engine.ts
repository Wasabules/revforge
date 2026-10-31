import { configArgs, type EngineConfig } from './config';

export const Mode = { Off: 0, Neutral: 1, Drive: 2, Lap: 3, Manual: 4 } as const;
export type ModeValue = (typeof Mode)[keyof typeof Mode];

export interface Telemetry {
  rpm: number;
  carRpm: number;
  speed: number;
  gear: number;
  throttle: number;
  load: number;
  boost: number;
  peak: number;
}

export const IDLE_TELEMETRY: Telemetry = { rpm: 0, carRpm: 0, speed: 0, gear: 0, throttle: 0, load: 0, boost: 0, peak: 0 };

interface RfExports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  rf_init(rate: number): void;
  rf_buffer(): number;
  rf_render(frames: number): void;
  rf_set_config(...args: number[]): void;
  rf_set_electric(): void;
  rf_set_mode(mode: number): void;
  rf_set_manual_rpm(rpm: number): void;
  rf_set_volume(volume: number): void;
  rf_from_car_rpm(rpm: number): number;
  rf_idle_rpm(): number;
  rf_multiplier(): number;
}

const SILENCE_BEFORE_SUSPEND_MS = 400;

/** Le moteur : un exemplaire dans l'AudioWorklet pour le son, un autre ici pour les calculs. */
export class Engine {
  analyser: AnalyserNode | null = null;
  private context: AudioContext | null = null;
  private node: AudioWorkletNode | null = null;
  private bytes: ArrayBuffer | null = null;
  private local: RfExports | null = null;
  private suspendTimer: ReturnType<typeof setTimeout> | undefined;

  constructor(private onTelemetry: (t: Telemetry) => void, private onError: (message: string) => void) {}

  async load(): Promise<void> {
    const response = await fetch(`${import.meta.env.BASE_URL}revforge.wasm`);
    if (!response.ok) throw new Error(`revforge.wasm : ${response.status}`);
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
    this.setMode(mode);
  }

  stop(): void {
    this.setMode(Mode.Off);
    clearTimeout(this.suspendTimer);
    this.suspendTimer = setTimeout(() => void this.context?.suspend(), SILENCE_BEFORE_SUSPEND_MS);
  }

  private onMessage(message: { type: string; message?: string } & Partial<Telemetry>): void {
    if (message.type === 'telemetry') this.onTelemetry(message as Telemetry);
    else if (message.type === 'error') this.onError(message.message ?? 'Erreur du moteur');
  }

  private send(command: Record<string, unknown>): void {
    this.node?.port.postMessage(command);
  }

  setConfig(config: EngineConfig): void {
    if (config.electric) this.send({ type: 'electric' });
    else this.send({ type: 'config', args: configArgs(config) });
  }

  setMode(mode: ModeValue): void {
    this.send({ type: 'mode', mode });
  }

  setControls(pedal: number, brake: number): void {
    this.send({ type: 'controls', pedal, brake });
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

  /** Régime moteur que donnerait un régime voiture, avec la vraie courbe du moteur. */
  preview(config: EngineConfig, carRpms: number[]): { idle: number; multiplier: number; rpms: number[] } {
    const rf = this.local;
    if (!rf) return { idle: 0, multiplier: 0, rpms: carRpms.map(() => 0) };
    if (config.electric) rf.rf_set_electric();
    else rf.rf_set_config(...configArgs(config));
    return { idle: rf.rf_idle_rpm(), multiplier: rf.rf_multiplier(), rpms: carRpms.map((rpm) => rf.rf_from_car_rpm(rpm)) };
  }

  /** Rend le moteur en WAV 16 bits, plus vite que le temps réel. */
  renderWav(config: EngineConfig, scenario: 'lap' | 'blips'): Blob {
    const rf = this.local;
    if (!rf) throw new Error('Moteur non chargé');
    const rate = 44100;
    const seconds = scenario === 'lap' ? 60 : 14;
    const tick = rate / 20;
    rf.rf_init(rate);
    if (config.electric) rf.rf_set_electric();
    else rf.rf_set_config(...configArgs(config));
    rf.rf_set_volume(0.7);
    const preview = this.preview(config, [6200]);
    const top = preview.idle + (config.electric ? 12000 - preview.idle : config.redlineRpm - preview.idle) * 0.85;
    rf.rf_set_mode(scenario === 'lap' ? Mode.Lap : Mode.Manual);

    const pcm = new Int16Array(rate * seconds);
    let step = 0;
    for (let done = 0; done < pcm.length; done += tick) {
      if (scenario === 'blips') {
        const inBlip = ((step - 20) % 80 + 80) % 80;
        let target = preview.idle;
        if (step >= 20) {
          if (inBlip < 16) target = preview.idle + (top - preview.idle) * (inBlip / 16);
          else if (inBlip < 24) target = top;
          else if (inBlip < 54) target = preview.idle + (top - preview.idle) * (1 - (inBlip - 24) / 30) ** 2;
        }
        rf.rf_set_manual_rpm(target);
      }
      const frames = Math.min(tick, pcm.length - done);
      rf.rf_render(frames);
      const block = new Float32Array(rf.memory.buffer, rf.rf_buffer(), frames);
      for (let i = 0; i < frames; i++) pcm[done + i] = Math.max(-32768, Math.min(32767, Math.trunc(block[i] * 32767)));
      step++;
    }
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
  header.setUint16(22, 1, true);
  header.setUint32(24, rate, true);
  header.setUint32(28, rate * 2, true);
  header.setUint16(32, 2, true);
  header.setUint16(34, 16, true);
  text(36, 'data');
  header.setUint32(40, pcm.byteLength, true);
  return new Blob([header, pcm as BlobPart], { type: 'audio/wav' });
}
