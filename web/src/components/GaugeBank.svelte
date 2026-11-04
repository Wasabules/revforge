<script lang="ts">
  import { onMount } from 'svelte';
  import Gauge from './Gauge.svelte';
  import { animate } from '../lib/canvas';
  import type { EngineConfig } from '../lib/config';
  import { KW_TO_HP, type Spec, type Telemetry } from '../lib/engine';
  import { fixed, lang, t, whole } from '../lib/i18n.svelte';
  import { InstrumentClock, niceStep, type Readings } from '../lib/instruments';
  import type { Motion } from '../lib/motion';

  interface Props {
    config: EngineConfig;
    spec: Spec;
    motion: Motion;
    readings: Readings;
    telemetry: () => Telemetry;
    running: () => boolean;
    /** Lit la simulation pour cette image. `gas` : ces jauges lisent le modèle du gaz, il doit avancer. */
    sample: (now: number, gas: boolean) => void;
    /** Remplir la bouteille de protoxyde. */
    onrefill?: () => void;
  }
  let { config, spec, motion, readings, telemetry, running, sample, onrefill }: Props = $props();

  const clock = new InstrumentClock();
  let root: HTMLElement;
  let turbineNode = $state<HTMLElement>();
  let tailpipeNode = $state<HTMLElement>();
  let intakeNode = $state<HTMLElement>();
  let coolantNode = $state<HTMLElement>();
  let oilNode = $state<HTMLElement>();
  let filterNode = $state<HTMLElement>();
  let camNode = $state<HTMLElement>();
  let dutyNode = $state<HTMLElement>();
  let chargeNode = $state<HTMLElement>();
  let bottleNode = $state<HTMLElement>();

  const turbo = $derived(config.aspiration === 'turbo' && !config.electric);
  // L'air d'admission n'a d'intérêt que s'il n'est plus celui du dehors : un filtre sous le capot.
  const hotAir = $derived(!config.electric && (config.airFilter === 'cone' || config.airFilter === 'stacks'));
  const charged = $derived(config.aspiration === 'supercharger' && !config.electric);
  // Une électrovanne n'existe que sur une régulation électronique : la wastegate à ressort n'a rien à afficher là.
  const solenoid = $derived(turbo && config.boostControl !== 'spring');
  const boosted = $derived(turbo || charged);
  // Le filtre à particules : sur un diesel ou une injection directe d'essence, comme le calcul (`particulateFiltered`).
  const filtered = $derived(!config.electric && config.particulateFilter && (config.fuel === 'diesel' || config.fuelSystem === 'direct') && config.layout !== 'rotary');
  // Une distribution variable : le profil en prise et le calage du déphaseur, tels que le modèle les donne.
  const twoStep = $derived(!config.electric && config.layout !== 'rotary' && config.cycle !== 'two_stroke' && (config.valveTiming === 'two_step' || config.valveTiming === 'phased_two_step'));
  const phased = $derived(!config.electric && config.layout !== 'rotary' && config.cycle !== 'two_stroke' && (config.valveTiming === 'phased' || config.valveTiming === 'phased_two_step'));
  /** L'air de charge, quand il n'est plus celui de l'échangeur d'origine : un autre échangeur, l'eau, le protoxyde. Pas sur un diesel. */
  const spark = $derived(config.layout === 'rotary' || config.fuel !== 'diesel');
  const nitrous = $derived(!config.electric && spark && config.nitrousKw > 0);
  const chargeShown = $derived(!config.electric && ((boosted && config.intercooler !== 'air_to_air') || (spark && (config.waterInjection || config.nitrousKw > 0))));
  /** Une échelle qui finit sur une graduation : tout ce que la simulation peut atteindre y tient. */
  const roundedUp = (value: number, step: number) => Math.ceil(value / step - 1e-9) * step;

  const speedStep = $derived(niceStep(Math.max(spec.topSpeed, 60), 6));
  const speedMax = $derived(roundedUp(Math.max(spec.topSpeed, 60), speedStep));
  const manifoldStep = $derived(boosted && config.boostBar >= 2 ? 1 : 0.5);
  // De la dépression totale à un peu plus que la pression réglée ; sans turbo, l'atmosphère est presque le bout.
  const manifoldMax = $derived(boosted ? roundedUp(config.boostBar * 1.1, 0.5) : 0.5);
  // Sans turbine, un compresseur laisse la ligne libre, mais tout l'air qu'il a poussé doit bien sortir.
  const backMax = $derived(turbo ? roundedUp(Math.max(config.boostBar, 1), 0.5) : charged ? 1 : 0.6);
  const backStep = $derived(turbo ? 0.5 : 0.2);
  const powerStep = $derived(niceStep(Math.max(spec.peakHp, 50), 5));
  const powerMax = $derived(roundedUp(Math.max(spec.peakHp, 50) * 1.05, powerStep));

  /** La vitesse de l'arbre si la télémétrie la donne, sinon l'estimation de `Motion`. */
  const turbineRpm = () => (charged ? telemetry().chargerRpm : (telemetry() as Telemetry & { turbineRpm?: number }).turbineRpm ?? motion.turbineRpm(config));
  /** Deux turbos : les deux arbres, chacun à sa vitesse. */
  const twin = $derived(turbo && (config.turboLayout === 'twin_parallel' || config.turboLayout === 'twin_sequential'));

  onMount(() => {
    const stop = animate(root, (_seconds, now) => {
      motion.meterWanted = true;
      sample(now, true);
      clock.run(now);
    }, false);
    let lastTurbine = NaN;
    let lastLang = lang();
    let lastTailpipe = NaN;
    let lastTailLang = lang();
    let lastIntake = NaN;
    let lastIntakeLang = lang();
    let lastCoolant = '';
    let lastOil = '';
    let lastFilter = '';
    let lastCam = '';
    let lastDuty = NaN;
    let lastDutyLang = lang();
    let lastCharge = NaN;
    let lastChargeLang = lang();
    let lastBottle = '';
    const stops = [
      clock.add((text) => {
        if (!text) return;
        if (!turbineNode) return;
        const rpm = Math.round(turbineRpm() / 1000);
        const second = twin ? Math.round(motion.turbo2Rpm / 1000) : 0;
        if (rpm + second * 1e4 === lastTurbine && lang() === lastLang) return;
        lastTurbine = rpm + second * 1e4;
        lastLang = lang();
        turbineNode.textContent = twin ? t('gauge.turbines', { a: whole(rpm * 1000), b: whole(second * 1000) }) : t(charged ? 'gauge.charger' : 'gauge.turbine', { rpm: whole(rpm * 1000) });
      }),
      clock.add((text) => {
        if (!text) return;
        if (!tailpipeNode) return;
        const c = Math.round(readings.tailpipeC);
        if (c === lastTailpipe && lang() === lastTailLang) return;
        lastTailpipe = c;
        lastTailLang = lang();
        tailpipeNode.textContent = t('gauge.tailpipe', { c: whole(c) });
      }),
      clock.add((text) => {
        if (!text) return;
        if (!intakeNode) return;
        const c = Math.round(running() ? telemetry().intakeAirC : 20);
        if (c === lastIntake && lang() === lastIntakeLang) return;
        lastIntake = c;
        lastIntakeLang = lang();
        intakeNode.textContent = t('gauge.intakeAir', { c: whole(c) });
      }),
      // L'eau : le calorstat, le ventilateur, et ce que le calculateur fait d'un moteur froid (ralenti visé, carburant en plus).
      clock.add((text) => {
        if (!text || !coolantNode) return;
        const live = running() ? telemetry() : null;
        const lift = live ? Math.round(live.thermostat * 100) : 0;
        const fan = live ? live.fan : 0;
        const idle = live ? Math.round(live.coldIdleRpm / 10) * 10 : 0;
        const rich = live ? Math.round(live.coldEnrichment * 100) : 0;
        const key = `${lift}|${fan}|${idle}|${rich}|${lang()}`;
        if (key === lastCoolant) return;
        lastCoolant = key;
        let line = t('gauge.coolant.sub', { lift: whole(lift) });
        if (fan) line += t('gauge.coolant.fan');
        if (idle > 0) line += t('gauge.coolant.idle', { rpm: whole(idle) });
        if (rich > 0) line += t('gauge.coolant.rich', { pct: whole(rich) });
        coolantNode.textContent = line;
      }),
      // L'huile : sa viscosité, et les frottements qu'elle ajoute froide, ou retire au-delà de 100 °C.
      clock.add((text) => {
        if (!text || !oilNode) return;
        const live = running() ? telemetry() : null;
        const viscosity = live ? Math.round(live.oilViscosity) : 0;
        const extra = live && live.warmFrictionNm > 0 ? Math.round((live.coldFrictionNm / live.warmFrictionNm) * 100) : 0;
        const key = `${viscosity}|${extra}|${lang()}`;
        if (key === lastOil) return;
        lastOil = key;
        let line = viscosity > 0 ? t('gauge.oil.sub', { mpas: whole(viscosity) }) : t('gauge.oil.idle');
        // Le réseau agit toujours : plus chaude que 100 °C, l'huile retire des frottements.
        if (extra > 0) line += t('gauge.oil.friction', { pct: whole(extra) });
        else if (extra < 0) line += t('gauge.oil.frictionLess', { pct: whole(-extra) });
        oilNode.textContent = line;
      }),
      clock.add((text) => {
        if (!text) return;
        if (!filterNode) return;
        const now = running() ? telemetry() : null;
        const kpa = now ? Math.round(now.dpfKpa) : 0;
        const c = now ? Math.round(now.dpfC) : 20;
        const next = t(now && now.dpfRegen ? 'gauge.dpf.regen' : 'gauge.dpf.sub', { kpa: whole(kpa), c: whole(c) });
        if (next === lastFilter) return;
        lastFilter = next;
        filterNode.textContent = next;
      }),
      clock.add((text) => {
        if (!text) return;
        if (!camNode) return;
        const now = running() ? telemetry() : null;
        const profile = twoStep ? t(now && now.camHigh > 0 ? 'gauge.cam.high' : 'gauge.cam.low') : '';
        const phaser = phased ? t('gauge.cam.phaser', { deg: `${now && now.camPhaser >= 0.5 ? '+' : ''}${whole(now ? now.camPhaser : 0)}` }) : '';
        const shown = profile && phaser ? t('gauge.cam.join', { a: profile, b: phaser }) : profile || phaser;
        if (shown === lastCam) return;
        lastCam = shown;
        camNode.textContent = shown;
      }),
      clock.add((text) => {
        if (!text) return;
        if (!dutyNode) return;
        // Le rapport cyclique que commande le calculateur ; moteur arrêté, l'électrovanne n'est pas alimentée.
        const duty = running() ? Math.round(Math.max(telemetry().boostDuty, 0) * 100) : 0;
        if (duty === lastDuty && lang() === lastDutyLang) return;
        lastDuty = duty;
        lastDutyLang = lang();
        dutyNode.textContent = t('gauge.boostDuty', { duty: whole(duty) });
      }),
      clock.add((text) => {
        if (!text) return;
        if (!chargeNode) return;
        const c = Math.round(running() ? telemetry().chargeC : 20);
        if (c === lastCharge && lang() === lastChargeLang) return;
        lastCharge = c;
        lastChargeLang = lang();
        chargeNode.textContent = t('gauge.chargeAir', { c: whole(c) });
      }),
      clock.add((text) => {
        if (!text) return;
        if (!bottleNode) return;
        const now = telemetry();
        const shown = t(now.nitrousFlow > 0 ? 'gauge.nitrous.flowing' : 'gauge.nitrous.left', { kg: fixed(running() ? now.nitrousKg : 0, 2), g: whole(now.nitrousFlow) });
        if (shown === lastBottle) return;
        lastBottle = shown;
        bottleNode.textContent = shown;
      }),
    ];
    return () => {
      stop();
      stops.forEach((item) => item());
    };
  });
</script>

<div class="bank" bind:this={root} data-testid="gauge-bank">
  <Gauge id="speed" label={t('common.speed')} note={t('gauge.speed.note')} unit="km/h" min={0} max={speedMax} step={speedStep} minor={speedStep / 2} {clock} read={() => motion.speed}>
    {t('gauge.speed.top', { speed: whole(spec.topSpeed) })}
  </Gauge>
  {#if config.electric}
    <Gauge id="power" label={t('common.power')} note={t('gauge.power.note')} unit={t('unit.hp')} min={0} max={powerMax} step={powerStep} {clock} read={() => (running() ? Math.max(telemetry().power, 0) * KW_TO_HP : 0)}>
      {t('gauge.electric')}
    </Gauge>
  {:else}
    <Gauge id="manifold" label={t('gauge.manifold')} note={t('gauge.manifold.note')} unit="bar" min={-1} max={manifoldMax} step={manifoldStep} minor={manifoldStep / 5} zero={0} mark={boosted ? config.boostBar : null}
      digits={2} signed tickDigits={1} {clock} read={() => readings.manifoldBar}>
      {#if boosted}<span data-testid="gauge-turbine" bind:this={turbineNode}></span>{:else}{t('gauge.manifold.natural')}{/if}
      {#if hotAir}<span data-testid="gauge-intake-air" bind:this={intakeNode}></span>{/if}
      {#if twoStep || phased}<span class="line" data-testid="gauge-cam" bind:this={camNode}></span>{/if}
      {#if solenoid}<span data-testid="gauge-boost-duty" bind:this={dutyNode}></span>{/if}
      {#if chargeShown}<span data-testid="gauge-charge-air" bind:this={chargeNode}></span>{/if}
    </Gauge>
    <Gauge id="collector" label={t('gauge.collector')} note={t('gauge.collector.note')} unit="°C" min={0} max={1000} step={200} minor={100} warm {clock} read={() => readings.collectorC}>
      <span data-testid="gauge-tailpipe" bind:this={tailpipeNode}></span>
    </Gauge>
    <Gauge id="back" label={t('gauge.back')} note={t('gauge.back.note')} unit="bar" min={0} max={backMax} step={backStep} minor={backStep / 2} digits={2} tickDigits={1} {clock} read={() => Math.max(readings.backBar, 0)}>
      {t('gauge.back.sub')}
    </Gauge>
    <!-- L'eau et l'huile : le réseau thermique, calculé à chaque pas du modèle. Repère : le moteur chaud de référence. -->
    <Gauge id="coolant" label={t('gauge.coolant')} note={t('gauge.coolant.note')} unit="°C" min={0} max={140} step={20} minor={10} zone={{ from: 120, to: 140 }} mark={90} {clock} read={() => readings.coolantC}>
      <span data-testid="gauge-coolant-sub" bind:this={coolantNode}></span>
    </Gauge>
    <Gauge id="oil" label={t('gauge.oil')} note={t('gauge.oil.note')} unit="°C" min={0} max={160} step={40} minor={20} zone={{ from: 140, to: 160 }} mark={100} {clock} read={() => readings.oilC}>
      <span data-testid="gauge-oil-sub" bind:this={oilNode}></span>
    </Gauge>
    {#if filtered}
      <Gauge id="dpf" label={t('gauge.dpf')} note={t('gauge.dpf.note')} unit="%" min={0} max={120} step={20} minor={10} {clock} read={() => (running() ? telemetry().dpfLoad * 100 : 0)}>
        <span data-testid="gauge-dpf-state" bind:this={filterNode}></span>
      </Gauge>
    {/if}
    {#if nitrous}
      <!-- La bouteille : sa pression suit sa température, qui baisse à mesure que le liquide bout pour remplir le vide. -->
      <Gauge id="nitrous" label={t('gauge.nitrous')} note={t('gauge.nitrous.note')} unit="bar" min={0} max={80} step={20} minor={5} {clock} read={() => (running() ? Math.max(telemetry().nitrousBar, 0) : 0)}>
        <span data-testid="gauge-nitrous-left" bind:this={bottleNode}></span>
        {#if onrefill}<button type="button" class="refill" onclick={() => onrefill?.()} data-testid="nitrous-refill">{t('gauge.nitrous.refill')}</button>{/if}
      </Gauge>
    {/if}
  {/if}
</div>

<style>
  .bank { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px 12px; }
  /* La distribution variable : sa propre ligne sous la jauge. */
  .line { display: block; }
  .refill { min-height: 24px; margin-left: 6px; padding: 0 8px; border: 0; background: var(--surface-raised); color: var(--text); font-size: var(--type-small); cursor: pointer; }
  .refill:hover { background: var(--surface-high); }
</style>
