#pragma once

#include <array>
#include <cstdint>

#include "dsp.hpp"
#include "revforge/gearbox.hpp"

namespace revforge {

/**
 * Le bruit de la boîte, à part du moteur : il reçoit l'état mécanique et rend un échantillon.
 *
 * Toutes les sources sont des forces, en newtons, appliquées aux roulements : elles traversent une seule réponse,
 * celle du carter, fixe. Plus de 99 % de ce bruit passe par la structure (Rust, Brandl et Thien 1992), et les raies
 * défilent devant ses résonances : c'est pour cela que le timbre ne suit pas la vitesse.
 *
 *  - Le sifflement : l'erreur de transmission de chaque paire chargée, dent par dent. Sa profondeur vient des
 *    rapports de conduite (Velex 2012), chaque dent a ses défauts, qui reviennent à chaque tour.
 *  - Le claquement : les roues folles, simulées dans leur jeu, secouées par l'acyclisme. Il n'y a rien à déclencher :
 *    il naît au-dessus du seuil de Rust (J·α > traînée) et meurt avec lui, donc quand on débraye.
 *  - Les passages : cône, baladeur qui force, chocs d'engagement.
 *
 * Le bruit a son propre générateur : il ne décale jamais les tirages du moteur. Sans `setDrive`, il reste muet et ne
 * coûte rien.
 */
class GearboxSound {
public:
    explicit GearboxSound(float sampleRate);

    /** La boîte montée. Calcule la réponse du carter : à ne pas appeler à chaque bloc, seulement si la pièce change. */
    void setup(const GearboxSpec& spec);
    void setDrive(const GearboxDrive& drive);
    /** `enginePhase` : position dans le cycle de 720°, de 0 à 1. Les chocs du claquement restent en phase avec les explosions. */
    float sample(float enginePhase);
    /** Où une roue folle en est dans son jeu, de -1 à 1, et la force du dernier choc. Lecture seule, pour l'affichage. */
    float looseShare(int index) const;
    float strike() const { return strike_; }

private:
    static constexpr int kHarmonics = 6;
    static constexpr int kStages = 3;
    static constexpr int kHousingModes = 6;
    static constexpr int kHousingEchoes = 3;
    static constexpr int kEchoSamples = 2048;
    static constexpr int kSilentAfter = 4096;
    static constexpr int kControlSamples = 32;

    /** Une paire de dents : ses harmoniques, selon le flanc qui porte. */
    struct Voice {
        int driver = 0;
        int driven = 0;
        std::array<float, kHarmonics> drive{};
        std::array<float, kHarmonics> coast{};
        std::array<float, kHarmonics> phase{};
        std::array<float, kHarmonics> phaseCoast{};
        float errorForce = 0.0f;   // force due aux seuls défauts, à faible charge (N)
        float designForce = 0.0f;  // force à laquelle les corrections de profil annulent la déformation (N)
        float pitchRadius = 0.03f; // rayon primitif du pignon menant (m)
        float weight = 1.0f;
        int harmonics = kHarmonics;
    };

    /** Un étage chargé, qui tourne : position en dents, défauts de la dent en cours. */
    struct Stage {
        const Voice* voice = nullptr;
        double teeth = 0.0;
        float speedFactor = 1.0f;  // vitesse de cet arbre rapportée à l'arbre de référence
        bool fromOutput = false;   // vitesse lue sur l'arbre de sortie, donc les roues, pas sur le primaire
        float fm = 1.0f;           // part de l'acyclisme qui atteint cet arbre
        int lastTooth = -1;
        float ampFrom = 1.0f;
        float ampTo = 1.0f;
        float pitchFrom = 0.0f;
        float pitchTo = 0.0f;
        float forceNow = 0.0f;
        float forceStep = 0.0f;
        std::array<float, kHarmonics> gain{};
        std::array<float, kHarmonics> offset{};
        int harmonics = 0;
    };

    void buildHousing(const GearboxSpec& spec);
    Voice makeVoice(const GearPair& pair, double moduleMm, double helixDeg, double limitNm, float errorForce, float designShare, float weight, bool spur);
    void chooseStages();
    void control();
    float whine();
    float rattle(float phase);
    float transients();
    void toothDefects(Stage& stage, int tooth);
    float fastSin(float turns) const;
    float toothError(int tooth, int salt) const;

    float sr_;
    float dt_;
    bool fed_ = false;
    bool ready_ = false;
    GearboxSpec spec_;
    GearboxDrive target_;
    dsp::Noise noise_;
    const std::array<float, dsp::kSineSize + 1>& sine_;

    // Vitesses et couple, lissés : ils arrivent par blocs, mais doivent évoluer à chaque échantillon.
    float inputOmega_ = 0.0f;
    float outputOmega_ = 0.0f;
    float inputStep_ = 0.0f;
    float outputStep_ = 0.0f;
    float torque_ = 0.0f;
    float flank_ = 0.0f;       // 0 : flanc d'attaque, 1 : flanc de retenue
    float acyclism_ = 0.0f;
    float fm_ = 0.0f;          // écart de vitesse dû à l'acyclisme, en part de la vitesse du primaire
    float accelNow_ = 0.0f;
    float smoothFast_ = 0.0f;
    float smoothSlow_ = 0.0f;
    int controlLeft_ = 0;
    int activeGear_ = 0;
    int stages_ = 0;
    std::array<Stage, kStages> stage_{};

    // Les paires de cette boîte.
    std::array<Voice, kMaxGears> pairVoice_{};
    Voice inputVoice_, reverseInVoice_, reverseOutVoice_, finalVoice_;
    std::array<float, 256> errors_{};

    // Bruit de contact et balancement des raies.
    float rough_ = 0.0f;
    float roughTarget_ = 0.0f;
    float roughStep_ = 0.0f;
    float roughCoef_ = 0.0f;
    std::array<float, kHarmonics> wander_{};
    std::array<float, kHarmonics> wanderSlow_{};
    float wanderCoef_ = 0.0f;
    float wanderSlowCoef_ = 0.0f;
    float drift_ = 0.0f;
    float driftCoef_ = 0.0f;
    float driftNow_ = 0.0f;
    float contactLevel_ = 0.0f;
    float bearingLevel_ = 0.0f;
    float slipLevel_ = 0.0f;
    float releaseLevel_ = 0.0f;
    float judderPhase_ = 0.0f;
    dsp::Svf broadBand_, slipBand_, releaseBand_;

    // Le claquement.
    std::array<float, kLooseGears> looseAngle_{};
    std::array<float, kLooseGears> looseSpeed_{};
    int firedTurn_ = -1;
    float fireGain_ = 1.0f;
    float strike_ = 0.0f;
    float strikeDecay_ = 0.0f;
    float rattling_ = 0.0f;
    dsp::Svf ringBand_;

    // Les chocs d'engagement, et le baladeur qui force.
    double lastImpacts_ = 0.0;
    double lastClicks_ = 0.0;
    float bang_ = 0.0f;
    float bangDecay_ = 0.0f;
    float click_ = 0.0f;
    float clickDecay_ = 0.0f;
    float grindPhase_ = 0.0f;
    float grindNext_ = 1.0f;
    float grindHit_ = 0.0f;
    float grindDecay_ = 0.0f;
    float grindChatter_ = 0.0f;
    float grindLevel_ = 0.0f;
    float coneLevel_ = 0.0f;
    dsp::Svf coneBand_;

    // Le carter : figé une fois la boîte choisie. Quelques résonances larges, deux diffuseurs qui étalent les chocs,
    // et des échos très courts du plancher, qui creusent la fine structure que traversent les raies.
    float housing(float force);
    std::array<dsp::Svf, kHousingModes> modes_{};
    std::array<float, kHousingModes> modeGain_{};
    std::array<float, 2> diffuseGain_{};
    std::array<float, 256> diffuseA_{};
    std::array<float, 512> diffuseB_{};
    int diffuseLengthA_ = 1;
    int diffuseLengthB_ = 1;
    int diffuseAtA_ = 0;
    int diffuseAtB_ = 0;
    std::array<float, kEchoSamples> echo_{};
    std::array<int, kHousingEchoes> echoDelay_{};
    std::array<float, kHousingEchoes> echoGain_{};
    int echoAt_ = 0;
    float housingDirect_ = 0.35f;
    int silentFor_ = 0;
};

}  // namespace revforge
