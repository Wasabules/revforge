#pragma once

#include <array>
#include <vector>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

/**
 * De la voiture à l'oreille : ce que devient la pression quand elle quitte chaque source.
 *
 * Chaque source (Source, dans engine_config.hpp) arrive ici en pascals, à 1 m d'elle, et traverse dans l'ordre :
 * sa directivité, le masquage par la caisse, l'absorption de l'air, une ligne à retard, puis se répartit
 * entre le chemin direct et son image dans le sol, et enfin la tête. La sortie est la pression à chaque
 * oreille, en pascals. Rien n'est saisi par position : tout découle de la géométrie.
 *
 * Les formules sont exposées pour que les tests les confrontent aux valeurs publiées (tests/acoustics_test.cpp).
 */
namespace revforge::acoustics {

// L'air de référence, 20 °C au niveau de la mer. Celui du jour arrive par `CarAcoustics::air` (atmosphere.hpp).
inline constexpr float kSoundSpeed = 343.0f;
inline constexpr float kAirDensity = 1.204f;
inline constexpr float kHeadRadius = 0.0875f;
inline constexpr double kReferencePressure = 20e-6;
inline constexpr double kPi = 3.141592653589793;

// ── Formules ──

/** Absorption atmosphérique (dB/m), ISO 9613-1. */
double airAbsorptionDbPerM(double hz, double celsius = 20.0, double humidityPercent = 50.0, double kilopascal = 101.325);

/** Pondération A analogique (dB), IEC 61672-1. */
double aWeightingDb(double hz);

/**
 * Réflexion d'une onde plane sur un sol de résistivité `sigma` (Pa·s/m²), sous cet angle depuis la normale :
 * Delany et Bazley. Renvoie le module et la phase (radians) dans `magnitude` et `phase`.
 */
void groundReflection(double hz, double sigma, double cosIncidence, double& magnitude, double& phase);

/** Ce qu'un obstacle retire (dB) pour ce nombre de Fresnel N = 2·excès de chemin/λ : Maekawa, adouci en N = 0. */
double barrierLossDb(double fresnel);

/** Excès de chemin (m) du trajet le plus court autour de la caisse, par le toit ou par les côtés : 0 si le trajet est libre. */
float pathExcessM(const Point3& source, const Point3& listener, const CarAcoustics& car);

/** Trajet (m) d'une source à distance `r` du centre de la tête jusqu'à une oreille à l'angle `theta` : contourne la sphère. */
float earPathM(float r, float theta, float headRadius = kHeadRadius);

/** Filtre de tête de Brown et Duda : α(θ), de 2 face à la source à 0,1 vers 150°. */
float headShadowAlpha(float theta);

/** Gain (dB) de la bouche d'un tube non bafflé selon l'angle depuis l'axe, à ce ka : ce que réalise le filtre de directivité. */
double outletFilterDb(double ka, double theta);

/** Perte par transmission (dB) d'une plaque en champ diffus : loi de masse, plateau de coïncidence, et fuites. */
double panelLossDb(double hz, double surfaceMass, double coincidenceHz, double dampingDb, double trimFromHz, double trimCapDb, double leak);

/** Pression dans l'habitacle pour 1 Pa à 1 m de cette source, à cette fréquence : bilan d'énergie sur toutes les parois. */
double cabinTransfer(double hz, const CarAcoustics& car, Source source);

/** Ce qui compte dans une carrosserie, par niveau d'insonorisation. */
struct CabinSpec {
    double steelKgM2;     // tôle
    double glassKgM2;     // vitres
    double glassCoincidenceHz;
    double firewallKgM2;  // cloison, plancher : tôle et barrière
    double trimFromHz;    // au-dessus, le garnissage (double paroi) ajoute à la loi de masse
    double trimCapDb;     // ce qu'il ajoute au plus
    double leak;          // part de la surface qui fuit
    double absorption;    // absorption moyenne de l'habitacle à 1 kHz
    float structureGain;  // ce que laissent passer les supports moteur et les attaches de ligne
};
const CabinSpec& cabinSpec(Insulation insulation);

// ── Filtres ──

/** Filtre du premier ordre : une étagère de gain 1 aux basses fréquences, `gainHigh` au-dessus de `poleHz`. Passe tout droit si le pôle dépasse Nyquist. */
struct Shelf {
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;

    void set(float poleHz, float gainHigh, float rate);
    /** La même forme, donnée par son pôle et son zéro. Un zéro au-delà de Nyquist : un simple passe-bas. */
    void setPoleZero(float poleHz, float zeroHz, float rate);
    void bypass() { b0 = 1.0f; b1 = 0.0f; a1 = 0.0f; }
    float process(float x) {
        const float y = b0 * x + b1 * x1 + a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

/** Pondération A numérique : les pôles analogiques, le plus aigu corrigé de la déformation fréquentielle. */
class AWeighting {
public:
    void init(float rate);
    double process(double x);
    /** Réponse calculée (dB), pour le test face à la table de la norme. */
    double responseDb(double hz) const;

private:
    struct Section {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
    };
    float rate_ = 44100.0f;
    std::array<Section, 3> sections_{};
};

/** Le sonomètre : pression pondérée A, constante de temps « fast » (125 ms). */
class LevelMeter {
public:
    void init(float rate);
    void add(float left, float right);
    void addMono(float mono);
    /** Niveau pondéré A en dB. */
    float db() const;
    /** Part par source : le même calcul en niveau, à appeler après `add`. */
    double energy() const { return energy_; }

private:
    AWeighting left_, right_;
    double energy_ = 0.0;
    double coefficient_ = 0.0;
};

/** Le champ sonore autour de la voiture. Sans allocation après la construction. */
class SoundField {
public:
    explicit SoundField(float rate);

    /** Les dimensions de la voiture et la place de ses sources. Sans allocation. */
    void setCar(const CarAcoustics& car, bool hasExhaust);

    /**
     * Où est la tête, et vers où elle regarde (radians dans le plan de la voiture, 0 vers l'avant). `facing` NaN :
     * elle regarde la voiture. La tête se déplace en glissant, à `maxSpeed` m/s au plus : ni clic, ni saut.
     */
    void setTarget(const Point3& at, float facing, float maxSpeed);
    const Point3& position() const { return head_; }
    const Point3& target() const { return target_; }

    /** À appeler tous les `controlSamples` échantillons. */
    void control(int controlSamples);
    /** Un échantillon : `in` en pascals à 1 m de chaque source. Sortie : pression aux oreilles, ou au centre de la tête en mono. */
    void process(const std::array<float, kSources>& in, float& left, float& right);
    void setStereo(bool stereo);

    /** 0 dehors, 1 dans l'habitacle : le fondu entre les deux chemins. */
    float inside() const { return inside_; }
    /** La tête est-elle en train de glisser vers sa place ? */
    bool moving() const { return dirty_; }
    /** 0 capot fermé, 1 la tête tout près du bloc. */
    float bayOpen() const { return bayOpen_; }
    /** Ce que renvoie le lieu : pression réverbérée pour 1 Pa à 1 m. Les tests de propagation la coupent. */
    void setAmbient(float gain) { ambient_ = gain; }
    /** Vitesse du gaz en sortie (m/s) : l'écoulement renforce le son vers l'aval et l'affaiblit vers l'amont. */
    void setJetSpeed(float metresPerSecond) { jetSpeed_ = metresPerSecond; }
    /** Part de la pression dynamique du jet à cette oreille (0 : hors du jet). Voir `jetSignal`. */
    bool inJet() const { return jetExposed_; }
    /** La pression du vent et des bouffées dans le jet, pour chaque sortie (Pa) : à donner avant `process` si `inJet`. */
    void setJetSignal(int outlet, float pascals) { jetSignal_[static_cast<std::size_t>(outlet)] = pascals; }

private:
    struct Path {
        float delay = 1.0f;
        float delayStep = 0.0f;
        float gain = 0.0f;
        float gainStep = 0.0f;
    };
    struct Ear {
        Path direct, mirror;
        float mirrorDamp = 0.0f;
        float mirrorCoef = 1.0f;
        Shelf head;
    };
    struct Emitter {
        std::vector<float> ring;
        int at = 0;
        int idle = 0;
        Shelf directivity, maskLow, maskHigh;
        float air = 0.0f;
        float airCoef = 1.0f;
        std::array<Ear, 2> ears{};
        SourcePlace place{};
        bool enabled = true;
        float flowCos = 0.0f;  // cosinus de l'angle entre l'axe de sortie et la tête
        float mirrorCoherence = 1.0f;
    };

    void updateGeometry(int controlSamples, bool snap);
    void updateEmitter(Emitter& emitter, int index, int controlSamples, bool snap);

    float rate_;
    int mask_ = 0;
    bool stereo_ = false;
    bool dirty_ = true;
    bool snap_ = true;
    CarAcoustics car_;
    std::array<Emitter, kSources> emitters_;
    Point3 head_{}, target_{};
    float facing_ = 0.0f;
    float facingWanted_ = 0.0f;
    bool facesCar_ = true;
    float maxSpeed_ = 15.0f;
    float inside_ = 0.0f;
    float bayOpen_ = 0.0f;
    float airPerMetre8k_ = 0.0f;
    /** La vitesse du son de l'air du jour (`CarAcoustics::air`). */
    float soundSpeed_ = kSoundSpeed;
    std::array<dsp::AllPass, 3> ambientLeft_, ambientRight_;
    float ambient_ = 0.0f;
    float outsideGain_ = 1.0f;
    float outsideStep_ = 0.0f;
    float jetSpeed_ = 0.0f;
    bool jetExposed_ = false;
    std::array<float, 2> jetSignal_{};
    std::array<std::array<float, 2>, 2> jetGain_{};  // par sortie, par oreille
};

/**
 * L'habitacle : chaque groupe de sources traverse les parois (bilan d'énergie, filtre calculé à l'application
 * d'une voiture), puis la cabine ajoute ses modes et sa courte réverbération. Les supports moteur font entrer
 * directement les bas ordres, sans passer par l'air.
 */
class CabinPath {
public:
    explicit CabinPath(float rate);

    /** Calcule les filtres de cette voiture : long, à faire pendant un silence. */
    void setCar(const CarAcoustics& car);
    /** `structure` : force dans les supports moteur, en unités de pression à la tête. */
    void process(const std::array<float, kSources>& in, float structure, float& left, float& right, bool stereo);
    void reset();

    static constexpr int kGroups = 3;
    /** La réponse de la caisse : 1024 points à 48 kHz, 21 ms. À plus haute fréquence d'échantillonnage, autant de millisecondes. */
    static constexpr int kFirLength = 1024;
    static int groupOf(Source source);
    /** Le nombre de points de la réponse à cette fréquence : une puissance de deux, au moins `kFirLength`, la même durée qu'à 48 kHz. */
    static int firLengthFor(float rate);

private:
    void design(int group, const CarAcoustics& car);

    float rate_;
    int firLength_ = kFirLength;
    dsp::Fft fft_;
    std::vector<float> re_, im_, fir_;
    std::array<dsp::Convolver, kGroups> filters_;
    std::array<dsp::AllPass, 3> roomLeft_, roomRight_;
    std::array<dsp::Svf, 3> modes_;
    std::array<float, 3> modeGain_{};
    dsp::Svf boomBody_, boomFloor_;
    float roomMix_ = 0.4f;
    float structureGain_ = 1.0f;
};

}  // namespace revforge::acoustics
