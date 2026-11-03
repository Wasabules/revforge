#include "revforge/engine_sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "acoustics.hpp"
#include "dsp.hpp"
#include "exhaust_line.hpp"
#include "gearbox_sound.hpp"
#include "intake_path.hpp"
#include "kotlin_math.hpp"
#include "revforge/air_filter.hpp"
#include "starter_sound.hpp"

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
constexpr float kLoadAttackS = 0.03f;
constexpr float kLoadReleaseS = 0.04f;
constexpr float kFullLoadRpmPerS = 1200.0f;
constexpr float kLimitCeiling = 0.85f;
constexpr float kSoftKnee = 0.9f;
constexpr float kKneeRange = 0.08f;

// Calage des soupapes dans le cycle de 720°, à partir de l'explosion.
constexpr float kEvo = 130.0f / 720.0f;
constexpr float kEvc = 370.0f / 720.0f;
// La came décolle en douceur : ce front, fixe en angle, évite un clic à chaque explosion au ralenti.
constexpr float kEvoRamp = 15.0f / 720.0f;
constexpr float kIvo = 350.0f / 720.0f;
constexpr float kIvc = 590.0f / 720.0f;

constexpr float kSoundHotMS = 520.0f;
// Une soupape s'ouvre à un angle fixe : deux bouffées restent en phase bien au-delà de 1 kHz.
constexpr float kPathJitterS = 0.00003f;
// La turbulence décale le gaz d'un instant à l'autre : les raies se défont peu à peu vers les aigus.
constexpr float kSampleJitterS = 0.000022f;
constexpr int kJitterDelay = 16;
constexpr float kTurbulenceGain = 0.6f;
constexpr float kHeaderDampHz = 2000.0f;
constexpr float kCombustionMemory = 0.55f;
constexpr float kReferenceCylinder = 0.084f * 0.084f * 0.09f;
constexpr float kCombustionSpread = 0.24f;
constexpr float kTimingSpread = 0.0003f;
constexpr float kMaxHeaderM = 1.2f;
constexpr float kRumbleLevel = 0.1f;
constexpr float kIntakeLevel = 0.08f;
constexpr float kHashLevel = 0.007f;
// Les aigus du souffle et de l'admission viennent avec la charge. À pleine bande et à niveau presque fixe,
// ils restaient derrière le moteur comme un bruit blanc à part, dès que l'échappement se faisait discret.
constexpr float kNoiseTopHz = 3000.0f;
constexpr float kValveLevel = 0.005f;
constexpr float kDieselKnock = 0.05f;
constexpr float kEvLevel = 1.0f;
constexpr float kOverrunLoad = 0.45f;
// Sous cette charge seulement, l'injection se coupe : une croisière à faible charge brûle encore son carburant.
constexpr float kFuelCutLoad = 0.15f;
// Quand on connaît l'accélérateur, c'est lui qui le dit.
constexpr float kLiftedThrottle = 0.03f;
constexpr float kOverrunMinIdleRatio = 1.35f;
constexpr float kOverrunDelayS = 0.02f;
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
constexpr float kPopBlast = 0.2f;
constexpr float kPopInPipe = 0.12f;
// L'anti-lag. Une chance par ouverture de soupape que ce qui arrive imbrûlé parte d'un coup, et la part de la chaleur qui part ainsi
// (supposés : l'écoute tranche). Le retard brûle surtout dans le cylindre ; un cylindre coupé envoie toute sa charge ; une flamme continue laisse peu de poches.
constexpr float kAntiLagBangRetard = 0.05f;
constexpr float kAntiLagSlugRetard = 0.15f;
constexpr float kAntiLagBangAir = 0.015f;
constexpr float kAntiLagSlugAir = 0.1f;
constexpr float kAntiLagBangCut = 0.45f;
constexpr float kAntiLagSlugCut = 1.0f;
// La force d'une détonation ordinaire, et son énergie (`POP_JOULES` à l'écran, ligne sport) : l'amplitude suit la racine de l'énergie.
constexpr float kAntiLagBangForce = 0.75f;
constexpr float kAntiLagPopJoules = 90.0f;
// La flamme au collecteur : du bruit de combustion, grave, qui traverse la ligne comme le reste du gaz.
constexpr float kAntiLagFlameHz = 380.0f;
constexpr float kAntiLagFlameInPipe = 0.5f;
constexpr float kTurboSpoolS = 0.75f;
constexpr float kTurboFallS = 0.22f;
constexpr float kTurboHoldS = 2.5f;
constexpr float kTurbineUpS = 0.9f;
constexpr float kTurbineDownS = 1.5f;
constexpr float kTurboReleaseMinBoost = 0.3f;
constexpr float kTurboWhistle = 0.03f;
constexpr float kTurboAir = 0.05f;
constexpr float kTurboWastegate = 0.09f;
constexpr float kTurboRelease = 1.7f;
constexpr float kWastegateRattleHz = 31.0f;
constexpr float kReleaseAttack = 0.004f;
constexpr float kReleaseFollowS = 0.003f;
constexpr float kWhistleLowShare = 0.3f;
// Le retour par la roue du compresseur fait bien plus de bruit que le même débit par une soupape.
constexpr float kBackflowLevel = 2.5f;
constexpr float kFlutterLowHz = 10.0f;
constexpr float kFlutterSpanHz = 16.0f;

constexpr float kRoadSpeedRef = 40.0f;

// ── Niveaux absolus : ce que chaque source produit à 1 m, en pascals ──
// Un cylindre rejette environ deux fois son volume en se vidant, sur la durée de la bouffée.
constexpr float kBlowVolume = 1.45f;
constexpr float kDieselFilter = 0.5f;
// La pression d'un monopôle à 1 m : ρ/(4π)·dQ/dt.
constexpr float kMonopoleAt1m = acoustics::kAirDensity / (4.0f * static_cast<float>(km::kPi));
constexpr float kIntakePa = 8.0f;
constexpr float kMechanicalPa = 8.0f;
constexpr float kPopPa = 25.0f;
constexpr float kTurboPa = 8.0f;
constexpr float kStarterPa = 3.0f;
constexpr float kUnderfloorPa = 1.0f;
constexpr float kTyrePa = 4.7f;
constexpr float kWindPa = 1.8f;
constexpr float kSquealPa = 6.0f;
constexpr float kElectricPa = 0.4f;
// La loi d'Anderton donne des dB(A) : on passe en pascals, puis on retire le gain du bruit de bloc par rapport à 1 Pa.
constexpr float kBlockPa = 2e-5f * 8.0f;
constexpr float kBlockTopHz = 4500.0f;
constexpr float kTailPa = 1.0f;
constexpr float kTailFloorPa = 0.05f;
constexpr float kBodyShare = 0.3f;
constexpr float kInletPa = 6.0f;
constexpr float kStructurePa = 0.05f;

// ── Du niveau réel à ce qui sort des haut-parleurs ──
// Le réel couvre de 40 à plus de 130 dB, un haut-parleur bien moins : une courbe de correspondance, comme pour
// une image, resserre l'écart sans inverser l'ordre. Niveau en dB(A), sortie en dB sous la pleine échelle à volume 1.
constexpr std::array<float, 9> kToneInDb{20.0f, 40.0f, 60.0f, 80.0f, 90.0f, 100.0f, 110.0f, 120.0f, 145.0f};
constexpr std::array<float, 9> kToneOutDb{-49.0f, -42.0f, -32.0f, -22.5f, -18.0f, -15.0f, -13.5f, -13.0f, -12.0f};
constexpr float kToneStartDb = 70.0f;
// Le détecteur est lent : un coup de gaz garde presque tout son écart, un plein gaz tenu se resserre.
constexpr float kToneAttackS = 1.0f;
// Quand on se déplace, c'est la tête qui change le niveau, pas le moteur : le gain suit plus vite.
constexpr float kToneMovingS = 0.04f;
// Vers une position toute prête, la tête va vite sans faire chanter le Doppler.
constexpr float kPresetSpeed = 6.0f;
constexpr float kToneReleaseS = 2.0f;
constexpr float kOutFadeS = 0.008f;
// Une partition courte : le retard de la convolution ne doit pas décaler l'admission face au reste du moteur.
constexpr int kIntakePartition = 32;
// Niveau efficace le plus haut à volume 1, avec une crête de 4 en moyenne : sous le seuil du limiteur.
constexpr float kOutputRmsMax = 0.25f;

using dsp::Svf;
using dsp::kSineSize;
using dsp::sineTable;

/** Niveau en dB(A) vers le gain linéaire qui mène des pascals à la sortie numérique, à volume 1. */
float toneGain(float heardDb) {
    float out = kToneOutDb[0];
    if (heardDb >= kToneInDb[kToneInDb.size() - 1]) {
        out = kToneOutDb[kToneOutDb.size() - 1];
    } else {
        for (std::size_t i = 1; i < kToneInDb.size(); ++i) {
            if (heardDb <= kToneInDb[i]) {
                const float t = km::max((heardDb - kToneInDb[i - 1]) / (kToneInDb[i] - kToneInDb[i - 1]), 0.0f);
                out = kToneOutDb[i - 1] + (kToneOutDb[i] - kToneOutDb[i - 1]) * t;
                break;
            }
        }
    }
    // Sans correspondance, 1 Pa vaudrait 94 dB : on lui retire la différence.
    return km::pow(10.0f, (out - (km::max(heardDb, kToneInDb[0]) - 94.0f)) / 20.0f);
}

/** Le gaz d'une rangée, du collecteur à la sortie : son propre souffle et sa propre ligne. */
struct GasPath {
    float turbineWave = 0.0f;
    float jitterState = 0.0f;
    std::array<float, 64> jitterLine{};
    int jitterAt = 0;
    float flowMean = 0.0f;
    Svf turbulenceBand;
    dsp::Convolver line;
    Svf highPass, highPassSecond;
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
    bool driven = false;
    float driveLoad = 0.0f;
    float driveThrottle = -1.0f;
    float driveBoost = 0.0f;
    bool driveCut = false;
    bool driveCrank = false;
    std::uint32_t popCount = 0;

    // ── Écoute et route ──
    Listener listener = Listener::Outside;
    bool presetPosition = true;
    bool stereo = false;
    acoustics::SoundField field;
    acoustics::CabinPath cabin;
    acoustics::LevelMeter meter;
    // Les sources de cet échantillon, en pascals à 1 m. Voir `Source` : tout ce qui s'entend passe par ici.
    std::array<float, kSources> stems{};
    float structure = 0.0f;
    bool cabinActive = false;
    bool cabinPending = false;
    CarAcoustics cabinCar;
    float cabinFade = 1.0f;
    float cabinCoef = 0.0f;
    float cabinLevel = 0.0f;
    float cabinStep = 0.0f;
    float toneDb = kToneStartDb;
    float outputEnergy = 0.0f;
    float outputCoef = 0.0f;
    float outGain = 0.0f;
    float outGainStep = 0.0f;
    float roadSpeed = 0.0f, roadSlip = 0.0f, roadLevel = 0.0f, squealLevel = 0.0f;
    float tyreLevel = 0.0f, windLevel = 0.0f;
    std::uint32_t roadRng = 0x51A7E;
    Svf windBand, tyreBand, squealBand, roadTone;
    StarterSound starter;
    GearboxSound gearbox;
    GearboxSpec gearboxSpec{};
    bool gearboxKnown = false;
    float crankHush = 1.0f;
    float starterLevel = 0.0f;
    bool wasCranking = false;

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
    float eventGain = 0.0f;
    float eventGainStep = 0.0f;
    float overrun = 0.0f;
    float overrunSeconds = 0.0f;
    float wobble = 0.0f;
    float openPressure = 0.0f;
    float bodyGain = 1.0f;
    float hashLevel = 0.0f;
    float hashTop = 0.0f, intakeTop = 0.0f, noiseTopCoef = 0.0f;
    // Sous le capot, le bloc garde tous ses aigus, quelle que soit la charge.
    float noiseOpen = 0.0f;

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
    std::array<float, kMaxCylinders> headerDamp{};
    std::array<float, kMaxCylinders> burnMemory{};
    float headerCoef = 0.0f;
    float jetShape = 1.0f;
    float gust = 1.0f;
    float gustSmooth = 1.0f;
    float gustCoef = 0.0f;
    float flowNoise = 1.0f;
    float cutGate = 1.0f;
    float blowdownBase = 0.002f;
    float idleShake = 0.0f;
    float flowMeanCoef = 0.0f;
    float jitterCoef = 0.0f;
    float jitterSamples = 0.0f;
    // La seconde rangée tire d'un autre générateur : la suite des tirages de la première ne change pas.
    std::uint32_t bankRng = 0x3C6EF35F;
    // Les bruits ajoutés depuis tirent de leur propre générateur : toute la suite du moteur reste la même.
    std::uint32_t addedRng = 0x1B873593;
    std::array<GasPath, 2> gas;
    float crossover = 0.0f;
    bool twoPaths = false;
    float linePressure = 0.0f;
    float exhaustLaw = 1.0f;
    float lineGain = 1.0f;
    float pumpLevel = 0.3f;
    bool wasCut = false;
    float boostPitch = 1.0f;
    float boostLevel = 1.0f;
    float popBus = 0.0f;
    float turboBus = 0.0f;
    float turbineCoef = 0.0f;
    float blowRate = 0.0f;
    float backStroke = 0.0f;

    // ── Échappement ──
    ExhaustLine line;
    // Le trajet de l'admission, quand on a quitté le filtre d'origine : le rapport des deux, en plus de ce qui est calibré.
    IntakePath intakePath;
    dsp::Convolver intakeResponse;
    bool otherFilter = false;
    Source intakeOut = Source::Intake;
    Svf turbulence, rumble;

    // ── Admission et mécanique ──
    Svf intakeBand, hashBand, valveBand, valveBandHigh;
    Svf inletTone, blockBand, blockBody, tailBand, jetLow, jetLower;
    float inletLast = 0.0f;
    float inletLevel = 0.0f;
    float blockEnv = 0.0f;
    float blockTop = 0.0f;
    float blockTopCoef = 0.0f;
    float blockDecay = 0.0f;
    float blockLevel = 0.0f;
    float tailLevel = 0.0f;
    float tailTop = 0.0f;
    float popTop = 0.0f;
    float tailTopCoef = 0.0f;
    float popTopCoef = 0.0f;
    float bodyDamp = 0.0f;
    float bodyShare = kBodyShare;
    float bodyDampCoef = 0.0f;
    float jetMean = 0.0f;       // vitesse moyenne du gaz en sortie (m/s)
    float jetDensity = 0.7f;
    float outletArea = 0.002f;
    std::array<float, 2> jetFlow{};
    float jetLeak = 0.0f;
    float valveEnv = 0.0f;
    float valveRise = 1.0f;
    float valveDecay;
    Svf knockBand;
    float knockEnv = 0.0f;
    float knockDecay;

    // ── Sortie ──
    Svf tone;
    float limiterPeak = 0.0f;
    float limiterGain = 1.0f;
    float limiterRelease;
    float limiterAttack;
    float limiterRecover;

    // ── Turbo ──
    float boost = 0.0f;
    float turbine = 0.0f;
    // Le vrai turbo, quand le modèle le donne. Son bruit a son propre tirage : la soupape qui s'ouvre ne décale pas le reste du son.
    bool turboDriven = false;
    TurboDrive turboIn;
    std::uint32_t turboRng = 0x9E3779B9;
    float releaseGoal = 0.0f;
    float releaseCoef = 0.0f;
    float whistleFlow = 0.0f;
    Svf whistleLow;
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
    bool poppedThisLift = false;
    Svf popBlast, popBody, popBodyHigh;

    // ── Anti-lag : rien de tout cela ne tourne sans lui, et son tirage est à part ──
    bool antiLagOn = false;
    std::uint32_t antiLagRng = 0x85EBCA6B;
    std::uint32_t antiLagEvent = 0;
    float antiLagFlame = 0.0f;
    Svf antiLagFlameBand;

    State(int sampleRate, std::int32_t seed)
        : sr(static_cast<float>(sampleRate)),
          rng(seed == 0 ? 1u : static_cast<std::uint32_t>(seed)),
          field(static_cast<float>(sampleRate)),
          cabin(static_cast<float>(sampleRate)),
          starter(static_cast<float>(sampleRate)),
          gearbox(static_cast<float>(sampleRate)),
          controlDt(static_cast<float>(kControlSamples) / static_cast<float>(sampleRate)),
          line(static_cast<float>(sampleRate)),
          intakePath(static_cast<float>(sampleRate)) {
        intakeResponse.init(kIntakePartition, IntakePath::kLength / kIntakePartition);
        valveDecay = km::exp(-1.0f / (0.0009f * sr));
        blockDecay = km::exp(-1.0f / (0.005f * sr));
        blockTopCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * kBlockTopHz / sr);
        jetLeak = km::exp(-2.0f * static_cast<float>(km::kPi) * 4.0f / sr);
        inletTone.set(220.0f, 1.3f, sr);
        blockBand.set(900.0f, 0.45f, sr);
        blockBody.set(350.0f, 1.0f, sr);
        tailBand.set(1500.0f, 0.5f, sr);
        tailTopCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 2000.0f / sr);
        popTopCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 1200.0f / sr);
        bodyDampCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 1800.0f / sr);
        jetLow.set(300.0f, 0.7f, sr);
        jetLower.set(300.0f, 0.7f, sr);
        knockDecay = km::exp(-1.0f / (0.012f * sr));
        limiterRelease = km::exp(-1.0f / (0.12f * sr));
        limiterAttack = 1.0f - km::exp(-1.0f / (0.0005f * sr));
        limiterRecover = 1.0f - km::exp(-1.0f / (0.08f * sr));
        dumpDecay = km::exp(-1.0f / (0.2f * sr));
        flutterDecay = km::exp(-1.0f / (0.42f * sr));

        meter.init(sr);
        cabinCoef = 1.0f - km::exp(-controlDt / kOutFadeS);
        outputCoef = 1.0f - km::exp(-1.0f / (0.06f * sr));
        for (GasPath& path : gas) {
            path.line.init(128, ExhaustLine::kImpulseLength / 128);
            path.turbulenceBand.set(2000.0f, 0.5f, sr);
            path.highPass.set(85.0f, 0.707f, sr);
            // Un petit tube rayonne mal les très basses fréquences : deux étages, soit 24 dB par octave.
            path.highPassSecond.set(85.0f, 0.707f, sr);
        }
        jitterCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 10000.0f / sr);
        jitterSamples = kSampleJitterS * sr;
        noiseTopCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * kNoiseTopHz / sr);
        flowMeanCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 10.0f / sr);
        const int maxHeader = static_cast<int>(kMaxHeaderM / kSoundHotMS * sr) + 2;
        for (auto& line : header) line.assign(maxHeader, 0.0f);
        turbulence.set(900.0f, 0.6f, sr);
        rumble.set(130.0f, 0.7f, sr);
        intakeBand.set(850.0f, 0.55f, sr);
        hashBand.set(5200.0f, 0.5f, sr);
        valveBand.set(3600.0f, 1.1f, sr);
        valveBandHigh.set(7400.0f, 1.4f, sr);
        knockBand.set(1000.0f, 0.5f, sr);
        antiLagFlameBand.set(kAntiLagFlameHz, 0.7f, sr);
        whistleBand.set(1900.0f, 16.0f, sr);
        whistleLow.set(1000.0f, 16.0f, sr);
        releaseCoef = 1.0f - km::exp(-1.0f / (kReleaseFollowS * sr));
        airBand.set(3200.0f, 0.7f, sr);
        wastegateBand.set(5200.0f, 1.0f, sr);
        releaseBand.set(2600.0f, 0.8f, sr);
        windBand.set(1500.0f, 0.5f, sr);
        roadTone.set(2200.0f, 0.707f, sr);
        tyreBand.set(900.0f, 0.7f, sr);
        squealBand.set(1650.0f, 7.0f, sr);

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

    /** Mêmes pièces, autres dimensions : le changement s'entend sans rien interrompre. */
    static bool sameParts(const EngineProfile& a, const EngineProfile& b) {
        return a.firingCount == b.firingCount && a.firing == b.firing && a.firingGain == b.firingGain && a.headerM == b.headerM &&
               a.exhaust == b.exhaust && a.exhaustScale == b.exhaustScale && a.diesel == b.diesel && a.turbo == b.turbo &&
               a.electric == b.electric && a.routing == b.routing && a.bank == b.bank && a.twoBanks == b.twoBanks && sameShell(a.car, b.car) &&
               a.airFilter == b.airFilter && (a.airFilter == AirFilter::Stock || (a.boreM == b.boreM && a.strokeM == b.strokeM && a.cylinders == b.cylinders && a.stackM == b.stackM));
    }

    /** La même caisse aux mêmes endroits : seule l'isolation change, et l'habitacle se recalcule sans couper le moteur. */
    static bool sameShell(const CarAcoustics& a, const CarAcoustics& b) {
        CarAcoustics other = b;
        other.insulation = a.insulation;
        other.windowsOpen = a.windowsOpen;
        return a == other;
    }

    /** Ce qui découle des dimensions du moteur, recalculé sans toucher aux ondes en cours. */
    void applyDimensions(const EngineProfile& p) {
        // Bouffée d'échappement : le cylindre se vide d'autant plus vite que la soupape
        // est grande face au volume à vider, soit une durée qui croît avec la course.
        // Elle s'allonge moins vite que la course : la soupape s'ouvre sur une pression déjà plus forte.
        blowdownBase = km::coerceAtLeast(0.002f * km::sqrt(p.strokeM / 0.09f), 0.0005f);
        backStroke = 0.22f * km::sqrt(km::max(p.boreM * p.boreM * p.strokeM, 1e-6f) / kReferenceCylinder);
        const float volume = p.boreM * p.boreM * p.strokeM * static_cast<float>(p.cylinders);
        const float sizeScale = km::pow(km::coerceIn(4.0f * kReferenceCylinder / km::max(volume, 1e-6f), 0.2f, 4.0f), 0.27f);
        // Une bouffée lente va moins vite : son souffle est plus faible et plus grave.
        const float cylinder = km::max(p.boreM * p.boreM * p.strokeM, 1e-6f);
        // Le débit crête d'une bouffée : le volume rejeté, sur sa durée. La ligne fait le reste.
        linePressure = kBlowVolume * 0.7854f * cylinder / blowdownBase * kMonopoleAt1m;
        jetShape = km::sqrt(km::coerceIn(kReferenceCylinder / cylinder, 0.15f, 3.0f));
        turbulence.set(km::coerceIn(900.0f * jetShape, 350.0f, 1800.0f), 0.6f, sr);
        popBlast.set(spec.popCutoffHz, 0.7f, sr);
        popBody.set(p.popBodyHz, kPopBodyQ, sr);
        popBodyHigh.set(p.popBodyHz * 2.3f, kPopBodyHighQ, sr);
        starter.setup(p.starter, sizeScale);
        // Plus de pression, turbine plus rapide : un sifflement plus aigu et plus fort.
        const float bar = km::sqrt(km::max(p.boostBar, 0.1f));
        boostPitch = km::coerceIn(bar, 0.7f, 1.6f);
        boostLevel = km::coerceIn(bar, 0.6f, 1.8f);
    }

    void applyProfile(const EngineProfile& p) {
        active = p;
        activeGeneration = wantedGeneration;
        spec = exhaustSpec(p.exhaust);
        spec.pipeM *= p.exhaustScale;
        // La ligne de ce moteur et de cette voiture : c'est long, et c'est pour cela qu'on change de moteur en silence.
        twoPaths = p.twoBanks;
        crossover = p.routing == ExhaustRouting::XPipe ? 0.4f : (p.routing == ExhaustRouting::HPipe ? 0.18f : 0.0f);
        if (!p.electric) {
            const double displacement = 0.7854 * static_cast<double>(p.boreM) * p.boreM * p.strokeM * p.cylinders;
            // Deux rangées séparées : chacune son tube, de section moitié.
            const std::vector<float>& impulse = line.compute(p.exhaust, p.exhaustScale, twoPaths ? displacement * 0.5 : displacement, twoPaths ? p.cylinders / 2 : p.cylinders);
            lineGain = line.gain();
            gas[0].line.setImpulse(impulse.data(), static_cast<int>(impulse.size()));
            if (twoPaths) gas[1].line.setImpulse(impulse.data(), static_cast<int>(impulse.size()));
            // Ce qui détone dans le tube sort par la ligne : autant retenu que le reste de ses médiums.
            spec.popOpening = km::pow(10.0f, static_cast<float>(-line.lossDb(100.0, 800.0)) / 20.0f);
        }
        otherFilter = !p.electric && p.airFilter != AirFilter::Stock;
        if (otherFilter) {
            IntakeGeometry geometry;
            geometry.displacementM3 = 0.7854 * static_cast<double>(p.boreM) * p.boreM * p.strokeM * p.cylinders;
            geometry.stackM = static_cast<double>(p.stackM);
            geometry.stackRadiusM = static_cast<double>(p.stackRadiusM);
            const std::vector<float>& inlet = intakePath.compute(p.airFilter, geometry);
            intakeResponse.setImpulse(inlet.data(), static_cast<int>(inlet.size()));
            // Sans boîte, il s'ouvre dans le compartiment moteur, et en sort avec lui.
            intakeOut = airFilterSpec(p.airFilter).underBonnet ? Source::Bay : Source::Intake;
        }
        field.setCar(p.car, !p.electric);
        if (presetPosition) applyListenerPreset();
        cabin.setCar(p.car);
        phase = 0.0f;
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
            headerDamp[c] = 0.0f;
            burnMemory[c] = 0.0f;
        }
        headerCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * kHeaderDampHz / sr);
        gustCoef = 1.0f - km::exp(-1.0f / (0.004f * sr));
        for (GasPath& path : gas) {
            path.jitterLine.fill(0.0f);
            path.jitterAt = 0;
            path.turbineWave = 0.0f;
        }
        turbineCoef = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 1500.0f / sr);
        applyDimensions(p);
    }

    float roadRandom() {
        std::uint32_t x = roadRng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        roadRng = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }

    float antiLagRandom() {
        std::uint32_t x = antiLagRng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        antiLagRng = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }

    float turboRandom() {
        std::uint32_t x = turboRng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        turboRng = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }

    float addedRandom() {
        std::uint32_t x = addedRng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        addedRng = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }

    float bankRandom() {
        std::uint32_t x = bankRng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        bankRng = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }

    /** Les positions toutes prêtes, à la mesure de cette voiture. */
    void applyListenerPreset() {
        const CarAcoustics& car = active.car;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float back = -car.lengthM / 2.0f;
        constexpr float kEarHeight = 1.6f;
        switch (listener) {
            case Listener::Cabin: field.setTarget(car.seat, 0.0f, kPresetSpeed); break;
            case Listener::EngineBay: field.setTarget({car.engine.x, car.engine.y + 0.1f, car.engine.z + 0.55f}, nan, kPresetSpeed); break;
            case Listener::Front: field.setTarget({-back + 3.0f, 0.5f, kEarHeight}, nan, kPresetSpeed); break;
            case Listener::Side: field.setTarget({0.0f, -5.0f, kEarHeight}, nan, kPresetSpeed); break;
            case Listener::Far: field.setTarget({-20.0f, -24.0f, kEarHeight}, nan, kPresetSpeed); break;
            case Listener::Pipe: {
                // L'oreille à 8 cm de la sortie, un peu de côté pour ne pas être dans le jet, et la tête derrière elle.
                const SourcePlace& outlet = car.sources[static_cast<std::size_t>(Source::ExhaustA)];
                const float side = outlet.at.y >= 0.0f ? 1.0f : -1.0f;
                constexpr float kBack = -0.82f;
                constexpr float kAcross = 0.57f;
                const float reach = 0.08f + acoustics::kHeadRadius;
                const Point3 head{outlet.at.x + kBack * reach, outlet.at.y + side * kAcross * reach, outlet.at.z + 0.02f};
                // L'oreille gauche vers la sortie : le regard est à sa droite.
                const float facing = static_cast<float>(std::atan2(static_cast<double>(kBack), static_cast<double>(-side * kAcross)));
                field.setTarget(head, facing, kPresetSpeed);
                break;
            }
            case Listener::Outside: field.setTarget({back - 5.0f, 0.0f, kEarHeight}, nan, kPresetSpeed); break;
        }
    }

    void control(float wantedRpm, float volume) {
        const float wantedTarget = wantedRpm;
        // Le démarreur s'entend avant même que le vilebrequin ait pris de la vitesse.
        const bool engineOn = wantedTarget >= kMinRpm || (driven && driveCrank);

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
        // Avec le vrai régime du modèle, pas de lissage : les rebonds du rupteur doivent s'entendre.
        if (driven) {
            rpmInput = wantedTarget;
            rpm += (rpmInput - rpm) * smoothing(0.004f);
        } else {
            rpmInput += (wantedTarget - rpmInput) * smoothing(kRpmTauS);
            rpm += (rpmInput - rpm) * smoothing(kRpmTauS);
        }
        const float previousSlow = slowRpm;
        slowRpm += (rpm - slowRpm) * smoothing(kSlopeTauS);
        const float slope = (slowRpm - previousSlow) / controlDt;

        // Le régime qui monte trahit l'accélérateur, et cette seule donnée suffit.
        const float wantedLoad = driven ? km::coerceIn(driveLoad, 0.0f, 1.0f) : km::coerceIn(kLoadCruise + slope / kFullLoadRpmPerS, 0.0f, 1.0f);
        load += (wantedLoad - load) * smoothing(wantedLoad > load ? kLoadAttackS : kLoadReleaseS);

        const EngineProfile& a = active;
        rpmFrac = km::coerceIn((rpm - a.idleRpm) / (a.redlineRpm - a.idleRpm), 0.0f, 1.0f);
        idleShake = (1.0f - rpmFrac) * (1.0f - rpmFrac) * (1.0f - rpmFrac) * (1.0f - rpmFrac);
        // À faible charge, le cylindre s'ouvre sur peu de pression : il se vide lentement, sans coup de bélier.
        blowRate = 1.0f / (blowdownBase * (1.0f + 3.5f * (1.0f - km::min(load, 1.0f))) * sr);

        // Le moteur ne tourne jamais rond : lente dérive, plus marquée au ralenti.
        const float driftNoise = random();
        wobble += (driftNoise * kRpmDrift - wobble) * smoothing(kDriftTauS);
        const float drift = 1.0f + wobble * (1.0f - 0.7f * rpmFrac);
        phaseInc = km::max(rpm, 0.0f) * drift / 120.0f / sr;

        // Pied levé, moteur lancé : l'injection se coupe, le moteur n'est plus qu'une pompe.
        // Un filet de gaz brûle encore : tenir un régime à vide ou rouler pied léger, ce n'est pas lever le pied.
        const bool lifted = driven && driveThrottle >= 0.0f ? driveThrottle < kLiftedThrottle : wantedLoad < kFuelCutLoad;
        // L'anti-lag n'est pas un lever de pied : il brûle, et rien ne se coupe.
        antiLagOn = turboDriven && driven && turboIn.antiLag.active;
        const bool cutting = engineOn && lifted && !antiLagOn && rpm > a.idleRpm * kOverrunMinIdleRatio;
        overrunSeconds = cutting ? overrunSeconds + controlDt : 0.0f;
        if (!cutting) poppedThisLift = false;
        const float overrunTarget = overrunSeconds > kOverrunDelayS ? 1.0f : 0.0f;
        overrun += (overrunTarget - overrun) * smoothing(kOverrunTauS);
        popHeat *= km::exp(-controlDt / kPopHeatTauS);
        popCooldown = km::max(popCooldown - controlDt, 0.0f);
        // Plein gaz, le cylindre s'ouvre sur bien plus de pression : c'est l'écart qui fait la vie du son.
        const float boostFill = driven ? 0.4f * driveBoost * km::coerceIn(a.boostBar, 0.3f, 2.5f) : 0.0f;
        const float fill = 0.2f + 0.8f * load * load + boostFill;
        openPressure = fill * (1.0f - kOverrunPressureDrop * overrun);
        // Le gaz finit de brûler pendant la détente : la soupape s'ouvre sur autant de pression de plus qu'il reste de chaleur.
        if (antiLagOn) openPressure *= turboIn.antiLag.evoRatio;
        if (antiLagOn || antiLagFlame > 0.0f) {
            const float flameWanted = antiLagOn ? km::sqrt(km::max(turboIn.antiLag.heatShare, 0.0f)) : 0.0f;
            antiLagFlame += (flameWanted - antiLagFlame) * smoothing(0.03f);
            if (antiLagFlame < 1e-4f && !antiLagOn) antiLagFlame = 0.0f;
        }
        // Le piston continue de pomper de l'air, combustion ou non.
        pumpLevel += (km::max(openPressure, 0.25f) - pumpLevel) * smoothing(0.004f);
        // Rupteur : la combustion s'arrête net, explosion par explosion. Le papillon, lui, reste ouvert,
        // mais plus rien ne pousse : tout ce qui suit le débit tombe avec elle.
        const bool cutNow = driven && driveCut && !driveCrank;
        if (cutNow) {
            openPressure *= 0.05f;
            pumpLevel += (0.05f - pumpLevel) * smoothing(0.004f);
        }
        cutGate += ((cutNow ? 0.1f : 1.0f) - cutGate) * smoothing(0.006f);
        if (driveCrank) openPressure = 0.1f;
        // Du carburant arrive encore pendant la coupure : il peut s'enflammer à la reprise.
        if (wasCut && !cutNow) popOnRecovery();
        wasCut = cutNow;
        // Le gaz à faible vitesse souffle peu : ce bruit croît avec le débit, donc la charge et le régime.
        // D'après des mesures sur un V6 : à pleine charge, ce bruit croît très vite avec le régime,
        // de l'ordre de la puissance 5,7 ; à charge partielle, il reste à peu près stable.
        const float rpmRatio = km::coerceIn(rpm / 4500.0f, 0.15f, 1.8f);
        const float steep = rpmRatio * rpmRatio * km::sqrt(km::sqrt(rpmRatio)) * km::sqrt(rpmRatio);
        // Le plancher vient de vrais ralentis : sans lui, il manque 8 dB entre 250 Hz et 2 kHz. Il suit les bouffées, et ne fait donc
        // pas de fond continu derrière le moteur.
        flowNoise = (0.7f + km::min(load * load, 1.0f) * steep) * crankHush * cutGate;
        wasCranking = driveCrank;
        starterLevel += ((driveCrank ? 1.0f : 0.0f) - starterLevel) * smoothing(driveCrank ? 0.012f : 0.025f);
        crankHush = 1.0f - 0.85f * starterLevel;
        const float carSpeed = km::coerceIn(roadSpeed / kRoadSpeedRef, 0.0f, 3.0f);
        roadLevel += (carSpeed - roadLevel) * smoothing(0.1f);
        squealLevel += (km::coerceIn((km::abs(roadSlip) - 2.5f) / 5.0f, 0.0f, 1.0f) - squealLevel) * smoothing(0.04f);
        const float at70 = roadLevel * kRoadSpeedRef / 19.44f;
        tyreLevel = kTyrePa * km::pow(at70, 1.7f);
        windLevel = kWindPa * at70 * at70 * at70;
        hashLevel = (0.8f + 0.2f * km::sqrt(rpmFrac)) * (0.6f + 0.4f * load) * (a.diesel ? 3.0f : 2.0f);
        updateLevels();

        if (a.electric) tone.set(km::min(a.lowpassIdleHz + (a.lowpassRedlineHz - a.lowpassIdleHz) * rpmFrac, sr * 0.3f), 0.707f, sr);
        if (a.turbo) updateTurbo(wantedLoad);
        // Pendant le lancement, l'air pompé par l'échappement est un souffle à peine audible.
        const float gainTarget = (!engineOn || switching) ? 0.0f : 1.0f - 0.97f * starterLevel;
        const float smoothed = gain + (gainTarget - gain) * smoothing(kGainTauS);
        gainStep = (smoothed - gain) / static_cast<float>(kControlSamples);
        // Ces bruits ont leur propre niveau.
        const float eventTarget = (engineOn && !switching) ? 1.0f : 0.0f;
        // Le claquement du solénoïde est le tout premier son : pas de fondu qui l'avalerait.
        const float eventSmoothed = eventGain + (eventTarget - eventGain) * smoothing(driveCrank ? 0.001f : kGainTauS);
        eventGainStep = (eventSmoothed - eventGain) / static_cast<float>(kControlSamples);

        field.control(kControlSamples);
        updateCabin();
        // Tout près du bloc, il garde tous ses aigus, quelle que soit la charge.
        noiseOpen += (field.bayOpen() - noiseOpen) * smoothing(0.08f);

        // Ce qui sort des haut-parleurs : le niveau réel, resserré. Figé moteur coupé, pour ne pas tout amplifier au démarrage.
        if (engineOn) {
            const float heardDb = meter.db();
            const float attack = field.moving() ? kToneMovingS : kToneAttackS;
            toneDb += (heardDb - toneDb) * smoothing(heardDb > toneDb ? attack : kToneReleaseS);
        }
        // La marge : le gain ne laisse pas le niveau efficace dépasser ce que supporte la sortie. Le limiteur, lui,
        // ne s'occupe plus que des crêtes, et ne travaille plus en continu à pleine charge.
        const float effective = km::sqrt(km::max(outputEnergy, 1e-10f));
        const float wanted = km::min(toneGain(toneDb), kOutputRmsMax / effective) * volume;
        const float nextOut = outGain + (wanted - outGain) * smoothing(kOutFadeS);
        outGainStep = (nextOut - outGain) / static_cast<float>(kControlSamples);
    }

    /** L'habitacle ne tourne que si on y est, et se recalcule sans couper le moteur quand l'isolation change. */
    void updateCabin() {
        const float inside = field.inside();
        if (inside > 0.0f && !cabinActive) {
            cabin.reset();
            cabinActive = true;
        } else if (inside <= 0.0f && cabinLevel <= 0.0f) {
            cabinActive = false;
        }
        if (cabinPending) {
            cabinFade += (0.0f - cabinFade) * cabinCoef;
            if (cabinFade < 0.01f) {
                cabin.setCar(cabinCar);
                cabinPending = false;
            }
        } else {
            cabinFade += (1.0f - cabinFade) * cabinCoef;
        }
        const float target = km::sqrt(inside) * cabinFade;
        cabinStep = (target - cabinLevel) / static_cast<float>(kControlSamples);
    }

    /** Les niveaux absolus qui suivent le régime et la charge : voir docs/modele.md. */
    void updateLevels() {
        const EngineProfile& a = active;
        if (a.electric) return;
        const float rpmNow = km::max(rpm, 200.0f);
        // Bloc : la loi d'Anderton donne les dB(A) à 1 m, à pleine charge, selon le régime et l'alésage.
        const float bore = a.boreM * 1000.0f;
        const float logN = km::log10(rpmNow);
        const float logB = km::log10(bore);
        float blockDb = 50.0f * logN + 60.0f * logB - 203.0f;
        if (a.diesel) blockDb = a.turbo ? 40.0f * logN + 50.0f * logB - 135.0f : 30.0f * logN + 50.0f * logB - 106.0f;
        // Sans charge, un moteur essence perd un peu ; un diesel presque rien (Austen et Priede, 1959 ; mesures sur une Saturn : presque rien non plus).
        blockDb -= (a.diesel ? 2.0f : 5.0f) * (1.0f - km::min(load, 1.0f));
        // Au ralenti, accessoires et distribution tiennent un plancher.
        const float floorDb = 65.0f + 15.0f * km::log10(bore / 86.0f) + (a.diesel ? 7.0f : 0.0f);
        blockLevel = kBlockPa * km::sqrt(km::pow(10.0f, blockDb / 10.0f) + km::pow(10.0f, floorDb / 10.0f));
        // La caisse ne suit pas le bloc : les supports filtrent, et sa part recule quand le régime monte.
        bodyShare = kBodyShare * km::min(km::pow(a.idleRpm / rpmNow, 0.7f), 1.0f);

        // Les raies et le front de la bouffée donnent déjà la pente : il manque 3,6 dB par octave pour retrouver les 10 dB
        // par doublement du régime que montrent les registres, du ralenti au régime de l'essai à l'arrêt.
        exhaustLaw = km::pow(rpmNow / 3750.0f, 0.6f);
        // Un diesel a aussi une turbine et un filtre à particules : les registres le donnent 4 dB sous un essence de même taille.
        if (a.diesel) exhaustLaw *= kDieselFilter;

        // Le débit moyen : le cylindre rejette ce qu'il a avalé, dilaté par la chaleur. Vitesse dans le tube de sortie.
        const float displacement = 0.7854f * a.boreM * a.boreM * a.strokeM * static_cast<float>(a.cylinders);
        const float fillNow = 0.1f + 0.85f * load + (a.turbo ? 0.5f * boost * a.boostBar : 0.0f);
        const float hot = 1.6f + 1.0f * load;
        float flowNow = displacement * rpmNow / 120.0f * fillNow * hot;
        // L'anti-lag envoie plus de gaz, et plus chaud, que le remplissage ne le dit : le calcul le donne.
        if (antiLagOn) flowNow = turboIn.antiLag.exhaustM3s;
        const float paths = (twoPaths || a.routing != ExhaustRouting::Single) ? 2.0f : 1.0f;
        const float radius = a.car.outletRadiusM;
        outletArea = static_cast<float>(km::kPi) * radius * radius;
        jetMean = flowNow / paths / outletArea;
        jetDensity = 1.204f / hot;
        field.setJetSpeed(km::min(jetMean, 200.0f));

        // Le bruit d'écoulement dans le silencieux ne traverse plus rien : il suit la vitesse (Selamet : 57 dB par décade de régime).
        // Au ralenti, un souffle de fond demeure : la coque du silencieux, les fuites, le gaz qui brasse.
        tailLevel = (spec.tailNoise * kTailPa * km::pow(km::max(jetMean, 3.0f) / 100.0f, 2.85f) + spec.tailFloor * kTailFloorPa) * cutGate;
        // L'admission : le remplissage se fait entendre, surtout papillon ouvert.
        inletLevel = kInletPa * (0.12f + 0.88f * load * load) * km::pow(rpmNow / 5000.0f, 1.25f) * crankHush * displacement / 0.002f;
    }

    /** Pression : monte avec retard sous charge. Turbine : continue sur son élan pied levé. */
    void updateTurbo(float wantedLoad) {
        if (turboDriven && driven) {
            updateRealTurbo();
            return;
        }
        const float pressureNow = driven ? km::coerceIn(driveBoost, 0.0f, 1.0f) : km::coerceIn(km::sqrt(rpmFrac) * ((load - 0.3f) / 0.7f), 0.0f, 1.0f);
        if (wantedLoad < kOverrunLoad && boost > kTurboReleaseMinBoost && releaseEnv < 0.05f) {
            releaseEnv = boost * boostLevel;
            releaseRise = 0.0f;
        }
        // Régime stable ne veut pas dire pied levé : la pression ne s'effondre qu'au lever de pied.
        const float boostTau = pressureNow > boost ? kTurboSpoolS : (wantedLoad < kOverrunLoad ? kTurboFallS : kTurboHoldS);
        // Le modèle a déjà son retard de turbo : on ne l'ajoute pas une seconde fois.
        if (driven) boost = pressureNow;
        else boost += (pressureNow - boost) * smoothing(boostTau);
        const float turbineTarget = km::max(boost, 0.06f * rpmFrac);
        turbine += (turbineTarget - turbine) * smoothing(turbineTarget > turbine ? kTurbineUpS : kTurbineDownS);
        whistleBand.set(km::min(1900.0f + 5200.0f * turbine * boostPitch, km::min(9000.0f, sr * 0.4f)), 16.0f, sr);
        wastegate += (km::coerceIn((boost - 0.8f) / 0.2f, 0.0f, 1.0f) * load - wastegate) * smoothing(0.12f);
        const float releaseHz = active.turboRelease == TurboRelease::Flutter ? 1250.0f : 1800.0f + 2000.0f * releaseEnv;
        releaseBand.set(releaseHz, 0.8f, sr);
    }

    /**
     * Le turbo calculé : rien à deviner. Le sifflement est à la vitesse de l'arbre (l'ordre 2 domine, le balourd donne l'ordre 1),
     * la décharge et le flutter sont les débits, tels qu'ils varient.
     */
    void updateRealTurbo() {
        boost = km::coerceIn(driveBoost, 0.0f, 1.0f);
        turbine = km::min(turboIn.shaftShare, 1.3f);
        const float top = km::min(9000.0f, sr * 0.4f);
        whistleBand.set(km::coerceIn(2.0f * turboIn.shaftHz, 200.0f, top), 16.0f, sr);
        whistleLow.set(km::coerceIn(turboIn.shaftHz, 100.0f, top), 16.0f, sr);
        whistleFlow += (km::min(turboIn.flowShare, 1.0f) - whistleFlow) * smoothing(0.05f);
        wastegate += (km::min(2.0f * turboIn.wastegateFlowShare, 1.0f) - wastegate) * smoothing(0.05f);
        // La soupape tenue fermée par l'anti-lag, l'air ne peut revenir que par la roue, sauf quand il part au collecteur.
        const bool flutter = active.turboRelease == TurboRelease::Flutter || antiLagOn;
        releaseGoal = km::min(flutter ? kBackflowLevel * turboIn.backflowShare : turboIn.blowOffShare, 1.5f);
        releaseBand.set(flutter ? 1250.0f : 1800.0f + 2000.0f * km::min(releaseEnv, 1.0f), 0.8f, sr);
    }

    /** Du carburant imbrûlé s'enflamme dans le tube : d'autant plus juste après le lever de pied. */
    void maybePop() {
        const EngineProfile& p = active;
        if (p.pops == PopLevel::Off || overrun < 0.5f || rpm < kPopMinRpm || popCooldown > 0.0f) return;
        const float firingsPerSecond = km::max(rpm / 120.0f * static_cast<float>(cylinders), 1.0f);
        const float freshness = km::exp(-overrunSeconds / kPopWindowS);
        const float rate = popsPerSecond(p.pops) * spec.popRate * (0.1f + 0.9f * freshness) * (0.5f + 0.5f * rpmFrac) * (1.0f + popHeat);
        const float roll = random() * 0.5f + 0.5f;
        // Beaucoup de carburant imbrûlé : le premier coup part presque à chaque lever de pied.
        const bool firstBang = p.pops == PopLevel::Strong && !poppedThisLift && overrunSeconds > 0.12f;
        if (!firstBang && roll >= rate / firingsPerSecond) return;
        poppedThisLift = true;
        // Une détonation est franche : pas de poussière de petits craquements.
        const float draw = random() * 0.5f + 0.5f;
        firePop(draw);
    }

    void firePop(float draw) {
        const EngineProfile& p = active;
        const float strength = p.pops == PopLevel::Strong ? 0.75f : 0.55f;
        popEnv = (0.55f + 0.45f * draw * draw) * strength * p.popForce;
        popAttack = 0.0f;
        popDecay = km::exp(-1.0f / ((spec.popDecayS * (0.7f + 0.6f * draw)) * sr));
        popHeat = km::min(popHeat + 0.5f, 1.0f);
        ++popCount;
        popCooldown = kPopMinGapS;
    }

    /** Ce qui arrive imbrûlé au collecteur peut partir d'un coup, s'il y fait assez chaud. Même chemin que les détonations. */
    void maybeAntiLagPop(bool fires) {
        const AntiLag kind = turboIn.antiLag.kind;
        const bool cutting = kind == AntiLag::CylinderCut;
        const float chance = cutting ? (fires ? 0.0f : kAntiLagBangCut) : (kind == AntiLag::Retard ? kAntiLagBangRetard : kAntiLagBangAir);
        // La chaleur du collecteur, ouverture par ouverture : seuls les cylindres coupés portent cette charge.
        const float eventsPerSecond = km::max(rpm / 120.0f * static_cast<float>(cylinders), 1.0f) * (cutting ? 1.0f - static_cast<float>(kAntiLagFiredShare) : 1.0f);
        const float slug = cutting ? kAntiLagSlugCut : (kind == AntiLag::Retard ? kAntiLagSlugRetard : kAntiLagSlugAir);
        const float size = km::sqrt(km::max(turboIn.antiLag.heatW, 0.0f) / eventsPerSecond * slug / kAntiLagPopJoules);
        const float roll = antiLagRandom() * 0.5f + 0.5f;
        const float draw = antiLagRandom() * 0.5f + 0.5f;
        if (popCooldown > 0.0f || roll >= chance * turboIn.antiLag.ignition) return;
        popEnv = (0.55f + 0.45f * draw * draw) * kAntiLagBangForce * size * active.popForce;
        popAttack = 0.0f;
        popDecay = km::exp(-1.0f / ((spec.popDecayS * (0.7f + 0.6f * draw)) * sr));
        ++popCount;
        popCooldown = kPopMinGapS;
    }

    /** La flamme du collecteur : un grondement, sans rythme propre. */
    float antiLagFlameSample() {
        antiLagFlameBand.process(antiLagRandom());
        return antiLagFlameBand.bp * antiLagFlame * kAntiLagFlameInPipe;
    }

    void popOnRecovery() {
        const EngineProfile& p = active;
        if (p.pops == PopLevel::Off || popCooldown > 0.0f) return;
        const float chance = p.pops == PopLevel::Strong ? 0.55f : 0.18f;
        if (random() * 0.5f + 0.5f >= chance) return;
        firePop(random() * 0.5f + 0.5f);
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
        popBus += (popBody.bp * kPopBody + popBodyHigh.bp * kPopBodyHigh + pressureNow * kPopBlast) * spec.popOpening;
        return pressureNow * kPopInPipe;
    }

    void realTurboSample() {
        const float whistleNoise = turboRandom();
        whistleBand.process(whistleNoise);
        whistleLow.process(whistleNoise);
        const float airNoise = turboRandom();
        airBand.process(airNoise);
        const float shaft = turbine * turbine * kTurboWhistle * boostLevel * (0.35f + 0.65f * whistleFlow);
        float out = (whistleBand.bp + kWhistleLowShare * whistleLow.bp) * shaft;
        out += airBand.bp * boost * boost * kTurboAir;
        if (wastegate > 1e-3f) {
            flutterPhase += kWastegateRattleHz / sr;
            if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
            const float gateNoise = turboRandom();
            wastegateBand.process(gateNoise);
            out += wastegateBand.bp * wastegate * kTurboWastegate * (0.72f + 0.28f * sinFast(flutterPhase));
        }
        // Pas d'enveloppe : le niveau suit le débit, sur quelques millisecondes pour ne pas cliquer.
        releaseEnv += (releaseGoal - releaseEnv) * releaseCoef;
        if (releaseEnv > 1e-3f) {
            const float releaseNoise = turboRandom();
            releaseBand.process(releaseNoise);
            out += releaseBand.bp * releaseEnv * kTurboRelease;
        }
        turboBus += out;
    }

    void turboSample() {
        if (turboDriven && driven) {
            realTurboSample();
            return;
        }
        const float whistleNoise = random();
        whistleBand.process(whistleNoise);
        const float airNoise = random();
        airBand.process(airNoise);
        float out = whistleBand.bp * turbine * turbine * kTurboWhistle * boostLevel * (0.35f + 0.65f * load);
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
        turboBus += out;
    }

    void combustionSample() {
        const EngineProfile& p = active;
        popBus = 0.0f;
        turboBus = 0.0f;
        // Le vilebrequin accélère à chaque explosion et ralentit à chaque compression, surtout au ralenti.
        const float acyclism = 1.0f + idleShake * 0.05f * sinFast(phase * static_cast<float>(cylinders));
        phase += phaseInc * acyclism;
        if (phase >= 1.0f) phase -= 1.0f;

        float collector[2] = {0.0f, 0.0f};
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
                    // Une combustion hérite en partie de la précédente : gaz résiduels, mélange.
                    burnMemory[c] = burnMemory[c] * kCombustionMemory + (first + second) * (1.0f - kCombustionMemory);
                    // À bas régime, le mélange brûle mal : les combustions varient bien plus, jusqu'au raté partiel.
                    const float spread = km::max(1.0f + kCombustionSpread * burnMemory[c] * (1.0f + 1.6f * idleShake), 0.25f);
                    pressure[c] = km::max(openPressure * p.firingGain[c] * spread, 0.02f);
                    // Chaque bouffée a sa force de souffle : rafales, pas un bruit égal.
                    const float gustDraw = random();
                    gust = 0.75f + 0.5f * gustDraw * gustDraw;
                    nextTiming[c] = random() * (kTimingSpread + kPathJitterS * km::max(rpm, 300.0f) / 120.0f);
                    maybePop();
                    // La coupure de cylindres : sans explosion, ni pression à l'ouverture, ni coup dans la structure.
                    bool fires = true;
                    if (antiLagOn) {
                        if (turboIn.antiLag.kind == AntiLag::CylinderCut) fires = antiLagFires(antiLagEvent++);
                        if (!fires) pressure[c] = km::max(pressure[c] * (1.0f - kOverrunPressureDrop), 0.02f);
                        maybeAntiLagPop(fires);
                    }
                    // La combustion frappe la structure : le bloc rayonne un coup à chaque explosion.
                    if (fires) blockEnv = km::max(blockEnv, spread);
                    if (p.diesel) knockEnv = km::max(knockEnv, spread);
                }
                // La soupape s'ouvre vite : la bouffée a un front raide, loin d'une arche arrondie. Mais la came
                // décolle en douceur, sur un angle fixe : un front long au ralenti, très court à haut régime.
                const float ramp = km::min((theta - kEvo) / kEvoRamp, 1.0f);
                const float lift = km::sqrt(km::max(sinFast((theta - kEvo) / (kEvc - kEvo) * 0.5f), 0.0f)) * ramp * ramp * (3.0f - 2.0f * ramp);
                const float escape = pressure[c] * lift;
                pressure[c] = km::max(pressure[c] - escape * blowRate, 0.0f);
                // La bouffée, puis le piston qui chasse ce qui reste.
                flow = escape + backStroke * lift * pumpLevel;
            } else if (exhaustOpen[c]) {
                // Une soupape ne s'entend qu'en retombant sur son siège.
                exhaustOpen[c] = false;
                valve = true;
            }

            if (theta >= kIvo && theta < kIvc) {
                if (!intakeOpen[c]) intakeOpen[c] = true;
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
            // Parois et coudes du collecteur : l'onde y perd d'abord ses aigus.
            headerDamp[c] += (line[next] - headerDamp[c]) * headerCoef;
            collector[twoPaths ? p.bank[static_cast<std::size_t>(c)] : 0] += headerDamp[c];
            line[slot] = flow;
            headerSlot[c] = next;
        }

        float detonation = popSample();
        if (antiLagFlame > 0.0f) detonation += antiLagFlameSample();
        float waves[2] = {collector[0], collector[1]};
        if (twoPaths && crossover > 0.0f) {
            // Un pont entre les rangées : chacune reçoit une part des bouffées de l'autre.
            const float a = waves[0];
            const float b = waves[1];
            waves[0] = a + (b - a) * crossover;
            waves[1] = b + (a - b) * crossover;
        }
        gustSmooth += (gust - gustSmooth) * gustCoef;

        const float outA = gasPathSample(gas[0], waves[0], detonation, false);
        float exhaustA = outA;
        float exhaustB = 0.0f;
        if (twoPaths) {
            exhaustB = gasPathSample(gas[1], waves[1], detonation, true);
        } else if (active.routing != ExhaustRouting::Single) {
            // Une seule ligne, deux sorties : le débit se partage, et les deux restent en phase.
            exhaustA *= 0.5f;
            exhaustB = exhaustA;
        }

        // Grondement : gaz et carrosserie, sans rythme propre. Il suit le niveau de la ligne.
        const float rumbleNoise = random();
        rumble.process(rumbleNoise);
        const float rumbled = rumble.lp * kRumbleLevel * (0.4f + 0.6f * load) * (0.5f + 0.5f * rpmFrac) * crankHush * cutGate * lineGain * linePressure * exhaustLaw;
        const float rumbleShare = (twoPaths || active.routing != ExhaustRouting::Single) ? 0.5f : 1.0f;
        if (field.inJet()) jetSample(0);
        const float gasA = exhaustA + rumbled * rumbleShare;
        const float gasB = exhaustB + rumbled * rumbleShare;

        const float intakeNoise = random();
        intakeBand.process(intakeNoise);
        // Pied levé, seul le bas du bruit reste ; les aigus reviennent avec la charge.
        const float bright = km::max(km::min(load * load, 1.0f), noiseOpen);
        intakeTop += (intakeBand.bp - intakeTop) * noiseTopCoef;
        const float intakeHeard = intakeTop + (intakeBand.bp - intakeTop) * bright;
        const float inducted = intakeHeard * intake * kIntakeLevel * (0.2f + 0.8f * load) * (0.4f + 0.6f * rpmFrac) * crankHush;

        // Mécanique : souffle aigu du moteur, et claquements de soupapes.
        const float hashNoise = random();
        hashBand.process(hashNoise);
        // Petits cylindres, pièces légères et rapides : la mécanique s'entend plus haut et plus fort.
        const float metal = km::coerceIn(jetShape, 0.45f, 1.3f);
        hashTop += (hashBand.bp - hashTop) * noiseTopCoef;
        const float hashHeard = hashTop + (hashBand.bp - hashTop) * bright;
        float mechanical = hashHeard * kHashLevel * hashLevel * metal * km::sqrt(metal) * crankHush;
        const float valveDraw = random();
        if (valve) {
            // Jamais deux chocs identiques, et une attaque arrondie : un tic sourd, pas un claquement.
            valveEnv = 0.5f + 0.25f * (valveDraw + 1.0f);
            valveRise = 0.0f;
        }
        valveRise = km::min(valveRise + 1.0f / (0.0005f * sr), 1.0f);
        const float tick = random() * valveEnv * valveRise;
        valveBand.process(tick);
        valveBandHigh.process(tick);
        mechanical += (valveBand.bp + 0.35f * valveBandHigh.bp) * kValveLevel * (0.5f + 0.5f * rpmFrac) * metal;
        valveEnv *= valveDecay;

        if (p.diesel) {
            const float knockNoise = random();
            knockBand.process(knockNoise);
            // Sans combustion, plus de claquement.
            mechanical += knockBand.bp * knockEnv * kDieselKnock * (1.0f - 0.5f * load) * (1.0f - overrun);
            knockEnv *= knockDecay;
        }
        if (p.turbo) turboSample();

        // Ce que le bloc rayonne : la combustion, et un fond mécanique large.
        const float blockNoise = addedRandom();
        blockBand.process(blockNoise);
        blockBody.process(collector[0] + collector[1]);
        // Il suit les explosions, plus qu'un fond continu, et perd ses aigus : les parois du bloc rayonnent peu au-dessus de 4 à 5 kHz.
        // Plus continu et sans cette coupure, il sonnait comme un bruit blanc sous le capot : 4 à 8 dB au-dessus de vrais compartiments, de 2 à 8 kHz.
        blockTop += (blockBand.bp - blockTop) * blockTopCoef;
        float block = blockTop * (0.55f + 1.65f * km::min(blockEnv, 1.5f)) * (0.4f + 0.6f * cutGate);
        block += blockBody.bp * 0.25f * cutGate;
        blockEnv *= blockDecay;

        // L'admission : chaque cylindre aspire, et le conduit résonne.
        const float inletRate = (intake - inletLast) * sr / 1000.0f;
        inletLast = intake;
        inletTone.process(inletRate);

        // Le souffle du silencieux, à la sortie.
        const float tailNoise = addedRandom();
        tailBand.process(tailNoise);
        // La bouche ne rayonne plus en (ka)² dans les aigus : ce souffle tombe vite au-dessus de quelques kilohertz.
        tailTop += (tailBand.bp - tailTop) * tailTopCoef;
        const float tail = (tailTop + (tailBand.bp - tailTop) * km::min(load * load, 1.0f)) * tailLevel;

        // Chaque bruit rejoint la source d'où il sort : c'est l'écoute qui décide ensuite ce qu'on en entend.
        popTop += (popBus - popTop) * popTopCoef;
        const float pops = popTop * spec.popOpening * kPopPa * eventGain;
        const bool twoOutlets = twoPaths || active.routing != ExhaustRouting::Single;
        stem(Source::ExhaustA) += (gasA + (twoOutlets ? tail * 0.7f : tail)) * gain + (twoOutlets ? pops * 0.5f : pops);
        stem(Source::ExhaustB) += twoOutlets ? (gasB + tail * 0.7f) * gain + pops * 0.5f : 0.0f;
        // Le compresseur et sa décharge sont sur le conduit d'admission : ils sortent avec lui.
        const float inletStem = (inducted * cutGate * kIntakePa + inletTone.bp * inletLevel * cutGate) * gain + turboBus * kTurboPa * eventGain;
        if (otherFilter) stem(intakeOut) += intakeResponse.process(inletStem);
        else stem(Source::Intake) += inletStem;
        const float engineNoise = (mechanical * (0.4f + 0.6f * cutGate) * kMechanicalPa + block) * blockLevel * gain;
        stem(Source::Bay) += engineNoise;
        // Le moteur fait vibrer toute la caisse : elle en rayonne une part, de partout, mais pas les aigus.
        bodyDamp += (engineNoise - bodyDamp) * bodyDampCoef;
        stem(Source::Underfloor) += bodyDamp * bodyShare;
        stem(Source::Bay) += starter.sample(rpm, driveCrank, starterLevel) * kStarterPa * eventGain;
        // Les supports moteur : la force des explosions, dont seuls les bas ordres passent.
        structure = (collector[0] + collector[1]) * kStructurePa * gain;
    }

    /**
     * Dans le jet, l'oreille ne reçoit plus un son mais du vent : la pression dynamique ½ρU², qui suit les bouffées,
     * et la turbulence, à spectre plat sous 300 Hz, de 116 dB à 12 m/s (Korhonen, 2021) et en U².
     */
    void jetSample(int) {
        const float noise = addedRandom();
        jetLow.process(noise);
        jetLower.process(jetLow.lp);
        const float turbulence = jetLower.lp * 2.4f;
        for (int outlet = 0; outlet < 2; ++outlet) {
            const float u = km::max(jetMean + jetFlow[static_cast<std::size_t>(outlet)] / outletArea, 0.0f);
            const float dynamic = 0.5f * jetDensity * (u * u - jetMean * jetMean);
            const float wind = 0.146f * 0.5f * jetDensity * jetMean * jetMean * turbulence;
            field.setJetSignal(outlet, (dynamic + wind) * gain);
        }
    }

    /** Du collecteur à la sortie : retour de turbine, gigue, souffle, puis toute la ligne. En pascals à 1 m. */
    float gasPathSample(GasPath& path, float collected, float detonation, bool second) {
        float wave = collected;
        if (active.turbo) {
            // La turbine prend son énergie aux bouffées : elles en ressortent plus douces et plus faibles.
            path.turbineWave += (wave - path.turbineWave) * turbineCoef;
            wave = path.turbineWave * (1.0f - 0.3f * boost);
        }
        wave += detonation;

        // La turbulence agite le gaz : une petite gigue d'échantillon à échantillon, qui défait les raies
        // progressivement vers les aigus, sans coupure.
        path.jitterLine[static_cast<std::size_t>(path.jitterAt)] = wave;
        const float jitterDraw = second ? bankRandom() : random();
        path.jitterState += (jitterDraw * 3.0f - path.jitterState) * jitterCoef;
        const float readAt = static_cast<float>(path.jitterAt) - static_cast<float>(kJitterDelay) - path.jitterState * jitterSamples;
        const float wrapped = readAt < 0.0f ? readAt + static_cast<float>(path.jitterLine.size()) : readAt;
        const int readIndex = static_cast<int>(wrapped);
        const float readFrac = wrapped - static_cast<float>(readIndex);
        const float older = path.jitterLine[static_cast<std::size_t>(readIndex)];
        const float newer = path.jitterLine[static_cast<std::size_t>((readIndex + 1) % static_cast<int>(path.jitterLine.size()))];
        const float shaken = older + (newer - older) * readFrac;
        path.jitterAt = path.jitterAt + 1 == static_cast<int>(path.jitterLine.size()) ? 0 : path.jitterAt + 1;

        // Le débit brasse l'air : un bruit proportionnel à ce qui passe, qui traverse la ligne comme le reste.
        const float noiseDraw = second ? bankRandom() : random();
        path.turbulenceBand.process(noiseDraw);
        // Seule la part qui varie agite l'air : le débit moyen ne fait pas de bruit à lui seul.
        path.flowMean += (shaken - path.flowMean) * flowMeanCoef;
        const float agitated = shaken + (shaken - path.flowMean) * path.turbulenceBand.lp * kTurbulenceGain * gustSmooth * flowNoise * jetShape;

        // Toute la ligne d'échappement : tubes, catalyseur, silencieux, sortie à l'air libre.
        const float raw = path.line.process(agitated) * linePressure * exhaustLaw;
        // Le débit qui sort, pour le jet : l'intégrale de ce que la pression rayonnée doit à sa dérivée.
        float& flow = jetFlow[second ? 1 : 0];
        flow = flow * jetLeak + raw / kMonopoleAt1m / sr;
        path.highPass.process(raw);
        const float firstPass = raw - path.highPass.lp - path.highPass.k * path.highPass.bp;
        path.highPassSecond.process(firstPass);
        return firstPass - path.highPassSecond.lp - path.highPassSecond.k * path.highPassSecond.bp;
    }

    void electricSample() {
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
        stem(Source::Bay) += tone.lp * kElectricPa * gain;
    }

    /** Limiteur à gain variable, le même pour les deux oreilles : garde la forme d'onde et l'image. */
    void limit(float& left, float& right) {
        limiterPeak = km::max(km::max(km::abs(left), km::abs(right)), limiterPeak * limiterRelease);
        const float wantedGain = limiterPeak > kLimitCeiling ? kLimitCeiling / limiterPeak : 1.0f;
        limiterGain += (wantedGain - limiterGain) * (wantedGain < limiterGain ? limiterAttack : limiterRecover);
        // Dernier filet pour la milliseconde d'attaque du limiteur.
        const auto knee = [](float y) {
            const float a = km::abs(y);
            return a <= kSoftKnee ? y : km::sign(y) * (kSoftKnee + kKneeRange * km::tanh((a - kSoftKnee) / kKneeRange));
        };
        left = knee(left * limiterGain);
        right = knee(right * limiterGain);
    }

    /**
     * Les bruits qui sortent de sous le plancher, au milieu de la voiture : boîte de vitesses, transmission.
     * C'est une source placée : l'écoute la traite selon sa position. À l'échelle du bus d'événements.
     */
    float underfloorSample() { return gearbox.sample(phase); }

    /** Vent et pneus : ils ne viennent pas du moteur, et n'ont pas son niveau. */
    void surroundings() {
        stem(Source::Underfloor) += underfloorSample() * kUnderfloorPa * eventGain;
        if (roadLevel > 1e-3f || squealLevel > 1e-3f) {
            const float windDraw = roadRandom();
            windBand.process(windDraw);
            const float tyreDraw = roadRandom();
            tyreBand.process(tyreDraw);
            // Le pneu suit 34·lg(v) (CNOSSOS, à 7,5 m : 72 dB(A) à 70 km/h sur revêtement d'essai), l'air 60·lg(v) : il passe devant vers 130 km/h.
            // Peu d'énergie vers le haut (CNOSSOS : 13 dB de moins à 4 kHz qu'à 1 kHz, 23 dB à 8 kHz). Trop riche en aigus,
            // ce bruit couvrait tout, comme une pluie, dès que la voiture roulait.
            roadTone.process(tyreBand.bp * tyreLevel + windBand.bp * windLevel);
            float road = roadTone.lp;
            if (squealLevel > 1e-3f) {
                const float squealDraw = roadRandom();
                squealBand.process(squealDraw);
                road += squealBand.bp * squealLevel * kSquealPa;
            }
            stem(Source::Road) += road * eventGain;
        }
    }

    float& stem(Source source) { return stems[static_cast<std::size_t>(source)]; }

    void nextFrame(float wantedRpm, float volume, float& left, float& right) {
        if (controlLeft == 0) {
            control(wantedRpm, volume);
            controlLeft = kControlSamples;
        }
        --controlLeft;
        stems.fill(0.0f);
        structure = 0.0f;
        if (active.electric) electricSample();
        else combustionSample();
        surroundings();
        eventGain += eventGainStep;
        gain += gainStep;

        float l = 0.0f, r = 0.0f;
        field.process(stems, l, r);
        if (cabinActive) {
            float insideL = 0.0f, insideR = 0.0f;
            cabin.process(stems, structure, insideL, insideR, stereo);
            l += insideL * cabinLevel;
            r += insideR * cabinLevel;
            cabinLevel += cabinStep;
        }
        // Le sonomètre lit la pression, avant le volume.
        if (stereo) meter.add(l, r);
        else meter.addMono(l);
        const float power = stereo ? 0.5f * (l * l + r * r) : l * l;
        outputEnergy += (power - outputEnergy) * outputCoef;
        left = l * outGain;
        right = r * outGain;
        outGain += outGainStep;
        limit(left, right);
    }
};

EngineSound::EngineSound(int sampleRate, std::int32_t seed) : s_(std::make_unique<State>(sampleRate, seed)) {}

EngineSound::~EngineSound() = default;

void EngineSound::setProfile(const EngineProfile& profile) {
    // Même moteur : pas de fondu inutile.
    if (profile == s_->wanted) return;
    const bool live = s_->wantedGeneration == s_->activeGeneration && State::sameParts(profile, s_->active);
    s_->wanted = profile;
    if (live) {
        // Seule l'isolation a changé ? L'habitacle se recalcule à part.
        if (!(profile.car == s_->active.car)) {
            s_->cabinCar = profile.car;
            s_->cabinPending = true;
        }
        s_->active = profile;
        s_->applyDimensions(profile);
    } else {
        ++s_->wantedGeneration;
    }
}

bool EngineSound::isSilent() const {
    if (volume_.load(std::memory_order_relaxed) == 0.0f && s_->outGain < kSilenceGain) return true;
    return s_->gain < kSilenceGain && targetRpm() < kMinRpm;
}

void EngineSound::render(float* out, int count) {
    if (s_->stereo) {
        s_->stereo = false;
        s_->field.setStereo(false);
    }
    float unused = 0.0f;
    for (int i = 0; i < count; ++i) {
        s_->nextFrame(targetRpm_.load(std::memory_order_relaxed), volume_.load(std::memory_order_relaxed), out[i], unused);
    }
}

void EngineSound::renderStereo(float* left, float* right, int count) {
    if (!s_->stereo) {
        s_->stereo = true;
        s_->field.setStereo(true);
    }
    for (int i = 0; i < count; ++i) {
        s_->nextFrame(targetRpm_.load(std::memory_order_relaxed), volume_.load(std::memory_order_relaxed), left[i], right[i]);
    }
}

void EngineSound::setDrive(float load, float boost, bool cut, bool cranking, float throttle) {
    s_->driven = true;
    s_->driveThrottle = throttle;
    s_->driveLoad = load;
    s_->driveBoost = boost;
    s_->driveCut = cut;
    s_->driveCrank = cranking;
}

void EngineSound::setTurbo(const TurboDrive& drive) {
    s_->turboDriven = true;
    s_->turboIn = drive;
}

void EngineSound::setStarter(bool powered, bool meshed, float load) {
    s_->starter.setState(powered, meshed, load);
}

void EngineSound::setRoad(float speedMs, float slipMs) {
    s_->roadSpeed = speedMs;
    s_->roadSlip = slipMs;
}

void EngineSound::setGearbox(const GearboxSpec& spec, const GearboxDrive& drive) {
    if (!s_->gearboxKnown || !(spec == s_->gearboxSpec)) {
        s_->gearboxSpec = spec;
        s_->gearboxKnown = true;
        s_->gearbox.setup(spec);
    }
    s_->gearbox.setDrive(drive);
}

void EngineSound::setListener(Listener listener) {
    s_->listener = listener;
    s_->presetPosition = true;
    s_->applyListenerPreset();
}

void EngineSound::setListenerPosition(float x, float y, float z, float maxSpeed) {
    s_->presetPosition = false;
    s_->field.setTarget({x, y, z}, std::numeric_limits<float>::quiet_NaN(), maxSpeed);
}

float EngineSound::listenerX() const { return s_->field.position().x; }

float EngineSound::gearboxLoose(int index) const { return s_->gearbox.looseShare(index); }
float EngineSound::gearboxStrike() const { return s_->gearbox.strike(); }
float EngineSound::listenerY() const { return s_->field.position().y; }

float EngineSound::listenerZ() const { return s_->field.position().z; }

float EngineSound::levelDbA() const { return s_->meter.db(); }

float EngineSound::limiterGain() const { return s_->limiterGain; }

std::uint32_t EngineSound::pops() const { return s_->popCount; }

float EngineSound::turbine() const { return s_->turbine; }

float EngineSound::turboRelease() const { return s_->releaseEnv; }

float EngineSound::load() const { return s_->load; }

float EngineSound::boost() const { return s_->boost; }

}  // namespace revforge
