#include "revforge/engine_sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "kotlin_math.hpp"

namespace revforge {

namespace {

constexpr int kControlSamples = 32;
constexpr float kMinRpm = 100.0f;
constexpr float kSilenceGain = 1e-4f;
constexpr float kRpmTauS = 0.035f;
constexpr float kSlopeTauS = 0.12f;
constexpr float kGainTauS = 0.05f;
constexpr float kDriftTauS = 0.25f;
constexpr float kRpmDrift = 0.03f;
constexpr float kLoadCruise = 0.3f;
constexpr float kLoadAttackS = 0.07f;
constexpr float kLoadReleaseS = 0.25f;
constexpr float kFullLoadRpmPerS = 1200.0f;
constexpr float kLimitCeiling = 0.85f;
constexpr float kSoftKnee = 0.9f;
constexpr float kKneeRange = 0.08f;

// Calage des soupapes dans le cycle de 720°, à partir de l'explosion.
constexpr float kEvo = 130.0f / 720.0f;
constexpr float kEvc = 370.0f / 720.0f;
constexpr float kIvo = 350.0f / 720.0f;
constexpr float kIvc = 590.0f / 720.0f;

constexpr float kSoundHotMS = 520.0f;
constexpr float kBlowdownSpeedMS = 110.0f;
constexpr float kReferenceCylinder = 0.084f * 0.084f * 0.09f;
constexpr float kCombustionSpread = 0.24f;
constexpr float kTimingSpread = 0.0025f;
constexpr float kMaxPipeS = 0.03f;
constexpr float kMaxHeaderM = 1.2f;
constexpr std::array<float, 3> kChamberS{0.0041f, 0.0089f, 0.0173f};
constexpr std::array<float, 3> kChamberG{0.5f, 0.55f, 0.5f};

constexpr float kLevelTarget = 0.2f;
constexpr float kLevelSettleS = 1.2f;
constexpr float kLevelSettleTauS = 0.3f;
constexpr float kLevelTauS = 2.5f;
constexpr float kInitialLevel = 0.03f;
constexpr float kLevelGainMin = 0.2f;
constexpr float kLevelGainMax = 12.0f;
constexpr float kRumbleLevel = 0.1f;
constexpr float kRadiation = 42.0f;
constexpr float kRadiationSoften = 0.5f;
constexpr float kDrive = 1.4f;
constexpr float kExhaustLevel = 0.6f;
constexpr float kBodyLevel = 0.26f;
constexpr float kJetNoise = 0.4f;
constexpr float kIntakeLevel = 0.08f;
constexpr float kHashLevel = 0.005f;
constexpr float kValveLevel = 0.018f;
constexpr float kDieselKnock = 0.07f;
constexpr float kRoomMix = 0.22f;
constexpr float kEvLevel = 1.0f;
constexpr float kOverrunLoad = 0.14f;
constexpr float kOverrunMinIdleRatio = 1.35f;
constexpr float kOverrunDelayS = 0.08f;
constexpr float kOverrunTauS = 0.05f;
constexpr float kOverrunPressureDrop = 0.72f;
constexpr float kPopMinRpm = 1800.0f;
constexpr float kPopWindowS = 1.1f;
constexpr float kPopHeatTauS = 0.35f;
constexpr float kPopMinGapS = 0.09f;
constexpr float kPopAttackS = 0.0022f;
constexpr float kPopPush = 0.5f;
constexpr float kPopBodyQ = 13.0f;
constexpr float kPopBodyHighQ = 5.0f;
constexpr float kPopBody = 2.2f;
constexpr float kPopBodyHigh = 0.5f;
constexpr float kPopBlast = 0.65f;
constexpr float kPopInPipe = 0.12f;
constexpr float kTurboSpoolS = 0.75f;
constexpr float kTurboFallS = 0.22f;
constexpr float kTurboHoldS = 2.5f;
constexpr float kTurbineUpS = 0.9f;
constexpr float kTurbineDownS = 1.5f;
constexpr float kTurboReleaseMinBoost = 0.3f;
constexpr float kTurboWhistle = 0.12f;
constexpr float kTurboAir = 0.05f;
constexpr float kTurboWastegate = 0.09f;
constexpr float kTurboRelease = 0.42f;
constexpr float kWastegateRattleHz = 31.0f;
constexpr float kReleaseAttack = 0.004f;
constexpr float kFlutterLowHz = 10.0f;
constexpr float kFlutterSpanHz = 16.0f;

constexpr int kSineSize = 2048;

const std::array<float, kSineSize + 1>& sineTable() {
    static const auto table = [] {
        std::array<float, kSineSize + 1> t{};
        for (int i = 0; i <= kSineSize; ++i) {
            t[i] = static_cast<float>(std::sin(2.0 * km::kPi * i / kSineSize));
        }
        return t;
    }();
    return table;
}

/** Filtre d'état à deux pôles (trapézoïdal) : stable quand sa fréquence bouge. */
struct Svf {
    float lp = 0.0f;
    float bp = 0.0f;
    float k = 1.0f;
    float g = 0.0f;
    float a1 = 0.0f;
    float ic1 = 0.0f;
    float ic2 = 0.0f;

    void set(float hz, float q, float sampleRate) {
        g = static_cast<float>(std::tan(km::kPi * static_cast<double>(hz) / static_cast<double>(sampleRate)));
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
    }

    void process(float x) {
        const float v3 = x - ic2;
        const float v1 = a1 * (ic1 + g * v3);
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        bp = v1;
        lp = v2;
    }
};

/** Disperse l'onde sans la colorer : une chambre de silencieux, ou l'écho de l'habitacle. */
struct AllPass {
    std::vector<float> line;
    float g = 0.0f;
    int at = 0;

    void init(float sampleRate, float seconds, float gain) {
        line.assign(std::max(static_cast<int>(sampleRate * seconds), 1), 0.0f);
        g = gain;
        at = 0;
    }

    void reset() {
        std::fill(line.begin(), line.end(), 0.0f);
        at = 0;
    }

    float process(float x) {
        const float delayed = line[at];
        const float input = x + delayed * g;
        line[at] = input;
        at = at + 1 == static_cast<int>(line.size()) ? 0 : at + 1;
        return delayed - input * g;
    }
};

}  // namespace

struct EngineSound::State {
    float sr;
    std::uint32_t rng;
    const std::array<float, kSineSize + 1>& sine = sineTable();

    EngineProfile wanted;
    EngineProfile active;
    std::uint64_t wantedGeneration = 0;
    std::uint64_t activeGeneration = 0;
    ExhaustSpec spec{};

    // ── Contrôle (tous les kControlSamples) ──
    int controlLeft = 0;
    float controlDt;
    float rpm = 0.0f;
    float rpmInput = 0.0f;
    float slowRpm = 0.0f;
    float load = kLoadCruise;
    float rpmFrac = 0.0f;
    float phaseInc = 0.0f;
    float gain = 0.0f;
    float gainStep = 0.0f;
    float eventBus = 0.0f;
    float eventGain = 0.0f;
    float eventGainStep = 0.0f;
    float overrun = 0.0f;
    float overrunSeconds = 0.0f;
    float blockEnergy = 0.0f;
    float levelAge = 0.0f;
    float levelEnergy = kInitialLevel * kInitialLevel;
    float levelGain = kLevelTarget / kInitialLevel;
    float wobble = 0.0f;
    float openPressure = 0.0f;
    float bodyGain = 1.0f;
    float hashLevel = 0.0f;

    // ── Cylindres ──
    float phase = 0.0f;
    int cylinders = 0;
    std::array<float, kMaxCylinders> pressure{};
    std::array<float, kMaxCylinders> timing{};
    std::array<float, kMaxCylinders> nextTiming{};
    std::array<bool, kMaxCylinders> exhaustOpen{};
    std::array<bool, kMaxCylinders> intakeOpen{};
    std::array<std::vector<float>, kMaxCylinders> header;
    std::array<int, kMaxCylinders> headerDelay{};
    std::array<int, kMaxCylinders> headerSlot{};
    float blowRate = 0.0f;
    float backStroke = 0.0f;

    // ── Échappement ──
    float lastCollector = 0.0f;
    std::vector<float> pipe;
    int pipeWrite = 0;
    int pipeDelay = 1;
    float pipeDamp = 0.0f;
    std::array<AllPass, 3> chamberBank;
    int chamberCount = 0;
    Svf muffler, bodyLow, turbulence, rumble;

    // ── Admission et mécanique ──
    Svf intakeBand, hashBand, valveBand, valveBandHigh;
    float valveEnv = 0.0f;
    float valveDecay;
    Svf knockBand;
    float knockEnv = 0.0f;
    float knockDecay;

    // ── Habitacle et sortie ──
    std::array<AllPass, 3> room;
    Svf tone, highPass;
    float limiterPeak = 0.0f;
    float limiterGain = 1.0f;
    float limiterRelease;
    float limiterAttack;
    float limiterRecover;

    // ── Turbo ──
    float boost = 0.0f;
    float turbine = 0.0f;
    float wastegate = 0.0f;
    float releaseEnv = 0.0f;
    float releaseRise = 0.0f;
    float flutterPhase = 0.0f;
    float dumpDecay;
    float flutterDecay;
    Svf whistleBand, airBand, wastegateBand, releaseBand;

    // ── Détonations ──
    float popEnv = 0.0f;
    float popDecay = 0.0f;
    float popAttack = 1.0f;
    float popHeat = 0.0f;
    float popCooldown = 0.0f;
    Svf popBlast, popBody, popBodyHigh;

    State(int sampleRate, std::int32_t seed)
        : sr(static_cast<float>(sampleRate)),
          rng(seed == 0 ? 1u : static_cast<std::uint32_t>(seed)),
          controlDt(static_cast<float>(kControlSamples) / static_cast<float>(sampleRate)) {
        valveDecay = km::exp(-1.0f / (0.0009f * sr));
        knockDecay = km::exp(-1.0f / (0.0022f * sr));
        limiterRelease = km::exp(-1.0f / (0.12f * sr));
        limiterAttack = 1.0f - km::exp(-1.0f / (0.0005f * sr));
        limiterRecover = 1.0f - km::exp(-1.0f / (0.08f * sr));
        dumpDecay = km::exp(-1.0f / (0.2f * sr));
        flutterDecay = km::exp(-1.0f / (0.42f * sr));

        pipe.assign(static_cast<int>(sr * kMaxPipeS) + 2, 0.0f);
        const int maxHeader = static_cast<int>(kMaxHeaderM / kSoundHotMS * sr) + 2;
        for (auto& line : header) line.assign(maxHeader, 0.0f);
        for (std::size_t i = 0; i < chamberBank.size(); ++i) chamberBank[i].init(sr, kChamberS[i], kChamberG[i]);
        room[0].init(sr, 0.0031f, 0.55f);
        room[1].init(sr, 0.0073f, 0.5f);
        room[2].init(sr, 0.0127f, 0.45f);

        highPass.set(28.0f, 0.707f, sr);
        turbulence.set(900.0f, 0.6f, sr);
        rumble.set(130.0f, 0.7f, sr);
        intakeBand.set(850.0f, 0.55f, sr);
        hashBand.set(5200.0f, 0.5f, sr);
        valveBand.set(3600.0f, 1.1f, sr);
        valveBandHigh.set(7400.0f, 1.4f, sr);
        knockBand.set(2100.0f, 1.0f, sr);
        whistleBand.set(1900.0f, 16.0f, sr);
        airBand.set(3200.0f, 0.7f, sr);
        wastegateBand.set(5200.0f, 1.0f, sr);
        releaseBand.set(2600.0f, 0.8f, sr);
        bodyLow.set(170.0f, 0.8f, sr);

        // Moteur de départ tant qu'aucun n'a été choisi.
        EngineConfig start;
        start.boreMm = 86;
        start.redlineRpm = 7200;
        start.exhaust = Exhaust::Sport;
        start.pops = PopLevel::Light;
        wanted = start.toProfile();
        applyProfile(wanted);
    }

    /** Dans [-1, 1[. */
    float random() {
        std::uint32_t x = rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }

    /** Sinus d'une phase exprimée en tours, par table interpolée. */
    float sinFast(float turns) const {
        const float position = (turns - km::floor(turns)) * static_cast<float>(kSineSize);
        const int index = static_cast<int>(position);
        const float t = position - static_cast<float>(index);
        return sine[index] + (sine[index + 1] - sine[index]) * t;
    }

    float smoothing(float tauSeconds) const { return 1.0f - km::exp(-controlDt / tauSeconds); }

    void applyProfile(const EngineProfile& p) {
        active = p;
        activeGeneration = wantedGeneration;
        spec = exhaustSpec(p.exhaust);
        phase = 0.0f;
        levelAge = 0.0f;
        levelEnergy = kInitialLevel * kInitialLevel;
        levelGain = kLevelTarget / kInitialLevel;
        std::fill(pipe.begin(), pipe.end(), 0.0f);
        pipeDamp = 0.0f;
        cylinders = p.firingCount;
        pressure.fill(0.0f);
        timing.fill(0.0f);
        nextTiming.fill(0.0f);
        exhaustOpen.fill(false);
        intakeOpen.fill(false);
        for (int c = 0; c < cylinders; ++c) {
            const int delay = static_cast<int>((p.headerM[c] / kSoundHotMS) * sr);
            headerDelay[c] = std::max(delay, 1);
            std::fill(header[c].begin(), header[c].end(), 0.0f);
            headerSlot[c] = 0;
        }
        const int wantedPipe = static_cast<int>((2.0f * spec.pipeM / kSoundHotMS) * sr);
        pipeDelay = std::clamp(wantedPipe, 1, static_cast<int>(pipe.size()) - 1);
        // Bouffée d'échappement : le cylindre se vide d'autant plus vite que la soupape
        // est grande face au volume à vider, soit une durée proportionnelle à la course.
        const float blowdownSeconds = km::coerceAtLeast(p.strokeM / kBlowdownSpeedMS, 0.0005f);
        blowRate = 1.0f / (blowdownSeconds * sr);
        backStroke = 0.12f * km::sqrt(km::max(p.boreM * p.boreM * p.strokeM, 1e-6f) / kReferenceCylinder);
        popBlast.set(spec.popCutoffHz, 0.7f, sr);
        popBody.set(p.popBodyHz, kPopBodyQ, sr);
        popBodyHigh.set(p.popBodyHz * 2.3f, kPopBodyHighQ, sr);
        chamberCount = spec.chambers;
        for (auto& chamber : chamberBank) chamber.reset();
    }

    void control(float wantedRpm, float volume) {
        const float wantedTarget = wantedRpm;
        const bool engineOn = wantedTarget >= kMinRpm && volume > 0.0f;

        // Changement de profil : silence bref, pour ne pas mélanger deux états.
        const bool switching = wantedGeneration != activeGeneration;
        if (switching && gain < kSilenceGain) applyProfile(wanted);

        // Moteur qui démarre : directement au ralenti, sans montée parasite depuis zéro.
        if (rpm < kMinRpm && engineOn) {
            rpm = km::min(wantedTarget, wanted.idleRpm);
            rpmInput = rpm;
            slowRpm = rpm;
        }
        // Deux étages : le régime arrive par marches, que le multiplicateur agrandit.
        rpmInput += (wantedTarget - rpmInput) * smoothing(kRpmTauS);
        rpm += (rpmInput - rpm) * smoothing(kRpmTauS);
        const float previousSlow = slowRpm;
        slowRpm += (rpm - slowRpm) * smoothing(kSlopeTauS);
        const float slope = (slowRpm - previousSlow) / controlDt;

        // Le régime qui monte trahit l'accélérateur, et cette seule donnée suffit.
        const float wantedLoad = km::coerceIn(kLoadCruise + slope / kFullLoadRpmPerS, 0.0f, 1.0f);
        load += (wantedLoad - load) * smoothing(wantedLoad > load ? kLoadAttackS : kLoadReleaseS);

        const EngineProfile& a = active;
        rpmFrac = km::coerceIn((rpm - a.idleRpm) / (a.redlineRpm - a.idleRpm), 0.0f, 1.0f);

        // Le moteur ne tourne jamais rond : lente dérive, plus marquée au ralenti.
        const float driftNoise = random();
        wobble += (driftNoise * kRpmDrift - wobble) * smoothing(kDriftTauS);
        const float drift = 1.0f + wobble * (1.0f - 0.7f * rpmFrac);
        phaseInc = km::max(rpm, 0.0f) * drift / 120.0f / sr;

        // Pied levé, moteur lancé : l'injection se coupe, le moteur n'est plus qu'une pompe.
        const bool cutting = engineOn && wantedLoad < kOverrunLoad && rpm > a.idleRpm * kOverrunMinIdleRatio;
        overrunSeconds = cutting ? overrunSeconds + controlDt : 0.0f;
        const float overrunTarget = overrunSeconds > kOverrunDelayS ? 1.0f : 0.0f;
        overrun += (overrunTarget - overrun) * smoothing(kOverrunTauS);
        popHeat *= km::exp(-controlDt / kPopHeatTauS);
        popCooldown = km::max(popCooldown - controlDt, 0.0f);
        openPressure = (0.28f + 0.72f * load) * (1.0f - kOverrunPressureDrop * overrun);
        bodyGain = 1.0f + 0.5f * (1.0f - rpmFrac);
        hashLevel = (0.12f + 0.88f * km::sqrt(rpmFrac)) * (0.55f + 0.45f * load);

        const float cutoff = spec.cutoffHz * (0.72f + 0.55f * load) * (0.7f + 0.5f * rpmFrac);
        muffler.set(km::min(cutoff, sr * 0.3f), 0.6f, sr);
        if (a.electric) tone.set(km::min(a.lowpassIdleHz + (a.lowpassRedlineHz - a.lowpassIdleHz) * rpmFrac, sr * 0.3f), 0.707f, sr);
        if (a.turbo) updateTurbo(wantedLoad);

        // Niveau automatique très lent : harmonise toutes les configurations possibles,
        // sans gommer une accélération. Figé tant que le moteur ne tourne pas.
        if (engineOn && !switching) {
            // Rapide au démarrage pour trouver le bon niveau, lent ensuite.
            levelAge += controlDt;
            const float levelTau = levelAge < kLevelSettleS ? kLevelSettleTauS : kLevelTauS;
            levelEnergy += (blockEnergy / static_cast<float>(kControlSamples) - levelEnergy) * smoothing(levelTau);
            levelGain = km::coerceIn(kLevelTarget / km::sqrt(km::max(levelEnergy, 1e-8f)), kLevelGainMin, kLevelGainMax);
        }
        blockEnergy = 0.0f;

        const float gainTarget = (!engineOn || switching)
            ? 0.0f
            : volume * levelGain * spec.level * (0.72f + 0.28f * rpmFrac) * (0.62f + 0.38f * load);
        const float smoothed = gain + (gainTarget - gain) * smoothing(kGainTauS);
        gainStep = (smoothed - gain) / static_cast<float>(kControlSamples);
        // Ces bruits ont leur propre niveau, indépendant du niveau automatique du moteur.
        const float eventTarget = (engineOn && !switching) ? volume : 0.0f;
        const float eventSmoothed = eventGain + (eventTarget - eventGain) * smoothing(kGainTauS);
        eventGainStep = (eventSmoothed - eventGain) / static_cast<float>(kControlSamples);
    }

    /** Pression : monte avec retard sous charge. Turbine : continue sur son élan pied levé. */
    void updateTurbo(float wantedLoad) {
        const float pressureNow = km::coerceIn(km::sqrt(rpmFrac) * ((load - 0.3f) / 0.7f), 0.0f, 1.0f);
        if (wantedLoad < kOverrunLoad && boost > kTurboReleaseMinBoost && releaseEnv < 0.05f) {
            releaseEnv = boost;
            releaseRise = 0.0f;
        }
        // Régime stable ne veut pas dire pied levé : la pression ne s'effondre qu'au lever de pied.
        const float boostTau = pressureNow > boost ? kTurboSpoolS : (wantedLoad < kOverrunLoad ? kTurboFallS : kTurboHoldS);
        boost += (pressureNow - boost) * smoothing(boostTau);
        const float turbineTarget = km::max(boost, 0.06f * rpmFrac);
        turbine += (turbineTarget - turbine) * smoothing(turbineTarget > turbine ? kTurbineUpS : kTurbineDownS);
        whistleBand.set(1900.0f + 6300.0f * turbine, 16.0f, sr);
        wastegate += (km::coerceIn((boost - 0.8f) / 0.2f, 0.0f, 1.0f) * load - wastegate) * smoothing(0.12f);
        const float releaseHz = active.turboRelease == TurboRelease::Flutter ? 1250.0f : 1800.0f + 2000.0f * releaseEnv;
        releaseBand.set(releaseHz, 0.8f, sr);
    }

    /** Du carburant imbrûlé s'enflamme dans le tube : d'autant plus juste après le lever de pied. */
    void maybePop() {
        const EngineProfile& p = active;
        if (p.pops == PopLevel::Off || overrun < 0.5f || rpm < kPopMinRpm || popCooldown > 0.0f) return;
        const float firingsPerSecond = km::max(rpm / 120.0f * static_cast<float>(cylinders), 1.0f);
        const float freshness = km::exp(-overrunSeconds / kPopWindowS);
        const float rate = popsPerSecond(p.pops) * spec.popRate * (0.1f + 0.9f * freshness) * (0.5f + 0.5f * rpmFrac) * (1.0f + popHeat);
        const float roll = random() * 0.5f + 0.5f;
        if (roll >= rate / firingsPerSecond) return;
        // Une détonation est franche : pas de poussière de petits craquements.
        const float draw = random() * 0.5f + 0.5f;
        const float strength = p.pops == PopLevel::Strong ? 0.75f : 0.55f;
        popEnv = (0.55f + 0.45f * draw * draw) * strength * p.popForce;
        popAttack = 0.0f;
        popDecay = km::exp(-1.0f / ((spec.popDecayS * (0.7f + 0.6f * draw)) * sr));
        popHeat = km::min(popHeat + 0.5f, 1.0f);
        popCooldown = kPopMinGapS;
    }

    /**
     * Une onde de pression qui fait résonner le tube, pas un claquement : attaque
     * douce, corps grave. Le bruit du souffle reste filtré selon l'échappement.
     * Renvoie la part qui remonte la ligne ; l'essentiel sort par le bus d'événements.
     */
    float popSample() {
        if (popEnv < 1e-3f && km::abs(popBody.bp) < 1e-4f) return 0.0f;
        popAttack = km::min(popAttack + 1.0f / (kPopAttackS * sr), 1.0f);
        const float envelope = popEnv * popAttack * (2.0f - popAttack);  // arrondie : aucune arête au début
        const float noise = random();
        popBlast.process(noise * envelope);
        const float pressureNow = popBlast.lp;
        popBody.process(pressureNow + envelope * kPopPush);
        popBodyHigh.process(pressureNow);
        popEnv *= popDecay;
        eventBus += (popBody.bp * kPopBody + popBodyHigh.bp * kPopBodyHigh + pressureNow * kPopBlast) * spec.popOpening;
        return pressureNow * kPopInPipe;
    }

    void turboSample() {
        const float whistleNoise = random();
        whistleBand.process(whistleNoise);
        const float airNoise = random();
        airBand.process(airNoise);
        float out = whistleBand.bp * turbine * turbine * kTurboWhistle * (0.35f + 0.65f * load);
        out += airBand.bp * boost * boost * kTurboAir;
        if (wastegate > 1e-3f) {
            flutterPhase += kWastegateRattleHz / sr;
            const float gateNoise = random();
            wastegateBand.process(gateNoise);
            out += wastegateBand.bp * wastegate * kTurboWastegate * (0.72f + 0.28f * sinFast(flutterPhase));
        }
        if (releaseEnv > 1e-3f) {
            releaseRise = km::min(releaseRise + kReleaseAttack, 1.0f);
            const float releaseNoise = random();
            releaseBand.process(releaseNoise);
            float shape = 1.0f;
            if (active.turboRelease == TurboRelease::Flutter) {
                // La pression repasse par le compresseur par saccades, de plus en plus lentes.
                flutterPhase += (kFlutterLowHz + kFlutterSpanHz * releaseEnv) / sr;
                const float beat = 0.5f + 0.5f * sinFast(flutterPhase);
                shape = beat * beat;
            }
            out += releaseBand.bp * releaseEnv * releaseRise * shape * kTurboRelease;
            releaseEnv *= active.turboRelease == TurboRelease::Flutter ? flutterDecay : dumpDecay;
        }
        eventBus += out;
    }

    /** Le gaz sature doucement : mêle les raies entre elles, comme un vrai tube. */
    static float softDrive(float x) {
        const float y = x * kDrive;
        return y / (1.0f + km::abs(y) * 0.45f);
    }

    float combustionSample() {
        const EngineProfile& p = active;
        eventBus = 0.0f;
        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;

        float collector = 0.0f;
        float jet = 0.0f;
        float intake = 0.0f;
        bool valve = false;
        for (int c = 0; c < cylinders; ++c) {
            float theta = phase + p.firing[c] + timing[c];
            if (theta >= 1.0f) theta -= 1.0f;
            if (theta < 0.0f) theta += 1.0f;

            float flow = 0.0f;
            if (theta >= kEvo && theta < kEvc) {
                if (!exhaustOpen[c]) {
                    exhaustOpen[c] = true;
                    // Aucune combustion n'est identique à la précédente.
                    const float first = random();
                    const float second = random();
                    const float spread = 1.0f + kCombustionSpread * (first + second) * (1.0f - 0.5f * rpmFrac);
                    pressure[c] = km::max(openPressure * p.firingGain[c] * spread, 0.02f);
                    valve = true;
                    nextTiming[c] = random() * kTimingSpread;
                    maybePop();
                    if (p.diesel) knockEnv = km::max(knockEnv, spread);
                }
                const float lift = sinFast((theta - kEvo) / (kEvc - kEvo) * 0.5f);
                const float escape = pressure[c] * lift;
                pressure[c] = km::max(pressure[c] - escape * blowRate, 0.0f);
                // La bouffée, puis le piston qui chasse ce qui reste.
                flow = escape + backStroke * lift * openPressure;
            } else if (exhaustOpen[c]) {
                exhaustOpen[c] = false;
                valve = true;
            }

            if (theta >= kIvo && theta < kIvc) {
                if (!intakeOpen[c]) {
                    intakeOpen[c] = true;
                    valve = true;
                }
                intake += sinFast((theta - kIvo) / (kIvc - kIvo) * 0.5f);
            } else if (intakeOpen[c]) {
                intakeOpen[c] = false;
                valve = true;
                // Soupapes fermées : on peut décaler la prochaine explosion sans à-coup.
                timing[c] = nextTiming[c];
            }

            auto& line = header[c];
            const int delay = headerDelay[c];
            const int slot = headerSlot[c];
            const int next = slot == delay ? 0 : slot + 1;
            collector += line[next];
            line[slot] = flow;
            headerSlot[c] = next;
            jet += flow * flow;
        }

        // Turbulences : la vraie source du grain, proportionnelle au débit.
        const float turbulenceNoise = random();
        turbulence.process(turbulenceNoise);
        // Un tube rayonne les variations de débit : c'est ce qui claque dans les médiums.
        // Légèrement adouci : une onde de pression n'a pas d'arête vive.
        const float softened = lastCollector + (collector - lastCollector) * kRadiationSoften;
        const float radiated = (softened - lastCollector) * kRadiation;
        lastCollector = softened;
        const float detonation = popSample();
        const float noisy = collector + radiated + detonation + turbulence.bp * km::sqrt(jet) * kJetNoise * (0.5f + 0.5f * load);

        // Tube : une part de l'onde revient, amortie, et se mêle à la suivante.
        int read = pipeWrite - pipeDelay;
        if (read < 0) read += static_cast<int>(pipe.size());
        pipeDamp += 0.3f * (pipe[read] - pipeDamp);
        const float inPipe = softDrive(noisy) - spec.reflection * pipeDamp;
        pipe[pipeWrite] = inPipe;
        pipeWrite = pipeWrite + 1 == static_cast<int>(pipe.size()) ? 0 : pipeWrite + 1;

        float outlet = inPipe;
        for (int i = 0; i < chamberCount; ++i) outlet = chamberBank[i].process(outlet);
        muffler.process(outlet);
        bodyLow.process(outlet);
        float mix = muffler.lp * kExhaustLevel + bodyLow.lp * kBodyLevel * bodyGain;

        // Grondement : gaz et carrosserie, sans rythme propre.
        const float rumbleNoise = random();
        rumble.process(rumbleNoise);
        mix += rumble.lp * kRumbleLevel * (0.4f + 0.6f * load) * (0.5f + 0.5f * rpmFrac);

        const float intakeNoise = random();
        intakeBand.process(intakeNoise);
        mix += intakeBand.bp * intake * kIntakeLevel * (0.2f + 0.8f * load) * (0.4f + 0.6f * rpmFrac);

        // Mécanique : souffle aigu du moteur, et claquements de soupapes.
        const float hashNoise = random();
        hashBand.process(hashNoise);
        mix += hashBand.bp * kHashLevel * hashLevel;
        if (valve) valveEnv = 1.0f;
        const float tick = random() * valveEnv;
        valveBand.process(tick);
        valveBandHigh.process(tick);
        mix += (valveBand.bp + 0.7f * valveBandHigh.bp) * kValveLevel * (0.35f + 0.65f * rpmFrac);
        valveEnv *= valveDecay;

        if (p.diesel) {
            const float knockNoise = random();
            knockBand.process(knockNoise);
            mix += knockBand.bp * knockEnv * kDieselKnock * (1.0f - 0.5f * load);
            knockEnv *= knockDecay;
        }
        if (p.turbo) turboSample();

        float wet = mix;
        for (auto& r : room) wet = r.process(wet);
        return mix * (1.0f - kRoomMix) + wet * kRoomMix;
    }

    float electricSample() {
        phase += phaseInc * 2.0f;
        if (phase >= 1.0f) phase -= 1.0f;
        // Le rotor et l'onduleur : quelques raies pures, donc aucun repliement.
        const float pole = sinFast(phase * 4.0f);
        const float second = sinFast(phase * 8.0f);
        const float inverter = sinFast(phase * 12.0f);
        const float body = 0.42f * pole + 0.16f * second + (0.05f + 0.12f * load) * inverter;
        const float noise = random();
        turbulence.process(noise);
        tone.process((body * (0.5f + 0.5f * load) + turbulence.lp * 0.03f * (0.4f + 0.6f * load)) * kEvLevel);
        return tone.lp;
    }

    /** Limiteur à gain variable : garde la forme d'onde, là où un écrêteur grésille. */
    float limit(float x) {
        limiterPeak = km::max(km::abs(x), limiterPeak * limiterRelease);
        const float wantedGain = limiterPeak > kLimitCeiling ? kLimitCeiling / limiterPeak : 1.0f;
        limiterGain += (wantedGain - limiterGain) * (wantedGain < limiterGain ? limiterAttack : limiterRecover);
        const float y = x * limiterGain;
        const float a = km::abs(y);
        // Dernier filet pour la milliseconde d'attaque du limiteur.
        return a <= kSoftKnee ? y : km::sign(y) * (kSoftKnee + kKneeRange * km::tanh((a - kSoftKnee) / kKneeRange));
    }

    float nextSample(float wantedRpm, float volume) {
        if (controlLeft == 0) {
            control(wantedRpm, volume);
            controlLeft = kControlSamples;
        }
        --controlLeft;
        const float raw = active.electric ? electricSample() : combustionSample();
        highPass.process(raw);
        const float clean = raw - highPass.lp - highPass.k * highPass.bp;
        blockEnergy += clean * clean;
        const float shaped = clean * gain + eventBus * eventGain;
        eventGain += eventGainStep;
        gain += gainStep;
        return limit(shaped);
    }
};

EngineSound::EngineSound(int sampleRate, std::int32_t seed) : s_(std::make_unique<State>(sampleRate, seed)) {}

EngineSound::~EngineSound() = default;

void EngineSound::setProfile(const EngineProfile& profile) {
    // Même moteur : pas de fondu inutile.
    if (profile == s_->wanted) return;
    s_->wanted = profile;
    ++s_->wantedGeneration;
}

bool EngineSound::isSilent() const {
    return s_->gain < kSilenceGain && (targetRpm() < kMinRpm || volume_.load(std::memory_order_relaxed) == 0.0f);
}

void EngineSound::render(float* out, int count) {
    for (int i = 0; i < count; ++i) {
        out[i] = s_->nextSample(targetRpm_.load(std::memory_order_relaxed), volume_.load(std::memory_order_relaxed));
    }
}

float EngineSound::load() const { return s_->load; }

float EngineSound::boost() const { return s_->boost; }

}  // namespace revforge
