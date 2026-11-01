#include "gearbox_sound.hpp"

#include <algorithm>
#include <cmath>

namespace revforge {

namespace {

// Passe des newtons sur les roulements au niveau du bus d'événements.
constexpr float kForceToBus = 1.1e-4f;

// Les défauts seuls, sans déformation : ce qui reste quand la denture est très peu chargée.
constexpr float kHelicalError = 100.0f;
constexpr float kSpurError = 260.0f;
constexpr float kReverseError = 950.0f;
constexpr float kRoadFinish = 0.6f;
// Une paire qui tourne à vide ne siffle pas : les dents ne s'appuient plus (part du couple admissible).
constexpr float kContactShare = 0.012f;
// Plus vite, plus fort : I ∝ n² (Rust, Brandl et Thien 1992), soit 6 dB par octave.
constexpr float kReferenceToothHz = 1000.0f;
// Les défauts propres à chaque dent : un lobe par tour (faux-rond), puis dent par dent.
constexpr float kPinionLobe = 0.34f;
constexpr float kWheelLobe = 0.12f;
constexpr float kPinionToothSpread = 0.2f;
constexpr float kWheelToothSpread = 0.1f;
// Erreur de pas, en fraction de dent.
constexpr float kPitchSpread = 0.012f;
// Une jupe autour de chaque raie, et un balancement : mesurés sur de vraies marches arrière. Une raie n'y est jamais
// stable : de 3 à 4,5 dB d'une trame de 46 ms à l'autre, et elle disparaît parfois une seconde entière.
constexpr float kRoughDepth = 0.32f;
constexpr float kWanderDepth = 2.0f;
constexpr float kWanderSlowDepth = 5.0f;
// La vitesse de la voiture ne tient jamais à mieux que quelques centimètres par seconde : pied du conducteur, route.
constexpr float kDriftMs = 0.05f;
// Le bruit de contact, large bande : une part de la force des défauts de denture.
constexpr float kContactNoise = 0.6f;
constexpr float kBearing = 7.0f;
constexpr float kClutchSlip = 16.0f;
constexpr float kReleaseBearing = 2.2f;

// Le claquement. Restitution d'un choc dans l'huile : Rust les trouve élastiques.
constexpr float kRestitution = 0.62f;
constexpr float kContactSeconds = 9e-5f;
constexpr float kRattleGain = 1.2f;
constexpr float kRestSpeed = 0.05f;    // rad/s : en dessous, une roue posée sur son flanc ne fait aucun choc
constexpr float kRingShare = 0.12f;

constexpr float kBangGain = 165.0f;
constexpr float kClickForce = 140.0f;
constexpr float kGrindForce = 3800.0f;
constexpr float kConeForce = 45.0f;

// Résonances du carter : relevées sur un grattement et des marches arrière réels, bandes vers 700 Hz, 2,1 kHz, 4 kHz, 6 kHz.
constexpr std::array<float, 6> kModeHz{720.0f, 1180.0f, 2150.0f, 2900.0f, 4100.0f, 6200.0f};
constexpr std::array<float, 6> kModeQ{6.0f, 7.0f, 5.0f, 8.0f, 5.0f, 4.0f};
constexpr std::array<float, 6> kModeWeight{0.7f, 0.55f, 1.0f, 0.5f, 0.75f, 0.55f};
constexpr float kHousingModeGain = 0.8f;
// Une fine structure de 40 à 140 Hz de large : mesurée à 2 à 5 dB sur le bruit de structure de vrais moteurs.
constexpr std::array<float, 3> kEchoSeconds{0.0073f, 0.0137f, 0.0237f};
constexpr std::array<float, 3> kEchoShare{0.42f, -0.3f, 0.2f};

float sinc(float x) {
    if (km::abs(x) < 1e-4f) return 1.0f;
    const double a = km::kPi * static_cast<double>(x);
    return static_cast<float>(std::sin(a) / a);
}

float wrapTurns(float turns) { return turns - km::floor(turns); }

}  // namespace

GearboxSound::GearboxSound(float sampleRate) : sr_(sampleRate), dt_(1.0f / sampleRate), sine_(dsp::sineTable()) {
    noise_.state = 0x6B1A73u;
    const float controlSeconds = static_cast<float>(kControlSamples) / sr_;
    smoothFast_ = 1.0f - km::exp(-controlSeconds / 0.002f);
    smoothSlow_ = 1.0f - km::exp(-controlSeconds / 0.015f);
    // Mis à jour tous les kControlSamples.
    const float controlRate = sr_ / static_cast<float>(kControlSamples);
    roughCoef_ = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 170.0f / controlRate);
    wanderCoef_ = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 6.0f / controlRate);
    wanderSlowCoef_ = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 1.0f / controlRate);
    driftCoef_ = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * 3.0f / controlRate);
    strikeDecay_ = km::exp(-1.0f / (kContactSeconds * sr_));
    bangDecay_ = km::exp(-1.0f / (0.0028f * sr_));
    clickDecay_ = km::exp(-1.0f / (0.0009f * sr_));
    grindDecay_ = km::exp(-1.0f / (0.0007f * sr_));
    broadBand_.set(km::min(2800.0f, sr_ * 0.4f), 0.5f, sr_);
    slipBand_.set(km::min(1500.0f, sr_ * 0.4f), 0.6f, sr_);
    releaseBand_.set(km::min(3100.0f, sr_ * 0.4f), 1.2f, sr_);
    coneBand_.set(km::min(2500.0f, sr_ * 0.4f), 0.8f, sr_);
    ringBand_.set(km::min(4800.0f, sr_ * 0.4f), 4.0f, sr_);
    // Les défauts de denture : un tirage fixe, une fois pour toutes. Une dent garde les siens.
    dsp::Noise draw;
    draw.state = 0x1D872B41u;
    for (float& error : errors_) error = draw.next();
}

float GearboxSound::fastSin(float turns) const {
    const float position = turns * static_cast<float>(dsp::kSineSize);
    const int whole = static_cast<int>(position);
    const float t = position - static_cast<float>(whole);
    const int index = whole & (dsp::kSineSize - 1);
    return sine_[static_cast<std::size_t>(index)] + (sine_[static_cast<std::size_t>(index + 1)] - sine_[static_cast<std::size_t>(index)]) * t;
}

float GearboxSound::toothError(int tooth, int salt) const {
    const unsigned at = static_cast<unsigned>(tooth) * 37u + static_cast<unsigned>(salt) * 101u;
    return errors_[at & 255u];
}

/**
 * Le carter : mêmes pièces pour toute la boîte, du grave au métal. Un tirage fixe, aucun enregistrement. Une grosse
 * boîte résonne plus bas : les modes suivent sa taille.
 */
void GearboxSound::buildHousing(const GearboxSpec& spec) {
    dsp::Noise draw;
    draw.state = 0x3AC91Du + static_cast<std::uint32_t>(spec.centerMm * 16.0);
    const float scale = km::sqrt(70.0f / km::max(static_cast<float>(spec.centerMm), 30.0f));
    for (std::size_t mode = 0; mode < modes_.size(); ++mode) {
        const float hz = km::min(kModeHz[mode] * scale * (1.0f + 0.05f * draw.next()), sr_ * 0.42f);
        modes_[mode] = dsp::Svf{};
        modes_[mode].set(hz, kModeQ[mode], sr_);
        // La sortie passe-bande d'un filtre d'état vaut Q à la résonance : on la ramène au poids voulu.
        modeGain_[mode] = kHousingModeGain * kModeWeight[mode] * (1.0f + 0.2f * draw.next()) * modes_[mode].k;
    }
    diffuseLengthA_ = std::clamp(static_cast<int>(0.0031f * sr_), 1, static_cast<int>(diffuseA_.size()));
    diffuseLengthB_ = std::clamp(static_cast<int>(0.0053f * sr_), 1, static_cast<int>(diffuseB_.size()));
    diffuseGain_ = {0.55f, -0.5f};
    diffuseA_.fill(0.0f);
    diffuseB_.fill(0.0f);
    diffuseAtA_ = 0;
    diffuseAtB_ = 0;
    echo_.fill(0.0f);
    echoAt_ = 0;
    for (std::size_t i = 0; i < echoDelay_.size(); ++i) {
        echoDelay_[i] = std::min(static_cast<int>(kEchoSeconds[i] * sr_ * (1.0f + 0.1f * draw.next())), kEchoSamples - 1);
        echoGain_[i] = kEchoShare[i];
    }
}

/** La force sur les roulements, vue du dehors. */
float GearboxSound::housing(float force) {
    // Deux diffuseurs : un choc ne ressort pas net, et les raies n'y changent pas de niveau.
    const float delayedA = diffuseA_[static_cast<std::size_t>(diffuseAtA_)];
    const float inputA = force + delayedA * diffuseGain_[0];
    diffuseA_[static_cast<std::size_t>(diffuseAtA_)] = inputA;
    diffuseAtA_ = diffuseAtA_ + 1 == diffuseLengthA_ ? 0 : diffuseAtA_ + 1;
    const float spreadA = delayedA - inputA * diffuseGain_[0];
    const float delayedB = diffuseB_[static_cast<std::size_t>(diffuseAtB_)];
    const float inputB = spreadA + delayedB * diffuseGain_[1];
    diffuseB_[static_cast<std::size_t>(diffuseAtB_)] = inputB;
    diffuseAtB_ = diffuseAtB_ + 1 == diffuseLengthB_ ? 0 : diffuseAtB_ + 1;
    const float spread = delayedB - inputB * diffuseGain_[1];

    float out = spread * housingDirect_;
    for (std::size_t mode = 0; mode < modes_.size(); ++mode) {
        modes_[mode].process(spread);
        out += modes_[mode].bp * modeGain_[mode];
    }
    // Les échos du plancher, sans rebouclage : ni queue, ni instabilité.
    echo_[static_cast<std::size_t>(echoAt_)] = out;
    float heard = out;
    for (std::size_t i = 0; i < echoDelay_.size(); ++i) {
        const int at = echoAt_ - echoDelay_[i];
        heard += echo_[static_cast<std::size_t>(at < 0 ? at + kEchoSamples : at)] * echoGain_[i];
    }
    echoAt_ = echoAt_ + 1 == kEchoSamples ? 0 : echoAt_ + 1;
    return heard;
}

/**
 * Les harmoniques d'une paire. Velex (2012) : la longueur de contact varie comme 2 Sinc(k εα) Sinc(k εβ), ce qui
 * s'annule quand un rapport de conduite est entier. Mais de vraies dents ont des défauts : un reste, décroissant,
 * comble les zéros. Sa pente est mesurée sur de vraies marches arrière : environ -3 dB par harmonique.
 */
GearboxSound::Voice GearboxSound::makeVoice(const GearPair& pair, double moduleMm, double helixDeg, double limitNm, float errorForce, float designShare, float weight, bool spur) {
    Voice v;
    v.driver = pair.driver;
    v.driven = pair.driven;
    v.weight = weight;
    v.errorForce = errorForce;
    const double helix = helixDeg * km::kPi / 180.0;
    const double transverse = moduleMm / std::cos(helix);
    v.pitchRadius = static_cast<float>(pair.driver * transverse * 0.5e-3);
    v.designForce = designShare * static_cast<float>(limitNm) / km::max(v.pitchRadius, 0.005f);
    // Passé le deuxième harmonique, une denture douce ne fait presque plus rien : autant ne pas les calculer.
    v.harmonics = spur ? kHarmonics : 3;
    dsp::Noise draw;
    draw.state = static_cast<std::uint32_t>(pair.driver) * 257u + static_cast<std::uint32_t>(pair.driven) * 65537u + 11u;
    // Le flanc de retenue est moins soigné : ses corrections de profil sont faites pour l'accélération.
    const float coastLift = spur ? 1.1f : 1.45f;
    for (int h = 0; h < kHarmonics; ++h) {
        const float k = static_cast<float>(h + 1);
        const float sinc2 = 2.0f * km::abs(sinc(k * pair.profile) * sinc(k * pair.overlap));
        const float rest = (spur ? 0.17f : 0.022f) / km::pow(k, 0.55f);
        const std::size_t i = static_cast<std::size_t>(h);
        v.drive[i] = km::sqrt(sinc2 * sinc2 + rest * rest);
        v.coast[i] = v.drive[i] * coastLift * (1.0f + 0.25f * draw.next());
        v.phase[i] = draw.next() * 0.5f + 0.5f;
        v.phaseCoast[i] = draw.next() * 0.5f + 0.5f;
    }
    return v;
}

void GearboxSound::setup(const GearboxSpec& spec) {
    spec_ = spec;
    ready_ = true;
    buildHousing(spec);
    const bool spur = spec.form == ToothForm::Spur;
    const float error = spur ? kSpurError : kHelicalError;
    // Les corrections de profil visent un couple moyen sur la route ; une boîte de course travaille près de son maximum.
    const float design = spur ? 0.5f : 0.35f;
    const float limit = static_cast<float>(spec.limitNm);
    for (int i = 0; i < spec.gears; ++i) {
        const GearPair& pair = spec.pairs[static_cast<std::size_t>(i)];
        // Une boîte de route est rectifiée et corrigée pour le silence : à rapport de conduite égal, elle fait encore moins de bruit.
        if (pair.driver > 1) pairVoice_[static_cast<std::size_t>(i)] = makeVoice(pair, spec.moduleMm, spec.helixDeg, limit, error, design, spur ? 1.0f : kRoadFinish, spur);
    }
    if (spec.inputPair.driver > 1) {
        inputVoice_ = makeVoice(spec.inputPair, spec.moduleMm, spec.helixDeg, limit, error, design, spur ? 0.85f : 0.85f * kRoadFinish, spur);
        inputVoice_.harmonics = spur ? 4 : 2;
    }
    if (spec.reverseIn.driver > 1) {
        // La marche arrière : en denture droite, sans correction, même sur une voiture de route. Les défauts dominent.
        reverseInVoice_ = makeVoice(spec.reverseIn, spec.moduleMm, 0.0, limit, kReverseError, 0.0f, 1.0f, true);
        reverseOutVoice_ = makeVoice(spec.reverseOut, spec.moduleMm, 0.0, limit, kReverseError, 0.0f, 0.8f, true);
    }
    if (spec.finalPair.driver > 1) {
        const double finalHelix = spec.hypoid ? 35.0 : spec.helixDeg;
        finalVoice_ = makeVoice(spec.finalPair, spec.moduleMm * 1.1, finalHelix, limit * 1.5f, kHelicalError, 0.35f, spec.hypoid ? 0.5f : 0.6f, false);
        finalVoice_.harmonics = 2;
    }
    activeGear_ = 99;
}

void GearboxSound::setDrive(const GearboxDrive& drive) {
    target_ = drive;
    fed_ = true;
}

/** Les paires chargées selon le rapport en prise : tout le reste tourne à vide, donc se tait. */
void GearboxSound::chooseStages() {
    stages_ = 0;
    const auto add = [this](const Voice* voice, float factor, bool fromOutput, float fm) {
        Stage& s = stage_[static_cast<std::size_t>(stages_)];
        if (s.voice != voice) {
            s.voice = voice;
            s.lastTooth = -1;
            s.forceNow = 0.0f;
        }
        s.speedFactor = factor;
        s.fromOutput = fromOutput;
        s.fm = fm;
        ++stages_;
    };
    const int gear = target_.gear;
    if (spec_.kind == GearboxKind::Reducer) {
        add(&pairVoice_[0], 1.0f, false, 0.0f);
        add(&finalVoice_, static_cast<float>(1.0 / spec_.pairs[0].ratio()), false, 0.0f);
        return;
    }
    if (gear < 0) {
        const float input = spec_.threeShafts ? static_cast<float>(1.0 / spec_.inputPair.ratio()) : 1.0f;
        if (spec_.threeShafts) add(&inputVoice_, 1.0f, false, 1.0f);
        add(&reverseInVoice_, input, false, 1.0f);
        add(&reverseOutVoice_, input * static_cast<float>(1.0 / spec_.reverseIn.ratio()), false, 1.0f);
        return;
    }
    if (gear == 0) {
        // Point mort : le pont tourne avec les roues, sans rien transmettre.
        return;
    }
    const std::size_t index = static_cast<std::size_t>(gear - 1);
    const bool direct = gear == spec_.directGear;
    if (spec_.threeShafts && !direct) {
        add(&inputVoice_, 1.0f, false, 1.0f);
        add(&pairVoice_[index], static_cast<float>(1.0 / spec_.inputPair.ratio()), false, 1.0f);
    } else if (!direct) {
        add(&pairVoice_[index], 1.0f, false, 1.0f);
    }
    // Le pont suit les roues : l'acyclisme lui parvient à peine, les demi-arbres le filtrent.
    add(&finalVoice_, 1.0f, true, 0.2f);
}

void GearboxSound::control() {
    if (target_.gear != activeGear_) {
        activeGear_ = target_.gear;
        chooseStages();
    }
    // Tout arrive par blocs : on y va en douceur, et les vitesses avancent à chaque échantillon, en ligne droite.
    inputStep_ = (target_.inputOmega - inputOmega_) * smoothFast_ / static_cast<float>(kControlSamples);
    outputStep_ = (target_.outputOmega - outputOmega_) * smoothFast_ / static_cast<float>(kControlSamples);
    torque_ += (target_.torqueNm - torque_) * smoothFast_;
    acyclism_ += (target_.acyclism - acyclism_) * smoothSlow_;
    const float limit = km::max(static_cast<float>(spec_.limitNm), 50.0f);
    const float torqueAbs = km::abs(torque_);
    for (std::size_t k = 0; k < wander_.size(); ++k) {
        wander_[k] += (noise_.next() * 2.0f - wander_[k]) * wanderCoef_;
        wanderSlow_[k] += (noise_.next() * 2.0f - wanderSlow_[k]) * wanderSlowCoef_;
    }
    drift_ += (noise_.next() * 2.0f - drift_) * driftCoef_;
    // La jupe de chaque raie : ce bruit n'a pas besoin d'être tiré à chaque échantillon, il reste sous 170 Hz.
    roughTarget_ += (noise_.next() * 2.5f - roughTarget_) * roughCoef_;
    roughStep_ = (roughTarget_ - rough_) / static_cast<float>(kControlSamples);
    // En part de la vitesse : énorme à l'arrêt, négligeable sur autoroute.
    const float wheelOmega = km::abs(outputOmega_) / km::max(static_cast<float>(spec_.finalDrive), 1.0f);
    driftNow_ = drift_ * 4.0f * kDriftMs / km::max(wheelOmega * 0.3f, 1.5f);
    // Un flanc ou l'autre : le couple change de sens en traversant le jeu.
    const float flankWanted = torque_ < 0.0f ? 1.0f : 0.0f;
    flank_ += (flankWanted - flank_) * 0.15f;
    // Sous ce couple, les dents ne s'appuient plus : l'erreur de transmission ne pousse plus rien.
    const float bearing = km::min(torqueAbs / (kContactShare * limit), 1.0f);
    const float nyquist = sr_ * 0.46f;
    for (int i = 0; i < stages_; ++i) {
        Stage& s = stage_[static_cast<std::size_t>(i)];
        const Voice& v = *s.voice;
        const float shaft = (s.fromOutput ? outputOmega_ : inputOmega_) * s.speedFactor;
        const float toothHz = km::abs(shaft) / static_cast<float>(2.0 * km::kPi) * static_cast<float>(v.driver);
        // La force moyenne sur la dent, et ce que la déformation y ajoute une fois retranchées les corrections de profil.
        // Un arbre plus lent porte d'autant plus de couple.
        const float through = s.fromOutput ? static_cast<float>(spec_.boxRatio[static_cast<std::size_t>(std::clamp(activeGear_ - 1, 0, spec_.gears - 1))]) : 1.0f / km::max(s.speedFactor, 0.05f);
        const float mean = torqueAbs * through / km::max(v.pitchRadius, 0.005f);
        const float deform = km::abs(mean - v.designForce * (s.fromOutput ? 1.0f : through));
        const float speed = km::min(toothHz / kReferenceToothHz, 6.0f);
        int count = 0;
        for (int h = 0; h < v.harmonics; ++h) {
            const std::size_t k = static_cast<std::size_t>(h);
            const float a = v.drive[k] + (v.coast[k] - v.drive[k]) * flank_;
            // Le reste, fait des seuls défauts, suit la même pente.
            const float shape = a / km::max(v.drive[0], 1e-3f);
            const float rest = v.errorForce * shape;
            // Pas beaucoup plus que ce qui appuie les dents l'une contre l'autre : au-delà, elles se quittent, et c'est le
            // claquement. Il reste ce que les défauts donnent à l'inertie des roues.
            const float force = km::min(km::sqrt(a * deform * a * deform + rest * rest), (1.2f * mean + 0.5f * v.errorForce) * shape);
            const float hz = toothHz * static_cast<float>(h + 1);
            // Rien au-dessus de la limite du filtre : une raie s'efface avant de se replier.
            const float fade = km::coerceIn((nyquist - hz) / (0.1f * nyquist), 0.0f, 1.0f);
            const std::size_t w = (k + static_cast<std::size_t>(i) * 2u) % wander_.size();
            const float swing = km::max(1.0f + kWanderDepth * wander_[w] + kWanderSlowDepth * wanderSlow_[w], 0.12f);
            s.gain[k] = force * fade * swing * swing;
            if (fade > 0.0f) count = h + 1;
            s.offset[k] = v.phase[k] + (v.phaseCoast[k] - v.phase[k]) * flank_;
        }
        s.harmonics = count;
        s.forceStep = (bearing * speed * v.weight - s.forceNow) / static_cast<float>(kControlSamples);
    }
    // Le souffle de l'huile et des roulements : à la vitesse des arbres, en partie même sans charge.
    const float rolling = km::abs(inputOmega_) * 0.002f + km::abs(outputOmega_) * 0.0015f;
    bearingLevel_ = kBearing * rolling * (spec_.form == ToothForm::Spur ? 1.25f : 1.0f);
    float contact = 0.0f;
    for (int i = 0; i < stages_; ++i) contact += stage_[static_cast<std::size_t>(i)].forceNow * stage_[static_cast<std::size_t>(i)].voice->errorForce;
    contactLevel_ = kContactNoise * contact;
    slipLevel_ = kClutchSlip * km::sqrt(km::max(target_.clutchSlip, 0.0f));
    // La butée d'embrayage ne tourne chargée que pédale enfoncée.
    releaseLevel_ = kReleaseBearing * target_.pedal * (target_.running ? 1.0f : 0.0f);
    coneLevel_ = kConeForce * km::sqrt(km::max(target_.coneSlip, 0.0f) * km::max(target_.coneTorque, 0.0f) * 0.001f);
    grindLevel_ = target_.grind;
}

/** Une dent arrive : ses défauts sont fixes, et le prochain tour les retrouvera. */
void GearboxSound::toothDefects(Stage& s, int tooth) {
    const Voice& v = *s.voice;
    const int driverTooth = tooth % v.driver;
    const int drivenTooth = tooth % v.driven;
    const float lobeA = fastSin(static_cast<float>(driverTooth) / static_cast<float>(v.driver) + 0.13f);
    const float lobeB = fastSin(static_cast<float>(drivenTooth) / static_cast<float>(v.driven) + 0.61f);
    s.ampFrom = s.ampTo;
    s.ampTo = 1.0f + kPinionLobe * lobeA + kWheelLobe * lobeB + kPinionToothSpread * toothError(driverTooth, v.driver) + kWheelToothSpread * toothError(drivenTooth, v.driven + 53);
    s.pitchFrom = s.pitchTo;
    s.pitchTo = kPitchSpread * (toothError(driverTooth, v.driver + 7) + toothError(drivenTooth, v.driven + 19));
}

float GearboxSound::whine() {
    float total = 0.0f;
    const float wiggle = 1.0f + kRoughDepth * rough_;
    for (int i = 0; i < stages_; ++i) {
        Stage& s = stage_[static_cast<std::size_t>(i)];
        const Voice& v = *s.voice;
        s.forceNow += s.forceStep;
        const float base = (s.fromOutput ? outputOmega_ : inputOmega_) * s.speedFactor;
        const float omega = base * (1.0f + fm_ * s.fm + driftNow_);
        s.teeth += static_cast<double>(omega) * static_cast<double>(v.driver) / (2.0 * km::kPi) * static_cast<double>(dt_);
        // Le motif complet revient tous les pignon × roue dents.
        const double cycle = static_cast<double>(v.driver) * static_cast<double>(v.driven);
        if (s.teeth >= cycle) s.teeth -= cycle;
        if (s.forceNow < 1e-4f) continue;
        const int tooth = static_cast<int>(s.teeth);
        if (tooth != s.lastTooth) {
            s.lastTooth = tooth;
            toothDefects(s, tooth);
        }
        const float within = static_cast<float>(s.teeth - static_cast<double>(tooth));
        const float amp = s.ampFrom + (s.ampTo - s.ampFrom) * within;
        const float pitch = s.pitchFrom + (s.pitchTo - s.pitchFrom) * within;
        const float at = within + pitch + 1.0f;
        float sum = 0.0f;
        for (int h = 0; h < s.harmonics; ++h) {
            const std::size_t k = static_cast<std::size_t>(h);
            sum += s.gain[k] * fastSin(at * static_cast<float>(h + 1) + s.offset[k]);
        }
        total += sum * amp * wiggle * s.forceNow;
    }
    return total;
}

float GearboxSound::looseShare(int index) const {
    if (index < 0 || index >= kLooseGears) return 0.0f;
    const auto i = static_cast<std::size_t>(index);
    const float gap = spec_.loose[i].gap;
    return gap > 0.0f ? looseAngle_[i] / gap : 0.0f;
}

/**
 * Les roues folles, dans leur jeu. Rust : elles partent quand J·α dépasse la traînée. Ni rythme ni chocs imposés :
 * ils sortent de cette équation, et le choc vaut ce que vaut la vitesse acquise dans le jeu.
 */
float GearboxSound::rattle(float phase) {
    const float count = static_cast<float>(km::max(static_cast<float>(target_.firings), 1.0f));
    const float fire = phase * count;
    const int turn = static_cast<int>(fire);
    // Aucune explosion n'est identique à la précédente.
    if (turn != firedTurn_) {
        firedTurn_ = turn;
        fireGain_ = 1.0f + 0.3f * noise_.next();
    }
    const float within = fire - static_cast<float>(turn);
    accelNow_ = acyclism_ * fireGain_ * (fastSin(within) + 0.35f * fastSin(within * 2.0f + 0.11f));
    // La vitesse de l'arbre primaire suit : c'est ce qui fait onduler les raies, à chaque explosion.
    const float meanOmega = km::max(km::abs(inputOmega_), 30.0f);
    fm_ += (accelNow_ / meanOmega) * dt_;
    fm_ *= 0.997f;
    float hits = 0.0f;
    if (acyclism_ > 1.0f) {
        for (std::size_t i = 0; i < kLooseGears; ++i) {
            const LooseGear& gear = spec_.loose[i];
            if (gear.inertia <= 0.0f) continue;
            float& angle = looseAngle_[i];
            float& speed = looseSpeed_[i];
            // Dans le repère du pignon qui la mène : quand il freine, la roue continue sur son élan et traverse le jeu.
            const float push = -accelNow_ * gear.speedRatio - gear.drag / gear.inertia;
            speed += push * dt_;
            angle += speed * dt_;
            if (angle > gear.gap) {
                hits += gear.inertia * (1.0f + kRestitution) * km::abs(speed);
                angle = gear.gap;
                speed *= -kRestitution;
            } else if (angle < -gear.gap) {
                // Sur le flanc menant, la traînée la retient : à très faible vitesse, elle y reste posée, et ce n'est pas un choc.
                angle = -gear.gap;
                if (speed > -kRestSpeed) {
                    speed = 0.0f;
                } else {
                    hits += gear.inertia * (1.0f + kRestitution) * km::abs(speed);
                    speed *= -kRestitution;
                }
            }
        }
    } else if (rattling_ > 0.0f) {
        // Plus d'à-coups : les roues se reposent sur leur flanc, dans le même état pour la prochaine fois.
        looseAngle_.fill(0.0f);
        looseSpeed_.fill(0.0f);
    }
    rattling_ = acyclism_;
    if (hits > 0.0f) {
        // L'impulsion rapportée à la durée du contact : une force, au primitif (rayon de l'ordre de 40 mm).
        strike_ += hits / 0.04f / kContactSeconds * kRattleGain;
    }
    if (strike_ < 1e-3f) {
        strike_ = 0.0f;
        return 0.0f;
    }
    const float shaped = strike_;
    strike_ *= strikeDecay_;
    // Le pignon d'acier sonne un instant à sa propre fréquence, puis le carter prend le relais.
    ringBand_.process(shaped);
    return shaped * (1.0f - kRingShare) + ringBand_.bp * kRingShare * 3.0f;
}

/** Ce qui ne dure pas : le baladeur, le cône, les chocs. */
float GearboxSound::transients() {
    float out = 0.0f;
    // Les chocs d'engagement et de crabots : un pic bref, d'autant plus violent que l'énergie est grande.
    const double impacts = target_.impacts;
    if (impacts < lastImpacts_) lastImpacts_ = impacts;
    if (impacts > lastImpacts_) {
        const float energy = static_cast<float>(impacts - lastImpacts_);
        bang_ += kBangGain * km::pow(energy, 0.45f);
        lastImpacts_ = impacts;
    }
    const double clicks = target_.selectorClicks;
    if (clicks < lastClicks_) lastClicks_ = clicks;
    if (clicks > lastClicks_) {
        click_ += kClickForce * (0.8f + 0.4f * (noise_.next() * 0.5f + 0.5f));
        lastClicks_ = clicks;
    }
    if (bang_ > 1e-2f) {
        out += bang_ * (0.6f + 0.4f * noise_.next());
        bang_ *= bangDecay_;
    } else {
        bang_ = 0.0f;
    }
    if (click_ > 1e-2f) {
        out += click_ * noise_.next();
        click_ *= clickDecay_;
    } else {
        click_ = 0.0f;
    }
    // Le baladeur force sur les crabots : une dent frappe à chaque passage, à la fréquence de l'écart de vitesses.
    if (grindLevel_ > 0.5f) {
        const float rate = grindLevel_ / static_cast<float>(2.0 * km::kPi) * static_cast<float>(spec_.dogTeeth);
        grindPhase_ += rate * dt_;
        if (grindPhase_ >= grindNext_) {
            grindPhase_ = 0.0f;
            // Toutes les dents ne touchent pas : le chanfrein repousse le baladeur, qui revient plus tôt ou plus tard.
            const float draw = noise_.next() * 0.5f + 0.5f;
            grindNext_ = 0.6f + 0.8f * (noise_.next() * 0.5f + 0.5f);
            if (draw > 0.25f) grindHit_ += kGrindForce * km::sqrt(km::min(grindLevel_ / 100.0f, 2.0f)) * (0.35f + 0.65f * draw);
        }
    }
    if (grindHit_ > 1e-2f) {
        // Le levier rebondit vers 120 Hz (mesuré sur un vrai grattement) : la force bat à ce rythme.
        grindChatter_ = wrapTurns(grindChatter_ + 120.0f * dt_);
        const float chatter = 0.55f + 0.45f * fastSin(grindChatter_);
        out += grindHit_ * chatter * (noise_.next() * 0.5f + 0.5f);
        grindHit_ *= grindDecay_;
    } else {
        grindHit_ = 0.0f;
    }
    // Le cône qui frotte : sous l'huile, presque rien, mais il annonce le passage.
    if (coneLevel_ > 1e-2f) {
        coneBand_.process(noise_.next());
        out += coneBand_.bp * coneLevel_;
    }
    return out;
}

float GearboxSound::sample(float enginePhase) {
    if (!fed_ || !ready_) return 0.0f;
    if (controlLeft_ == 0) {
        control();
        controlLeft_ = kControlSamples;
    }
    --controlLeft_;

    inputOmega_ += inputStep_;
    outputOmega_ += outputStep_;

    // Tout est immobile et le dernier écho est passé : ni son, ni calcul, ni tirage.
    const bool moving = km::abs(inputOmega_) > 1.0f || km::abs(outputOmega_) > 1.0f;
    const bool quiet = !moving && acyclism_ < 1.0f && target_.grind <= 0.5f && bang_ == 0.0f && click_ == 0.0f && grindHit_ == 0.0f && strike_ == 0.0f && target_.impacts == lastImpacts_ && target_.selectorClicks == lastClicks_;
    if (quiet) {
        if (silentFor_ > kSilentAfter) return 0.0f;
    }

    rough_ += roughStep_;

    float force = whine();
    if (acyclism_ > 1.0f || strike_ > 0.0f) force += rattle(enginePhase);
    else fm_ *= 0.997f;
    force += transients();

    // Le souffle de l'huile, des roulements et du contact des dents : un seul bruit, large, que le carter colore.
    const float broad = contactLevel_ + bearingLevel_;
    if (broad > 1e-3f) {
        broadBand_.process(noise_.next());
        force += broadBand_.bp * broad;
    }
    if (slipLevel_ > 1e-3f) {
        // L'embrayage qui patine broute un peu : un à-coup lent, à la fréquence propre du disque.
        judderPhase_ = wrapTurns(judderPhase_ + 13.0f * dt_);
        slipBand_.process(noise_.next());
        force += slipBand_.bp * slipLevel_ * (0.75f + 0.25f * fastSin(judderPhase_));
    }
    if (releaseLevel_ > 1e-3f) {
        releaseBand_.process(noise_.next());
        force += releaseBand_.bp * releaseLevel_;
    }

    silentFor_ = force == 0.0f ? silentFor_ + 1 : 0;
    return housing(force * kForceToBus);
}

}  // namespace revforge
