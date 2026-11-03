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
    this.pointerRight = this.rf.rf_buffer_right();
    this.untilTelemetry = 0;
    this.peak = 0;
    this.port.onmessage = ({ data }) => this.onCommand(data);
    this.port.postMessage({ type: 'ready' });
  }

  onCommand(command) {
    const { rf } = this;
    switch (command.type) {
      // Les réglages du socle d'abord : rf_set_tuning les pose, rf_set_config ou rf_set_electric les applique.
      case 'config': rf.rf_set_tuning(...command.tuning); rf.rf_set_config(...command.args); break;
      case 'electric': rf.rf_set_tuning(...command.tuning); rf.rf_set_electric(); break;
      case 'mode': rf.rf_set_mode(command.mode); break;
      case 'car': rf.rf_set_car(...command.args); break;
      case 'gearbox':
        command.ratios.forEach((ratio, i) => rf.rf_set_gear_ratio(i + 1, ratio));
        rf.rf_set_gearbox(...command.args);
        break;
      case 'assist': rf.rf_set_clutch_assist(command.assisted ? 1 : 0); break;
      case 'repair': rf.rf_repair_gearbox(); break;
      case 'resetTrip': rf.rf_reset_trip(); break;
      case 'controls': rf.rf_set_controls(command.pedal, command.brake, command.clutch); break;
      case 'transmission': rf.rf_set_transmission(command.manual ? 1 : 0); break;
      case 'shiftMode': rf.rf_set_shift_mode(command.mode); break;
      case 'scenario': rf.rf_set_scenario(command.scenario); break;
      case 'shift': rf.rf_shift(command.direction); break;
      case 'crank': rf.rf_crank(); break;
      case 'listener': rf.rf_set_listener(command.listener); break;
      case 'listenerAt': rf.rf_set_listener_position(command.x, command.y, command.z); break;
      case 'windows': rf.rf_set_windows(command.open ? 1 : 0); break;
      case 'volume': rf.rf_set_volume(command.volume); break;
      default: break;
    }
  }

  process(_inputs, outputs) {
    const { rf } = this;
    if (!rf) return false;
    const output = outputs[0];
    const frames = output[0].length;
    rf.rf_render_stereo(frames);
    if (!this.view || this.view.length !== frames) {
      this.view = new Float32Array(rf.memory.buffer, this.pointer, frames);
      this.viewRight = new Float32Array(rf.memory.buffer, this.pointerRight, frames);
    }
    output[0].set(this.view);
    output[1]?.set(this.viewRight);
    for (let i = 0; i < frames; i++) {
      const level = Math.max(Math.abs(this.view[i]), Math.abs(this.viewRight[i]));
      if (level > this.peak) this.peak = level;
    }

    this.untilTelemetry -= frames;
    if (this.untilTelemetry <= 0) {
      this.untilTelemetry = sampleRate / TELEMETRY_HZ;
      this.port.postMessage({
        type: 'telemetry',
        rpm: rf.rf_rpm(),
        speed: rf.rf_speed(),
        gear: rf.rf_gear(),
        throttle: rf.rf_throttle(),
        load: rf.rf_load(),
        boost: rf.rf_boost(),
        turbine: rf.rf_turbine(),
        turboRelease: rf.rf_turbo_release(),
        turboRpm: rf.rf_turbo_rpm(),
        turboCharge: rf.rf_turbo_charge(),
        turboPlenum: rf.rf_turbo_plenum(),
        turboOutlet: rf.rf_turbo_outlet(),
        turboInlet: rf.rf_turbo_inlet(),
        turboExhaust: rf.rf_turbo_exhaust(),
        turboWastegate: rf.rf_turbo_wastegate(),
        turboBlowoff: rf.rf_turbo_blowoff(),
        turboFlow: rf.rf_turbo_flow(),
        turboOutC: rf.rf_turbo_out_c(),
        turboCoolC: rf.rf_turbo_cool_c(),
        intakeAirC: rf.rf_intake_air_c(rf.rf_rpm()),
        turboGasC: rf.rf_turbo_gas_c(),
        antiLag: rf.rf_antilag_active(),
        antiLagHeatKw: rf.rf_antilag_heat_kw(),
        antiLagAirFlow: rf.rf_antilag_air_flow(),
        antiLagLostNm: rf.rf_antilag_lost_nm(),
        chargerRpm: rf.rf_supercharger_rpm(),
        chargerKw: rf.rf_supercharger_kw(),
        chargerSpill: rf.rf_supercharger_spill(),
        torque: rf.rf_torque(),
        power: rf.rf_power(),
        clutch: rf.rf_clutch(),
        clutchInputRpm: rf.rf_clutch_input_rpm(),
        clutchSlipKw: rf.rf_clutch_slip_kw(),
        clutchTorque: rf.rf_clutch_torque(),
        wheelRpm: rf.rf_wheel_rpm(),
        slip: rf.rf_slip(),
        pops: rf.rf_pops(),
        cut: rf.rf_cut(),
        shifting: rf.rf_shifting(),
        cranking: rf.rf_cranking(),
        stalled: rf.rf_stalled(),
        lugging: rf.rf_lugging(),
        legKind: rf.rf_leg_kind(),
        legTarget: rf.rf_leg_target(),
        gradePct: rf.rf_grade_pct(),
        underpowered: rf.rf_underpowered(),
        levelDbA: rf.rf_level_dba(),
        listenerX: rf.rf_listener_x(),
        listenerY: rf.rf_listener_y(),
        listenerZ: rf.rf_listener_z(),
        outputRpm: rf.rf_output_rpm(),
        meshed: rf.rf_meshed(),
        shiftStep: rf.rf_shift_step(),
        grinding: rf.rf_grinding(),
        overload: rf.rf_overload(),
        boxWear: rf.rf_box_wear(),
        boxBroken: rf.rf_box_broken(),
        boxPending: rf.rf_box_pending(),
        boxConeSlip: rf.rf_box_cone_slip(),
        boxRattle: rf.rf_box_rattle(),
        boxLoose0: rf.rf_box_loose(0),
        boxLoose1: rf.rf_box_loose(1),
        boxLoose2: rf.rf_box_loose(2),
        boxLoose3: rf.rf_box_loose(3),
        fuelLph: rf.rf_fuel_lph(),
        fuelLper100km: rf.rf_fuel_l_per_100km(),
        fuelTripL: rf.rf_fuel_trip_l(),
        fuelAvgLper100km: rf.rf_fuel_avg_l_per_100km(),
        tripKm: rf.rf_trip_km(),
        co2GPerKm: rf.rf_co2_g_per_km(),
        oilGph: rf.rf_oil_gph(),
        oilTripMl: rf.rf_oil_trip_ml(),
        oilMlPer1000km: rf.rf_oil_ml_per_1000km(),
        energyKwhPer100km: rf.rf_energy_kwh_per_100km(),
        energyAvgKwhPer100km: rf.rf_energy_avg_kwh_per_100km(),
        energyTripKwh: rf.rf_energy_trip_kwh(),
        coolantC: rf.rf_coolant_c(),
        oilC: rf.rf_oil_c(),
        metalC: rf.rf_metal_c(),
        thermostat: rf.rf_thermostat(),
        fan: rf.rf_fan(),
        oilViscosity: rf.rf_oil_viscosity(),
        coldFrictionNm: rf.rf_cold_friction_nm(),
        coldIdleRpm: rf.rf_cold_idle_rpm(),
        coldEnrichment: rf.rf_cold_enrichment(),
        warmFrictionNm: rf.rf_warm_friction_nm(rf.rf_rpm()),
        sparkAdvance: rf.rf_spark_advance(),
        knockLimit: rf.rf_knock_limit(),
        knockRetard: rf.rf_knock_retard(),
        knockShare: rf.rf_knock_share(),
        ecuAdvance: rf.rf_ecu_advance(),
        ecuLambda: rf.rf_ecu_lambda(),
        ecuFired: rf.rf_ecu_fired(),
        ecuLimiter: rf.rf_ecu_limiter(),
        ecuLaunch: rf.rf_ecu_launch(),
        ecuFlat: rf.rf_ecu_flat(),
        ecuTraction: rf.rf_ecu_traction(),
        dpfLoad: rf.rf_dpf_load(),
        dpfKpa: rf.rf_dpf_kpa(),
        dpfRegen: rf.rf_dpf_regen(),
        dpfC: rf.rf_dpf_c(),
        egrShare: rf.rf_egr_share(),
        camHigh: rf.rf_cam_high(),
        camPhaser: rf.rf_cam_phaser(),
        peak: this.peak,
      });
      this.peak = 0;
    }
    return true;
  }
}

registerProcessor('revforge', RevforgeProcessor);
