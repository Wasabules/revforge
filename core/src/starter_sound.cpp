#include "starter_sound.hpp"

#include <algorithm>
#include <cmath>

namespace revforge {

namespace {

// Les valeurs sans mesure citée sortent d'un calage borné (CMA-ES) sur neuf vrais démarrages, vérifié sur cinq autres.
constexpr float kStarterLevel = 0.55f;

// Le bruit de contact des dents : plat de 300 Hz à 2 kHz, puis 12 dB plus bas dès 3 kHz (mesuré).
constexpr float kContactKneeHz = 2150.0f;
constexpr float kContactKneeQ = 0.8f;
constexpr float kContactSplitHz = 780.0f;
constexpr float kContactSplitQ = 0.7f;
constexpr float kContactLowShare = 0.8f;
constexpr float kRumbleLevel = 0.101f;
// Le couple transmis est rapporté au calage : à valeur égale, un gros démarreur force plus.
constexpr float kReferencePowerKw = 1.5f;
// Même en détente, le pignon traîne encore un peu sur la couronne, d'autant moins que la compression
// est forte et rejette le vilebrequin loin devant : c'est pourquoi un diesel se tait entre deux coups.
constexpr float kContactFloor = 0.187f;
constexpr float kFloorRatio = 10.5f;
// Denture droite : pas de poussée axiale qui garde le pignon appuyé, il se décharge plus franchement.
constexpr float kDirectFloor = 0.07f;
// Les balais : aigus, à la mesure du courant, et pas du couple.
constexpr float kBrushHz = 5770.0f;
constexpr float kBrushQ = 0.45f;
constexpr float kBrushLevel = 0.612f;
constexpr float kBrushRolloffHz = 6420.0f;
// Mesuré sur deux V8 : le coude à 2 kHz est bien moins marqué, 6 à 8 dB de plus au-dessus. Une denture droite claque sec.
constexpr float kDirectKnee = 1.35f;
constexpr float kDirectBrush = 1.35f;
// Les raies : mesurées rares, surtout l'engrènement sous 800 Hz. Au-dessus, ce sont des bosses de la structure.
constexpr float kMeshLevel = 0.11f;
constexpr float kDirectMesh = 1.6f;
constexpr float kMeshSecond = 0.308f;
constexpr float kMeshThird = 0.24f;
constexpr float kMotorLineLevel = 0.0106f;
constexpr float kSunTeeth = 12.0f;
constexpr float kPitchSwing = 0.55f;
// Chaque dent qui entre en prise relance le bruit : une cadence sans raie, qui glisse avec le régime.
// Le calage la réduit à presque rien, et les enregistrements en montrent peu.
constexpr float kToothGrit = 0.0282f;
// De combien chaque bande bouge d'elle-même, en dehors du couple.
constexpr float kWanderDepth = 1.34f;
constexpr float kWanderSlowDepth = 1.18f;
// Aucune compression n'est identique : gaz résiduels, film d'huile, tension de la batterie.
constexpr float kBurstSpread = 0.717f;
constexpr float kBurstGlideS = 0.0138f;
// Reprise de contact après une séparation.
constexpr float kRecontactLevel = 1.04f;
constexpr float kRecontactGain = 36.1f;
// L'engagement. Mesuré : 8 à 20 ms de montée, un sommet de 10 à 15 dB au-dessus du lancement (15 à 25 dB dans
// l'aigu), retombé en 30 à 60 ms, plusieurs pics.
constexpr float kImpactLevel = 0.85f;
constexpr float kImpactDecayS = 0.0165f;
constexpr float kImpactRiseS = 0.002f;
// Chaque choc claque d'abord, très bref, avant que le métal ne continue de vibrer.
constexpr float kClickLevel = 0.9f;
constexpr float kClickDecayS = 0.0018f;
constexpr float kShockDecayS = 0.0163f;
constexpr float kShockLevel = 0.2f;
constexpr float kRelayTick = 0.25f;
constexpr float kPlungerSeconds = 0.01f;
constexpr float kPlungerNoise = 0.35f;
constexpr float kPlungerStop = 1.2f;
constexpr float kMeshStrike = 1.0f;
constexpr float kReleaseStrike = 0.25f;
constexpr float kOverrunRattle = 0.25f;
// La structure : un trajet direct, puis une queue dense qui fixe la fine structure du spectre.
constexpr std::uint32_t kBodySeed = 0x7CD8C8u;
constexpr float kBodySeconds = 0.06f;
constexpr int kBodyBands = 8;
constexpr std::array<float, kBodyBands> kBodyHz{350.0f, 550.0f, 850.0f, 1300.0f, 2000.0f, 3100.0f, 4800.0f, 7500.0f};
constexpr std::array<float, kBodyBands> kBodyDecayS{0.0075f, 0.0075f, 0.0065f, 0.0055f, 0.005f, 0.0045f, 0.004f, 0.003f};
constexpr std::array<float, kBodyBands> kBodyGain{0.95f, 0.95f, 0.8f, 0.7f, 0.6f, 0.55f, 0.5f, 0.4f};
constexpr float kBodyTail = 1.42f;
constexpr float kBodyDecayScale = 1.18f;
// Les résonances larges du bloc : mesurées par LPC sur sept démarrages, déplacées selon le moteur. Trois prises
// d'un même Iveco gardent les mêmes bosses à 1 dB près : c'est bien la signature du moteur.
constexpr int kBodyModes = 13;
constexpr std::array<float, kBodyModes> kModeHz{650.0f, 1090.0f, 1430.0f, 1640.0f, 1900.0f, 2200.0f, 2450.0f, 2890.0f, 3100.0f, 3600.0f, 4200.0f, 4850.0f, 5700.0f};
constexpr std::array<float, kBodyModes> kModeQ{8.0f, 6.0f, 9.0f, 5.0f, 5.0f, 6.0f, 8.0f, 7.0f, 8.0f, 11.0f, 18.0f, 15.0f, 19.0f};
constexpr std::array<float, kBodyModes> kModeWeight{0.5f, 1.4f, 1.5f, 0.7f, 0.55f, 0.65f, 1.0f, 0.65f, 0.45f, 0.32f, 0.2f, 0.16f, 0.12f};
constexpr float kModeGain = 0.3f;
constexpr float kModeSpread = 0.09f;
constexpr float kModeWeightSpread = 0.5f;

}  // namespace

StarterSound::StarterSound(float sampleRate) : sr_(sampleRate) {
    forceCoef_ = 1.0f - km::exp(-1.0f / (0.0015f * sr_));
    pitchFastCoef_ = 1.0f - km::exp(-1.0f / (0.012f * sr_));
    pitchSlowCoef_ = 1.0f - km::exp(-1.0f / (0.2f * sr_));
    currentCoef_ = 1.0f - km::exp(-1.0f / (0.004f * sr_));
    wanderCoef_ = 1.0f - km::exp(-1.0f / (0.006f * sr_));
    wanderSlowCoef_ = 1.0f - km::exp(-1.0f / (0.03f * sr_));
    impactDecay_ = km::exp(-1.0f / (kImpactDecayS * sr_));
    shockDecay_ = km::exp(-1.0f / (kShockDecayS * sr_));
    burstCoef_ = 1.0f - km::exp(-1.0f / (kBurstGlideS * sr_));
    forceMeanCoef_ = 1.0f - km::exp(-1.0f / (0.1f * sr_));
    clickDecay_ = km::exp(-1.0f / (kClickDecayS * sr_));
    impactRiseCoef_ = 1.0f - km::exp(-1.0f / (kImpactRiseS * sr_));
    brushRolloffCoef_ = 1.0f - km::exp(-2.0f * static_cast<float>(km::kPi) * kBrushRolloffHz / sr_);
    body_.init(kBodyPartition, kBodyPartitions);
}

void StarterSound::setState(bool powered, bool meshed, float load, float supply) {
    powered_ = powered;
    meshed_ = meshed;
    load_ = load;
    supply_ = supply;
}

/**
 * La réponse du bloc et du carter, propre à chaque moteur et figée. Les enregistrements ont une fine
 * structure de 40 à 100 Hz de large, fixe dans le temps : 2 à 5 dB, plus marquée dans le grave.
 * Un trajet direct, puis une queue dense, en bruit à décroissance exponentielle : aucun échantillon
 * d'enregistrement, seulement un tirage déterministe à partir de la fiche du démarreur.
 */
void StarterSound::buildBody(float scale) {
    bodyLength_ = std::min(kBodyCapacity, static_cast<int>(kBodySeconds * sr_));
    std::fill(bodyImpulse_.begin(), bodyImpulse_.end(), 0.0f);
    dsp::Noise draw;
    // Deux moteurs de même cylindrée, l'un essence et l'autre diesel, ne partagent pas leur bloc.
    draw.state = kBodySeed + static_cast<std::uint32_t>(spec_.ringTeeth) * 257u + static_cast<std::uint32_t>(spec_.commutatorBars) * 65537u + static_cast<std::uint32_t>(spec_.compressionRatio * 10.0f) * 16411u;
    for (int band = 0; band < kBodyBands; ++band) {
        const float hz = std::min(kBodyHz[static_cast<std::size_t>(band)] * scale, sr_ * 0.4f);
        dsp::Svf filter;
        filter.set(hz, 1.2f, sr_);
        const float samples = kBodyDecayS[static_cast<std::size_t>(band)] * kBodyDecayScale * sr_;
        const float decay = km::exp(-1.0f / samples);
        const int reach = std::min(bodyLength_, static_cast<int>(samples * 7.0f));
        float envelope = 1.0f;
        float energy = 0.0f;
        for (int n = 0; n < reach; ++n) {
            filter.process(draw.next());
            const float x = filter.bp * envelope;
            bodyScratch_[static_cast<std::size_t>(n)] = x;
            energy += x * x;
            envelope *= decay;
        }
        // Chaque bande rapportée au trajet direct, qui lui, passe partout.
        const float bandwidth = hz / 1.2f;
        const float gain = kBodyTail * kBodyGain[static_cast<std::size_t>(band)] * km::sqrt(bandwidth * 2.0f / sr_ / km::max(energy, 1e-9f));
        for (int n = 0; n < reach; ++n) bodyImpulse_[static_cast<std::size_t>(n)] += bodyScratch_[static_cast<std::size_t>(n)] * gain;
    }
    // Les résonances larges, en sinusoïdes amorties : leur gain à la résonance vaut sensiblement `peak`.
    for (std::size_t mode = 0; mode < kBodyModes; ++mode) {
        const float hz = std::min(kModeHz[mode] * scale * (1.0f + kModeSpread * draw.next()), sr_ * 0.4f);
        const float step = hz / sr_;
        const float length = kModeQ[mode] / (static_cast<float>(km::kPi) * hz) * sr_;
        const float decay = km::exp(-1.0f / length);
        const float peak = kModeGain * kModeWeight[mode] * (1.0f + kModeWeightSpread * draw.next());
        float envelope = 2.0f * peak / length;
        // Au-delà de sept constantes de temps, il reste moins de -60 dB : `setup` tourne dans le fil du son.
        const int reach = std::min(bodyLength_, static_cast<int>(length * 7.0f));
        for (int n = 0; n < reach; ++n) {
            bodyImpulse_[static_cast<std::size_t>(n)] += envelope * dsp::sinTurns(step * static_cast<float>(n));
            envelope *= decay;
        }
    }
    bodyImpulse_[0] += 1.0f;
    body_.setImpulse(bodyImpulse_.data(), bodyLength_);
}

/** Caractéristiques du démarreur, et défauts propres à chaque dent : ils reviennent à chaque tour. */
void StarterSound::setup(const StarterSpec& spec, float sizeScale) {
    spec_ = spec;
    // Un autre moteur : rien de la séquence en cours ne le suit.
    wasPowered_ = false;
    wasMeshed_ = false;
    sinceContact_ = -1;
    impact_ = impactHeld_ = click_ = shock_ = 0.0f;
    silentFor_ = 0;
    if (spec.powerKw <= 0.0f) return;
    const double reduction = static_cast<double>(spec.reduction);
    pinionFreeOmega_ = static_cast<double>(spec.motorFreeRpm) * 2.0 * km::kPi / 60.0 / reduction;
    pinionStall_ = 4.0 * static_cast<double>(spec.powerKw) * 1000.0 / pinionFreeOmega_;
    pinionInertia_ = static_cast<double>(spec.motorInertia) * reduction * reduction;
    // Une denture inclinée a plusieurs dents en prise à la fois : le contact passe en douceur.
    const float helix = spec.helixDeg * static_cast<float>(km::kPi) / 180.0f;
    const float contactRatio = 1.5f + 2.2f * helix;
    toothRough_ = 1.0f / contactRatio;
    meshLevel_ = kMeshLevel * (spec.kind == StarterKind::Direct ? kDirectMesh : 1.0f);
    // Un diesel charge plus ses dents : mesuré, son engagement ne dépasse son lancement que de 4 à 7 dB, contre 11 à 13 dB.
    torqueScale_ = km::sqrt(km::max(spec.powerKw, 0.5f) / kReferencePowerKw * km::max(spec.compressionRatio, 1.0f) / kFloorRatio);
    const float squeeze = kFloorRatio / km::max(spec.compressionRatio, 1.0f);
    contactFloor_ = kContactFloor * squeeze * squeeze * squeeze * (spec.kind == StarterKind::Direct ? kDirectFloor : 1.0f);
    std::uint32_t seed = 0x1F123BB5u + static_cast<std::uint32_t>(spec.ringTeeth);
    const auto draw = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 16 & 0xFFFF) / 32768.0f - 1.0f;
    };
    for (float& error : ringErrors_) error = draw() * 0.35f;
    for (float& error : pinionErrors_) error = draw() * 0.3f;
    for (float& error : barErrors_) error = draw() * 0.4f;
    for (float& error : sunErrors_) error = draw() * 0.5f;
    // Mesuré : le coude à 2 kHz bouge peu d'un moteur à l'autre. C'est la fine structure qui change.
    const float housingScale = spec.kind == StarterKind::Direct ? 0.95f : 1.0f;
    const float scale = km::sqrt(sizeScale) * housingScale;
    const auto hzFor = [this, scale](float hz) { return std::min(hz * scale, sr_ * 0.4f); };
    const float knee = kContactKneeHz * (spec.kind == StarterKind::Direct ? kDirectKnee : 1.0f);
    contactTone_.set(hzFor(knee), kContactKneeQ, sr_);
    contactToneSteep_.set(hzFor(knee), kContactKneeQ, sr_);
    brushWeight_ = kBrushLevel * torqueScale_ * (spec.kind == StarterKind::Direct ? kDirectBrush : 1.0f);
    contactSplit_.set(hzFor(kContactSplitHz), kContactSplitQ, sr_);
    brushBand_.set(hzFor(kBrushHz), kBrushQ, sr_);
    hitBand_.set(hzFor(3500.0f), 0.4f, sr_);
    // Plus de puissance, un plongeur plus lourd et plus lent.
    const float heavy = km::sqrt(km::max(spec.powerKw, 0.5f) / kReferencePowerKw);
    plungerTravel_ = std::max(static_cast<int>(kPlungerSeconds * heavy * sr_), 1);
    int at = plungerTravel_;
    float force = kPlungerStop;
    for (std::size_t i = 0; i < plungerStops_.size(); ++i) {
        plungerStops_[i] = at;
        plungerForces_[i] = force * (0.8f + 0.4f * (draw() * 0.5f + 0.5f));
        at += static_cast<int>((0.0025f + 0.0025f * (draw() * 0.5f + 0.5f)) * sr_);
        force *= 0.55f;
    }
    buildBody(scale);
}

void StarterSound::strikeWith(float force) {
    impact_ += force;
    click_ += force;
}

float StarterSound::sample(float rpm, bool cranking, float level) {
    if (spec_.powerKw <= 0.0f) return 0.0f;
    // Plus rien à exciter, et le dernier écho est passé : ni son, ni calcul, ni tirage.
    if (!powered_ && !meshed_ && !wasPowered_ && !wasMeshed_ && silentFor_ > bodyLength_ + kBodyPartition) {
        pinionOmega_ = 0.0;
        return 0.0f;
    }

    const double dt = 1.0 / static_cast<double>(sr_);
    const double turnsPerRad = 1.0 / (2.0 * km::kPi);
    const double gear = static_cast<double>(spec_.ringTeeth) / spec_.pinionTeeth;

    // Le contact : le relais, puis le plongeur qui court et frappe sa butée, avec ses rebonds.
    if (powered_ && !wasPowered_) {
        sinceContact_ = 0;
        strikeWith(kRelayTick);
        // Le filtre du lanceur a pu s'arrêter sur le régime du moteur qui tournait : le pignon, lui, repart de zéro.
        pitchFast_ = rpm;
        pitchSlow_ = rpm;
        forceMean_ = 0.2f;
        inContact_ = false;
    } else if (!powered_ && wasPowered_) {
        strikeWith(kReleaseStrike);
        sinceContact_ = -1;
    }
    wasPowered_ = powered_;
    float plunger = 0.0f;
    if (sinceContact_ >= 0) {
        if (sinceContact_ < plungerTravel_) {
            const float course = static_cast<float>(sinceContact_) / static_cast<float>(plungerTravel_);
            plunger = course * course * course * kPlungerNoise;
        }
        for (std::size_t i = 0; i < plungerStops_.size(); ++i) {
            if (sinceContact_ == plungerStops_[i]) strikeWith(plungerForces_[i]);
        }
        ++sinceContact_;
    }
    // Le pignon entre dans la couronne, moteur électrique à l'arrêt : tout le courant, tout le choc.
    if (meshed_ && !wasMeshed_) {
        strikeWith(kMeshStrike);
        shock_ = 1.0f;
    }
    wasMeshed_ = meshed_;

    // Le lanceur et ses cannelures amortissent les à-coups : les dents n'en voient qu'une partie.
    pitchFast_ += (rpm - pitchFast_) * pitchFastCoef_;
    pitchSlow_ += (rpm - pitchSlow_) * pitchSlowCoef_;
    const double ringOmega = static_cast<double>(pitchSlow_ + (pitchFast_ - pitchSlow_) * kPitchSwing) * 2.0 * km::kPi / 60.0;

    // Le couple arrive par tranches : on l'interpole, pour qu'aucune marche ne s'entende.
    force_ += ((meshed_ ? load_ : 0.0f) - force_) * forceCoef_;
    const bool driving = meshed_ && cranking;
    float contact = 0.0f;
    float grit = 1.0f;
    float line = 0.0f;
    float current = 0.0f;
    if (meshed_) {
        pinionOmega_ = ringOmega * gear;
        current = static_cast<float>(std::max(1.0 - pinionOmega_ / pinionFreeOmega_, 0.0));
        // Le vrai courant : celui que la batterie laisse passer, en écart à la batterie de référence (cold_start.hpp). Nul moteur chaud.
        if (supply_ != 0.0f) current = km::max(current + supply_, 0.0f);
        ringTurns_ += ringOmega * turnsPerRad * dt;
        ringTurns_ -= std::floor(ringTurns_);
        pinionTurns_ += pinionOmega_ * turnsPerRad * dt;
        pinionTurns_ -= std::floor(pinionTurns_);
        contact = km::max(force_, 0.0f);
        if (driving) {
            // Une détente fait filer le vilebrequin : les dents se quittent, et la vitesse d'écart
            // qui s'accumule fait la force du choc quand la compression suivante les rapproche.
            forceMean_ += (force_ - forceMean_) * forceMeanCoef_;
            const bool rising = force_ > forceMean_;
            if (rising && !wasRising_) burstTarget_ = 1.0f + kBurstSpread * noise_.next();
            wasRising_ = rising;
            if (force_ <= 0.0f) {
                overrun_ += static_cast<double>(-force_) * dt;
                inContact_ = false;
            } else if (!inContact_) {
                inContact_ = true;
                strikeWith(km::min(static_cast<float>(overrun_) * kRecontactGain, 1.0f) * kRecontactLevel);
                overrun_ = 0.0;
            }
            const float ringPosition = static_cast<float>(ringTurns_) * static_cast<float>(spec_.ringTeeth);
            const int ringTooth = static_cast<int>(ringPosition) % spec_.ringTeeth;
            const int pinionTooth = static_cast<int>(static_cast<float>(pinionTurns_) * static_cast<float>(spec_.pinionTeeth)) % spec_.pinionTeeth;
            const float within = ringPosition - km::floor(ringPosition);
            grit = 1.0f + kToothGrit * dsp::sinTurns(within);
            const float tooth = 1.0f + ringErrors_[static_cast<std::size_t>(ringTooth)] + pinionErrors_[static_cast<std::size_t>(pinionTooth)];
            const float shape = dsp::sinTurns(within) + toothRough_ * (kMeshSecond * dsp::sinTurns(within * 2.0f) + kMeshThird * dsp::sinTurns(within * 3.0f));
            // Une raie qui va et vient : elle ne sort du bruit que par moments, selon la portée des dents.
            const float comes = km::max(0.4f + wanderSlow_[0] * 18.0f, 0.0f);
            line = shape * tooth * km::sqrt(contact + contactFloor_) * meshLevel_ * comes;
        }
    } else {
        // Pignon sorti : le moteur électrique s'emballe s'il est alimenté, puis retombe.
        const double drive = powered_ ? pinionStall_ * (1.0 - pinionOmega_ / pinionFreeOmega_) * (level > 0.5f ? 0.06 : 1.0) : -0.05 * pinionStall_;
        pinionOmega_ = std::max(pinionOmega_ + drive / pinionInertia_ * dt, 0.0);
        // Tant que le pignon n'est pas engagé, le courant passe encore par l'enroulement d'appel : presque rien.
        current = powered_ ? static_cast<float>(std::max(1.0 - pinionOmega_ / pinionFreeOmega_, 0.0)) * 0.08f : 0.0f;
    }

    // L'induit : ses lames et, à réducteur, les satellites. Il suit le pignon, mais à sa propre échelle.
    motorTurns_ += pinionOmega_ * static_cast<double>(spec_.reduction) * turnsPerRad * dt;
    motorTurns_ -= std::floor(motorTurns_);
    const float barPosition = static_cast<float>(motorTurns_) * static_cast<float>(spec_.commutatorBars);
    const float bars = dsp::sinTurns(barPosition);
    const float barWear = barErrors_[static_cast<std::size_t>(static_cast<int>(barPosition) % spec_.commutatorBars)];
    currentHeld_ += (current - currentHeld_) * currentCoef_;
    const float drawn = km::sqrt(currentHeld_ + (driving ? shock_ : 0.0f));
    line += km::tanh(bars * 2.5f) * (0.6f + barWear) * drawn * kMotorLineLevel;
    if (spec_.kind == StarterKind::Reduction) {
        const float sun = static_cast<float>(motorTurns_) * kSunTeeth;
        line += km::tanh(dsp::sinTurns(sun) * 2.0f) * (0.55f + sunErrors_[static_cast<std::size_t>(static_cast<int>(sun) % static_cast<int>(kSunTeeth))]) * drawn * kMotorLineLevel;
    }

    // Trois tirages de bruit, toujours dans le même ordre : contact, balais, chocs. Chaque bande a en
    // plus son propre hasard, lent et rapide : c'est ce qui les empêche de bouger d'un bloc.
    const float contactDraw = noise_.next();
    const float brushDraw = noise_.next();
    const float hitDraw = noise_.next();
    for (std::size_t i = 0; i < wander_.size(); ++i) {
        wander_[i] += (noise_.next() - wander_[i]) * wanderCoef_;
        wanderSlow_[i] += (noise_.next() - wanderSlow_[i]) * wanderSlowCoef_;
    }
    const auto moved = [this](std::size_t i) {
        const float fast = wander_[i] * kWanderDepth * 6.0f;
        const float slow = wanderSlow_[i] * kWanderSlowDepth * 12.0f;
        return km::max(1.0f + fast + slow, 0.2f);
    };

    // Deux étages : les balais, pas le contact des dents, doivent tenir seuls les aigus.
    contactTone_.process(contactDraw);
    contactToneSteep_.process(contactTone_.lp);
    const float contactNoise = contactToneSteep_.lp;
    contactSplit_.process(contactNoise);
    const float rumble = contactSplit_.lp;
    const float lowBand = contactSplit_.bp * contactSplit_.k;
    const float midBand = contactNoise - rumble - lowBand;

    // Le moteur a pris et entraîne le pignon, qui va sortir : la roue libre cliquette.
    burst_ += (burstTarget_ - burst_) * burstCoef_;
    const float rolling = driving ? (contact + contactFloor_) * burst_ : (meshed_ ? kOverrunRattle : 0.0f);
    const float rollingRoot = km::sqrt(rolling);
    const float gainLow = rollingRoot * kContactLowShare * torqueScale_ * moved(0);
    const float gainMid = rollingRoot * km::sqrt(rollingRoot) * torqueScale_ * 1.25f * moved(1) * grit;
    brushBand_.process(brushDraw * (0.85f + 0.15f * bars));
    brushRolloff_ += (brushBand_.bp - brushRolloff_) * brushRolloffCoef_;
    const float gainBrush = drawn * brushWeight_ * moved(2);
    hitBand_.process(hitDraw);
    impactHeld_ += (impact_ - impactHeld_) * impactRiseCoef_;
    const float strike = (hitBand_.bp * 0.7f + hitDraw * 0.3f) * (impactHeld_ * kImpactLevel + click_ * kClickLevel + plunger);
    const float shake = (lowBand + midBand) * shock_ * kShockLevel * (driving ? 1.0f : 0.0f);
    // Sous ce seuil, plus rien ne s'entend : le zéro exact laisse le silence s'installer.
    impact_ = impact_ < 1e-5f ? 0.0f : impact_ * impactDecay_;
    shock_ = shock_ < 1e-5f ? 0.0f : shock_ * shockDecay_;
    click_ = click_ < 1e-5f ? 0.0f : click_ * clickDecay_;
    if (impact_ == 0.0f && impactHeld_ < 1e-5f) impactHeld_ = 0.0f;
    if (currentHeld_ < 1e-5f) currentHeld_ = 0.0f;

    const float excitation = rumble * rollingRoot * kRumbleLevel + lowBand * gainLow + midBand * gainMid + brushRolloff_ * gainBrush + line + strike + shake;
    silentFor_ = excitation == 0.0f ? silentFor_ + 1 : 0;
    return body_.process(excitation) * kStarterLevel;
}

}  // namespace revforge
