#pragma once

#include <array>
#include <cstdint>

#include "dsp.hpp"
#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * Le tic d'une injection directe d'essence, à part du moteur. Chaque injecteur frappe deux fois par injection : l'induit sur
 * sa butée à l'ouverture, l'aiguille sur son siège à la fermeture, poussée par son ressort et par la pression de la rampe.
 * La pompe haute pression, entraînée par une came de l'arbre, claque à chaque course : sa soupape de dosage se ferme, et
 * l'onde de pression part dans la rampe. Des chocs, aux instants que donnent le calage et la durée d'injection, sur les modes
 * de la culasse et de la rampe : au-dessus de 5 kHz pour les injecteurs, vers 1,6 kHz pour la pompe (SAE 2011-01-0930).
 * Son générateur à lui : il ne décale aucun tirage du moteur. Voir « L'injection » dans docs/modele.md.
 */
class InjectorSound {
public:
    explicit InjectorSound(float sampleRate);

    void setup(const EngineProfile& profile);
    /** Tous les quelques échantillons : régime, charge, et le plancher du bloc au ralenti (Pa à 1 m), auquel ces chocs sont calés. */
    void control(float rpm, float load, float floorPa);
    /**
     * Un échantillon, en pascals à 1 m. `phase` : le cycle de 720°, en tours ; `firing` et `timing` : ceux du moteur, qui
     * placent l'explosion de chaque cylindre. `fuelGate` : 0 pendant une coupure d'injection, plus rien n'est injecté.
     */
    float sample(float phase, int cylinders, const std::array<float, kMaxCylinders>& firing, const std::array<float, kMaxCylinders>& timing, float fuelGate);

    /** Pour les essais : les chocs depuis la mise en place, injecteurs et pompe. */
    std::uint32_t needleHits() const { return needleHits_; }
    std::uint32_t pumpHits() const { return pumpHits_; }
    /** Durée d'une injection en ce moment (s), et pression de la rampe (bar). */
    float injectionSeconds() const { return injectionS_; }
    float railBar() const { return railBar_; }

private:
    float random();
    void strike(float& amplitude, int& left, float level);

    float sr_;
    std::uint32_t rng_ = 0x6C8E9CF5;
    float cylinderM3_ = 0.0005f;
    float redlineRpm_ = 6000.0f;
    float nozzleM2_ = 9.2e-8f;
    int strokes_ = 4;
    std::array<bool, kMaxCylinders> injecting_{};
    int pumpIndex_ = 0;

    float soi_ = 0.0f;
    float span_ = 0.01f;
    float injectionS_ = 0.0f;
    float railBar_ = 40.0f;
    float needleLevel_ = 0.0f;
    float pumpLevel_ = 0.0f;
    int burst_ = 4;
    int pumpBurst_ = 13;

    float needleAmp_ = 0.0f;
    int needleLeft_ = 0;
    float pumpAmp_ = 0.0f;
    int pumpLeft_ = 0;
    dsp::Svf needle_, needleHigh_, pump_, pumpHigh_;
    std::uint32_t needleHits_ = 0;
    std::uint32_t pumpHits_ = 0;
};

}  // namespace revforge
