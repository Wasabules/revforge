#pragma once

#include <array>

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * L'architecture d'un moteur à pistons : où est chaque cylindre, quand il allume, dans quelle culasse il souffle.
 * Tout part de l'ordre d'allumage réel et de la place des numéros (culasse, rangée, rang le long du vilebrequin) : les
 * intervalles et les angles des manetons s'en déduisent. Voir « Les architectures » dans docs/modele.md.
 *
 * C'est la grandeur que lisent le son (intervalles, partage entre les lignes), le démarreur (compressions) et, à venir,
 * un turbo par culasse. `web/src/lib/architecture.ts` en est la recopie pour le dessin et le gaz à l'écran.
 */
struct Architecture {
    /** La disposition réellement construite : un VR ou un W qui n'existe pas à ce nombre de cylindres est construit en V. */
    Layout layout = Layout::Inline;
    int cylinders = 0;
    /** Culasses, donc collecteurs : une en ligne et en VR, deux en V, en W et à plat. */
    int heads = 1;
    /** Angle entre les axes des deux culasses (°) : le V, l'angle entre les deux bancs d'un W, 180 à plat. */
    double headAngleDeg = 0.0;
    /** Angle entre les deux rangées d'une même culasse (°) : celui du VR, ou des bancs d'un W. 0 sinon. */
    double rowAngleDeg = 0.0;
    /**
     * Désaxage d'un VR et d'un W : l'axe de chaque rangée passe à cette distance du vilebrequin, vers l'extérieur de sa culasse
     * (12,5 mm chez VW, SSP 248). Le point mort haut de la rangée tourne alors de asin(e / (l + r)) vers l'extérieur.
     */
    double rowOffsetMm = 0.0;
    /** Manetons communs à deux cylindres face à face : les intervalles découlent de l'angle du V, inégaux s'il ne divise pas le cycle. */
    bool sharedPins = false;
    /** L'ordre d'allumage, numéros à partir de 1. */
    std::array<int, kMaxCylinders> order{};
    /** Angle du vilebrequin (°) au point mort haut d'allumage de chaque rang de l'ordre : le premier à 0, croissant, sous 720. */
    std::array<double, kMaxCylinders> firingDeg{};
    /** Par numéro − 1 : la culasse (0 à gauche vue de l'avant, 1 à droite), la rangée dans la culasse (0 ou 1), le rang le long du vilebrequin depuis l'avant. */
    std::array<int, kMaxCylinders> head{};
    std::array<int, kMaxCylinders> row{};
    std::array<int, kMaxCylinders> position{};
    /** Par numéro − 1 : inclinaison de l'axe du cylindre depuis la verticale, vue de l'avant, comptée dans le sens horaire (°). */
    std::array<double, kMaxCylinders> axisDeg{};

    /** Le rang de ce numéro dans l'ordre d'allumage. */
    int slotOf(int number) const;
    /** L'intervalle entre ce rang et le suivant (°). */
    double intervalAfterDeg(int slot) const;
    /** Vrai si les intervalles ne sont pas tous égaux. */
    bool uneven() const;
    /** Part des cylindres de cette culasse : le débit qui passe par son collecteur, et sa turbine s'il y en a une. */
    double headShare(int head) const;
};

/** L'architecture de ce moteur. Un rotatif n'en a pas : `cylinders` vaut alors 0. */
Architecture architectureOf(const EngineConfig& e);
Architecture architectureOf(Layout layout, int cylinders);

/** Vrai si ce moteur passe par `architectureOf` pour son son : VR, W, et plus de 12 cylindres. Les autres gardent le calcul d'origine. */
bool ownArchitecture(const EngineConfig& e);

/**
 * Les cylindres d'un VR et d'un W qui existent vraiment : VR5 et VR6, W8, W12 et W16. Ailleurs, la disposition demandée se
 * construit en V. Un moteur de plus de 12 cylindres ne se construit qu'en V ou en W.
 */
Layout builtLayout(Layout layout, int cylinders);

/**
 * L'angle du maneton de ce numéro (°, 0 à 360), pour une bielle `rodMm` et une course `strokeMm` : il est au point mort
 * haut de son cylindre, désaxage compris, quand le vilebrequin est à l'angle de son allumage.
 */
double pinDeg(const Architecture& a, int number, double strokeMm, double rodMm);

}  // namespace revforge
