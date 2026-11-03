#include "revforge/architecture.hpp"

#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;

/**
 * Un moteur réel : l'ordre d'allumage, puis la place de chaque numéro. `left` et `right` : les numéros de chaque culasse, de
 * l'avant vers l'arrière ; `right` vide pour une seule culasse. `rows` : la rangée de chaque numéro dans sa culasse, une
 * lettre par numéro à partir de 1 ('0' ou '1'), vide s'il n'y en a qu'une. Le web recopie ces lignes (`architecture.ts`) :
 * `architectures.spec.ts` les compare, d'où une entrée par ligne.
 */
struct Entry {
    Layout layout;
    int cylinders;
    double headAngleDeg;
    double rowAngleDeg;
    double rowOffsetMm;
    bool sharedPins;
    std::array<int, kMaxCylinders> order;
    std::array<int, kMaxCylinders> left;
    std::array<int, kMaxCylinders> right;
    const char* rows;
};

// clang-format off
// ── Les architectures du simulateur avant les VR et les W : la recopie de `FIRING_ORDERS` et `placementOf` du site ──
// Elles ne passent pas par ici pour leur son (calcul d'origine, au bit près), mais disent leurs culasses à qui les lit.
const Entry kEntries[] = {
    {Layout::V, 2, 90.0, 0.0, 0.0, false, {1, 2}, {1}, {2}, ""},
    {Layout::V, 3, 60.0, 0.0, 0.0, false, {1, 3, 2}, {1, 2}, {3}, ""},
    {Layout::V, 4, 60.0, 0.0, 0.0, false, {1, 3, 4, 2}, {1, 2}, {4, 3}, ""},
    {Layout::V, 5, 72.0, 0.0, 0.0, false, {1, 2, 4, 5, 3}, {1, 2, 3}, {4, 5}, ""},
    {Layout::V, 6, 60.0, 0.0, 0.0, false, {1, 5, 3, 6, 2, 4}, {1, 2, 3}, {6, 5, 4}, ""},
    {Layout::V, 8, 90.0, 0.0, 0.0, false, {1, 8, 4, 3, 6, 5, 7, 2}, {1, 4, 7, 6}, {8, 3, 2, 5}, ""},
    {Layout::CrossPlane, 8, 90.0, 0.0, 0.0, false, {1, 8, 4, 3, 6, 5, 7, 2}, {2, 4, 6, 8}, {1, 3, 5, 7}, ""},
    {Layout::V, 10, 90.0, 0.0, 0.0, false, {1, 6, 5, 10, 2, 7, 3, 8, 4, 9}, {1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}, ""},
    {Layout::V, 12, 60.0, 0.0, 0.0, false, {1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10}, {1, 2, 3, 4, 5, 6}, {7, 8, 9, 10, 11, 12}, ""},
    // ── Les VR, les W, le V14 et le V16 : voir « Les architectures » dans docs/modele.md pour les sources ──
    // VR5 et VR6 : 15° entre les rangées, axes désaxés de 12,5 mm (VW, SSP 248). Les numéros alternent d'une rangée à l'autre (supposé).
    {Layout::Vr, 5, 0.0, 15.0, 12.5, false, {1, 2, 4, 5, 3}, {1, 2, 3, 4, 5}, {}, "01010"},
    {Layout::Vr, 6, 0.0, 15.0, 12.5, false, {1, 5, 3, 6, 2, 4}, {1, 2, 3, 4, 5, 6}, {}, "010101"},
    // W8, W12 : deux VR à 72° (SSP 248). W16 : deux VR8 à 90° (Bugatti) ; numéros de gauche puis de droite, comme le SSP 248 pour
    // le W12 (supposé pour le W16). Les deux culasses sont deux VR identiques : la même rangée relative au même rang.
    {Layout::W, 8, 72.0, 15.0, 12.5, false, {1, 5, 4, 8, 6, 3, 7, 2}, {1, 2, 3, 4}, {5, 6, 7, 8}, "01010101"},
    {Layout::W, 12, 72.0, 15.0, 12.5, false, {1, 12, 5, 8, 3, 10, 6, 7, 2, 11, 4, 9}, {1, 2, 3, 4, 5, 6}, {7, 8, 9, 10, 11, 12}, "010101010101"},
    {Layout::W, 16, 90.0, 15.0, 12.5, false, {1, 14, 9, 4, 7, 12, 15, 6, 13, 8, 3, 16, 11, 2, 5, 10}, {1, 2, 3, 4, 5, 6, 7, 8}, {9, 10, 11, 12, 13, 14, 15, 16}, "0101010101010101"},
    // V14 : à 45°, comme le MAN 14V32/44CR, sur sept manetons communs ; aucun ordre publié, celui qui annule au mieux les couples
    // de basculement des deux premiers ordres (1-2-4-6-7-5-3 à gauche, calcul dans docs/modele.md).
    {Layout::V, 14, 45.0, 0.0, 0.0, true, {1, 8, 2, 9, 4, 11, 6, 13, 7, 14, 5, 12, 3, 10}, {1, 2, 3, 4, 5, 6, 7}, {8, 9, 10, 11, 12, 13, 14}, ""},
    // V16 : celui de la Cadillac 452 de 1930, deux huit-en-ligne à 45° sur un même vilebrequin ; l'ordre d'un huit-en-ligne
    // (1-6-2-5-8-3-7-4) pour chaque rangée, la droite 45° après la gauche (ordre d'usine non trouvé).
    {Layout::V, 16, 45.0, 0.0, 0.0, true, {1, 9, 6, 14, 2, 10, 5, 13, 8, 16, 3, 11, 7, 15, 4, 12}, {1, 2, 3, 4, 5, 6, 7, 8}, {9, 10, 11, 12, 13, 14, 15, 16}, ""},
};
// clang-format on

/** Ordres d'allumage d'un moteur en ligne : ceux du site (`FIRING_ORDERS`), jusqu'à 12. */
const std::array<int, kMaxCylinders>& inlineOrder(int cylinders) {
    static const std::array<int, kMaxCylinders> kOrders[] = {
        {1, 2}, {1, 3, 2}, {1, 3, 4, 2}, {1, 2, 4, 5, 3}, {1, 5, 3, 6, 2, 4}, {1, 8, 4, 3, 6, 5, 7, 2}, {1, 6, 5, 10, 2, 7, 3, 8, 4, 9}, {1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10},
    };
    static const int kCounts[] = {2, 3, 4, 5, 6, 8, 10, 12};
    for (std::size_t k = 0; k < sizeof(kCounts) / sizeof(kCounts[0]); ++k) {
        if (kCounts[k] == cylinders) return kOrders[k];
    }
    static std::array<int, kMaxCylinders> sequential = [] {
        std::array<int, kMaxCylinders> out{};
        for (int i = 0; i < kMaxCylinders; ++i) out[static_cast<std::size_t>(i)] = i + 1;
        return out;
    }();
    return sequential;
}

const Entry* entryOf(Layout layout, int cylinders) {
    for (const Entry& entry : kEntries) {
        if (entry.layout == layout && entry.cylinders == cylinders) return &entry;
    }
    return nullptr;
}

double wrap360(double deg) {
    const double out = std::fmod(deg, 360.0);
    return out < 0.0 ? out + 360.0 : out;
}

}  // namespace

Layout builtLayout(Layout layout, int cylinders) {
    if (layout == Layout::Rotary) return layout;
    if (layout == Layout::Vr) return entryOf(Layout::Vr, cylinders) != nullptr ? layout : Layout::V;
    if (layout == Layout::W) return entryOf(Layout::W, cylinders) != nullptr ? layout : Layout::V;
    if (cylinders > 12) return Layout::V;
    return layout;
}

bool ownArchitecture(const EngineConfig& e) {
    if (e.layout == Layout::Rotary) return false;
    return e.layout == Layout::Vr || e.layout == Layout::W || e.cylinders > 12;
}

Architecture architectureOf(const EngineConfig& e) {
    if (e.layout == Layout::Rotary) return Architecture{};
    return architectureOf(e.layout, e.cylinders);
}

Architecture architectureOf(Layout wanted, int count) {
    Architecture a;
    const int cylinders = count < 1 ? 1 : (count > kMaxCylinders ? kMaxCylinders : count);
    const Layout layout = builtLayout(wanted, cylinders);
    a.layout = layout;
    a.cylinders = cylinders;
    if (layout == Layout::Rotary) {
        a.cylinders = 0;
        return a;
    }
    const auto at = [](int number) { return static_cast<std::size_t>(number - 1); };
    // Ce qui n'est pas dans la table : en ligne, à plat, ou un V à un nombre que le site ne propose pas.
    const Entry* entry = entryOf(layout == Layout::CrossPlane && cylinders != 8 ? Layout::V : layout, cylinders);
    if (layout == Layout::Inline || (entry == nullptr && layout != Layout::Boxer)) {
        a.order = inlineOrder(cylinders);
        if (layout != Layout::Inline) {
            // Un V sans table : les numéros de l'avant à gauche, puis à droite, comme `placementOf` par défaut.
            a.heads = cylinders >= 2 ? 2 : 1;
            a.headAngleDeg = a.heads == 2 ? 90.0 : 0.0;
            const int front = (cylinders + 1) / 2;
            for (int n = 1; n <= cylinders; ++n) {
                a.head[at(n)] = n <= front ? 0 : 1;
                a.position[at(n)] = n <= front ? n - 1 : n - 1 - front;
            }
        } else {
            for (int n = 1; n <= cylinders; ++n) a.position[at(n)] = n - 1;
        }
    } else if (layout == Layout::Boxer) {
        // À plat : deux cylindres face à face sur deux manetons opposés ; les numéros comme `placementOf`.
        a.order = inlineOrder(cylinders);
        a.heads = cylinders >= 2 ? 2 : 1;
        a.headAngleDeg = 180.0;
        const int half = cylinders / 2;
        for (int r = 0; r < (cylinders + 1) / 2; ++r) {
            int left = 0, right = 0;
            if (cylinders % 2 == 1) {
                left = r + 1;
                right = r + 1 + (cylinders + 1) / 2 <= cylinders ? r + 1 + (cylinders + 1) / 2 : 0;
            } else if (cylinders == 4 || cylinders == 6) {
                left = r + 1;
                right = cylinders - r;
            } else {
                left = a.order[static_cast<std::size_t>(r)];
                right = a.order[static_cast<std::size_t>(r + half)];
            }
            a.head[at(left)] = 0;
            a.position[at(left)] = 2 * r;
            if (right > 0) {
                a.head[at(right)] = 1;
                a.position[at(right)] = 2 * r + 1;
            }
        }
    } else {
        a.order = entry->order;
        a.headAngleDeg = entry->headAngleDeg;
        a.rowAngleDeg = entry->rowAngleDeg;
        a.rowOffsetMm = entry->rowOffsetMm;
        a.sharedPins = entry->sharedPins;
        a.heads = entry->right[0] == 0 ? 1 : 2;
        for (int k = 0; k < kMaxCylinders && entry->left[static_cast<std::size_t>(k)] != 0; ++k) {
            const int n = entry->left[static_cast<std::size_t>(k)];
            a.head[at(n)] = 0;
            a.position[at(n)] = k;
        }
        for (int k = 0; k < kMaxCylinders && entry->right[static_cast<std::size_t>(k)] != 0; ++k) {
            const int n = entry->right[static_cast<std::size_t>(k)];
            a.head[at(n)] = 1;
            a.position[at(n)] = k;
        }
        for (int n = 1; n <= cylinders && entry->rows[0] != '\0'; ++n) a.row[at(n)] = entry->rows[at(n)] == '1' ? 1 : 0;
    }

    // Les axes : chaque culasse de part et d'autre de la verticale, chaque rangée de part et d'autre de l'axe de sa culasse.
    for (int n = 1; n <= cylinders; ++n) {
        const double headAxis = a.heads == 2 ? (a.head[at(n)] == 0 ? -0.5 : 0.5) * a.headAngleDeg : 0.0;
        const double rowAxis = a.rowAngleDeg > 0.0 ? (a.row[at(n)] == 0 ? -0.5 : 0.5) * a.rowAngleDeg : 0.0;
        a.axisDeg[at(n)] = headAxis + rowAxis;
    }

    // Les allumages : à intervalles égaux, sauf manetons communs à un angle de V qui ne divise pas le cycle. Sur ceux-là, chaque
    // cylindre de droite allume l'angle du V après celui de gauche du même maneton, et la gauche allume à intervalles égaux.
    if (a.sharedPins && a.heads == 2) {
        const int pairs = cylinders / 2;
        for (int s = 0; s < cylinders; ++s) {
            const int pair = s / 2;
            a.firingDeg[static_cast<std::size_t>(s)] = 720.0 * pair / pairs + (s % 2 == 1 ? a.headAngleDeg : 0.0);
        }
    } else {
        for (int s = 0; s < cylinders; ++s) a.firingDeg[static_cast<std::size_t>(s)] = 720.0 * s / cylinders;
    }
    return a;
}

int Architecture::slotOf(int number) const {
    for (int s = 0; s < cylinders; ++s) {
        if (order[static_cast<std::size_t>(s)] == number) return s;
    }
    return 0;
}

double Architecture::intervalAfterDeg(int slot) const {
    if (cylinders <= 0) return 0.0;
    const int next = (slot + 1) % cylinders;
    const double to = next == 0 ? 720.0 : firingDeg[static_cast<std::size_t>(next)];
    return to - firingDeg[static_cast<std::size_t>(slot)];
}

bool Architecture::uneven() const {
    const double even = 720.0 / (cylinders > 0 ? cylinders : 1);
    for (int s = 0; s < cylinders; ++s) {
        if (std::fabs(intervalAfterDeg(s) - even) > 1e-6) return true;
    }
    return false;
}

double Architecture::headShare(int which) const {
    if (cylinders <= 0) return 0.0;
    int count = 0;
    for (int n = 0; n < cylinders; ++n) count += head[static_cast<std::size_t>(n)] == which ? 1 : 0;
    return static_cast<double>(count) / cylinders;
}

double pinDeg(const Architecture& a, int number, double strokeMm, double rodMm) {
    const std::size_t i = static_cast<std::size_t>(number - 1);
    const double axis = a.axisDeg[i];
    // Le désaxage : l'axe de la rangée passe à `e` du vilebrequin, vers l'extérieur de sa culasse. Le point mort haut est atteint
    // quand maneton, axe de piston et vilebrequin sont alignés : à asin(e / (l + r)) de l'axe, du même côté.
    double tdc = axis;
    if (a.rowOffsetMm != 0.0 && a.rowAngleDeg > 0.0) {
        const double shift = std::asin(a.rowOffsetMm / (rodMm + strokeMm / 2.0)) * 180.0 / kPi;
        tdc += a.row[i] == 0 ? -shift : shift;
    }
    return wrap360(tdc - a.firingDeg[static_cast<std::size_t>(a.slotOf(number))]);
}

}  // namespace revforge
