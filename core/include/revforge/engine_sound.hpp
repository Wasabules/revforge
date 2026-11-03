#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "revforge/combustion.hpp"
#include "revforge/ecu.hpp"
#include "revforge/cam.hpp"
#include "revforge/engine_config.hpp"
#include "revforge/gearbox.hpp"
#include "revforge/supercharger.hpp"
#include "revforge/turbo.hpp"

namespace revforge {

/**
 * Positions d'écoute toutes prêtes : chacune est un point autour de la voiture (voir `setListener`).
 * Outside : derrière elle, à 5 m. L'ordre compte : le site envoie la position dans cette liste.
 */
enum class Listener { Outside, Cabin, EngineBay, Front, Side, Far, Pipe };

/**
 * Simulation acoustique d'un moteur.
 *
 * Un vrai moteur n'a rien de périodique : chaque explosion diffère de la précédente
 * et l'échappement est turbulent. On suit donc le trajet du gaz : chaque cylindre
 * garde sa propre pression, qui se vide à l'ouverture de la soupape (durée fixée
 * par l'alésage et la course), remonte son collecteur, rejoint le tube puis le
 * silencieux. S'y ajoutent l'admission et les bruits mécaniques.
 *
 * Tout évolue échantillon par échantillon : le résultat ne dépend pas de la taille
 * des blocs. `render` n'alloue jamais de mémoire et ne prend aucun verrou.
 *
 * `setProfile` et `render` s'appellent depuis le même thread ; régime et volume
 * peuvent être changés de n'importe où.
 */
class EngineSound {
public:
    explicit EngineSound(int sampleRate, std::int32_t seed = 0x2F6E2B1);
    ~EngineSound();
    EngineSound(const EngineSound&) = delete;
    EngineSound& operator=(const EngineSound&) = delete;

    /** Le passage d'un moteur à l'autre se fait après un bref fondu au silence. */
    void setProfile(const EngineProfile& profile);
    void setTargetRpm(float rpm) { targetRpm_.store(rpm, std::memory_order_relaxed); }
    void setVolume(float volume) { volume_.store(volume, std::memory_order_relaxed); }

    float targetRpm() const { return targetRpm_.load(std::memory_order_relaxed); }

    /**
     * État réel du moteur, quand on le connaît : remplissage (0-1), pression du turbo (0-1),
     * combustion coupée, démarreur. Sans cet appel, la charge se déduit de la montée du régime.
     * `throttle` : l'accélérateur (0-1), s'il est connu. Pied levé, l'injection se coupe ; un filet de gaz, lui, brûle
     * encore. Sans lui, seul le remplissage permet de deviner le lever de pied.
     */
    void setDrive(float load, float boost, bool cut, bool cranking, float throttle = -1.0f);
    /**
     * Le vrai turbo, quand il y en a un : la vitesse de l'arbre fait le sifflement, les débits font la décharge et le flutter.
     * Sans cet appel, le son déduit tout cela de la pression de `setDrive`.
     */
    void setTurbo(const TurboDrive& drive);
    /**
     * Le second arbre d'un biturbo : son propre sifflement, à sa propre vitesse, et son propre tirage. Sans cet appel, il
     * n'existe pas et ne coûte rien.
     */
    void setTurboB(const TurboDrive& drive);
    /** Le vrai compresseur, quand il y en a un : sa vitesse et l'écart de pression font son sifflement. */
    void setSupercharger(const SuperchargerDrive& drive);
    /**
     * Le calculateur, quand il agit : l'étincelle retardée ouvre les cylindres sur plus de pression, les cylindres coupés
     * n'explosent pas, et le mélange d'un cylindre dont seule l'étincelle est coupée peut détoner dans la ligne. Inactif, ou
     * sans cet appel, rien ne change au son.
     */
    void setEcu(const EcuDrive& drive);
    /** La distribution variable : le profil en prise et le déphaseur. Sans effet sur une came d'origine. */
    void setCam(const CamDrive& drive);
    /**
     * Ce que l'air de charge ajoute à la pression d'ouverture de l'échappement, en part d'un atmosphérique à pleine charge :
     * le protoxyde, ou une charge plus ou moins dense (`DrivingModel::chargeFill`). 0 : rien ne change.
     */
    void setCharge(float fill);
    /** Le démarreur : alimenté ; pignon engagé dans la couronne. Le contact des dents fait le son. */
    void setStarter(bool powered, bool meshed, float load = 0.5f);
    /** Vitesse de la voiture et glissement des pneus : vent, roulement, crissement. */
    void setRoad(float speedMs, float slipMs);
    /** La boîte et son état : ses dents sifflent et ses roues folles claquent. Sans cet appel, elle reste muette et ne coûte rien. */
    void setGearbox(const GearboxSpec& spec, const GearboxDrive& drive);
    /** Le claquement, pour l'affichage : position d'une roue folle dans son jeu (-1 à 1), force du dernier choc. Ne change rien au son. */
    float gearboxLoose(int index) const;
    float gearboxStrike() const;
    /**
     * Le cliquetis, quand le modèle de conduite dit que le moteur peut cliqueter : la chambre résonne, et le bloc la rayonne. Sans cet
     * appel, ou tant que `drive.active` est faux, rien ne tourne et rien ne s'entend.
     */
    void setKnock(const KnockDrive& drive);
    /** Les combustions qui ont cliqueté depuis le début. */
    std::uint32_t knocks() const;
    /** Une position toute prête, à la mesure de cette voiture. */
    void setListener(Listener listener);
    /**
     * Une position libre : en mètres, au sol et au centre de la voiture, x vers l'avant, y vers la gauche, z vers le
     * haut. La tête regarde la voiture et s'y rend en glissant, à `maxSpeed` m/s au plus : ni clic, ni saut.
     */
    void setListenerPosition(float x, float y, float z, float maxSpeed = 15.0f);
    /** Où est la tête en ce moment, dans le repère de la voiture. */
    float listenerX() const;
    float listenerY() const;
    float listenerZ() const;
    /** Niveau estimé à la position d'écoute, pondéré A, constante de temps « fast » : indépendant du volume. */
    float levelDbA() const;
    /** Ce que le limiteur retire en ce moment (1 : rien). Il ne doit plus travailler en continu. */
    float limiterGain() const;
    /** Nombre de détonations depuis le début : pour faire jaillir une flamme à l'écran. */
    std::uint32_t pops() const;
    /** Deux temps : la part des explosions manquées depuis le début (0 sur un quatre-temps), et leur nombre. */
    float misfireShare() const;
    std::uint32_t twoStrokeEvents() const;
    /** Vitesse de la turbine, 0 à 1 : elle garde son élan quand la pression retombe. */
    float turbine() const;
    /** Décharge du turbo en cours (0 : aucune). */
    float turboRelease() const;

    /** Vrai quand la sortie est muette et le restera : la piste peut être mise en pause. */
    bool isSilent() const;

    /** Un seul canal : un micro au centre de la tête. Sert aux tests et aux empreintes. */
    void render(float* out, int count);
    /** Les deux oreilles : retards et ombre de la tête. */
    void renderStereo(float* left, float* right, int count);

    /** Pour l'affichage : charge estimée (0-1), pression du turbo (0-1). */
    float load() const;
    float boost() const;

private:
    struct State;
    std::unique_ptr<State> s_;
    std::atomic<float> targetRpm_{0.0f};
    std::atomic<float> volume_{0.7f};
};

}  // namespace revforge
