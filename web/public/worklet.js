// Le moteur tourne ici, hors du thread de la page : l'interface ne peut pas le faire décrocher.
const TELEMETRY_HZ = 30;

class RevforgeProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    try {
      const module = new WebAssembly.Module(options.processorOptions.wasm);
      this.rf = new WebAssembly.Instance(module, {}).exports;
      this.rf._initialize?.();
      this.rf.rf_init(sampleRate);
    } catch (error) {
      this.port.postMessage({ type: 'error', message: String(error) });
      return;
    }
    this.pointer = this.rf.rf_buffer();
    this.untilTelemetry = 0;
    this.peak = 0;
    this.port.onmessage = ({ data }) => this.onCommand(data);
    this.port.postMessage({ type: 'ready' });
  }

  onCommand(command) {
    const { rf } = this;
    switch (command.type) {
      case 'config': rf.rf_set_config(...command.args); break;
      case 'electric': rf.rf_set_electric(); break;
      case 'mode': rf.rf_set_mode(command.mode); break;
      case 'controls': rf.rf_set_controls(command.pedal, command.brake); break;
      case 'volume': rf.rf_set_volume(command.volume); break;
      default: break;
    }
  }

  process(_inputs, outputs) {
    const { rf } = this;
    if (!rf) return false;
    const output = outputs[0];
    const frames = output[0].length;
    rf.rf_render(frames);
    if (!this.view || this.view.length !== frames) {
      this.view = new Float32Array(rf.memory.buffer, this.pointer, frames);
    }
    for (const channel of output) channel.set(this.view);
    for (let i = 0; i < frames; i++) {
      const level = Math.abs(this.view[i]);
      if (level > this.peak) this.peak = level;
    }

    this.untilTelemetry -= frames;
    if (this.untilTelemetry <= 0) {
      this.untilTelemetry = sampleRate / TELEMETRY_HZ;
      this.port.postMessage({
        type: 'telemetry',
        rpm: rf.rf_rpm(),
        carRpm: rf.rf_car_rpm(),
        speed: rf.rf_speed(),
        gear: rf.rf_gear(),
        throttle: rf.rf_throttle(),
        load: rf.rf_load(),
        boost: rf.rf_boost(),
        peak: this.peak,
      });
      this.peak = 0;
    }
    return true;
  }
}

registerProcessor('revforge', RevforgeProcessor);
