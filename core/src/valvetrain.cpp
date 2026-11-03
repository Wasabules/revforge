#include "revforge/valvetrain.hpp"

#include "revforge/architecture.hpp"

#include <array>
#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;

// Ce qui reste de métal dans la culasse (mm) : entre la soupape et le bord de l'alésage, entre deux sièges.
constexpr double kEdgeMm = 2.5;
constexpr double kBridgeTwoValvesMm = 5.0;
constexpr double kBridgeFourValvesMm = 4.0;
// Deux soupapes, côte à côte en travers de l'alésage : l'échappement fait 83 % de l'admission, car le gaz chaud a plus de son.
constexpr double kExhaustShare = 0.83;
// Quatre soupapes, chacune dans son quart d'alésage : les admissions à 100° l'une de l'autre, les échappements à 80°.
// Leurs centres sont au rayon (d + pont) × ce facteur, 1 / (2 sin(écart / 2)), écrit en dur pour rester portable au bit près.
constexpr double kIntakeReach = 0.6527036446661393;
constexpr double kExhaustReach = 0.7778619134302062;
// La levée suit l'alésage, et non la soupape : il faut la même came ou presque pour une petite soupape ou une grande.
constexpr double kLiftPerBore = 0.12;
// Levée moyenne d'une levée en sinus, et gorge du siège : 90 % de la tête, sans compter la queue.
constexpr double kMeanLift = 2.0 / kPi;
constexpr double kThroatPerHead = 0.636;
constexpr double kDischarge = 0.7;

// Le gaz à l'ouverture de l'échappement (1250 K). L'air admis, lui, est celui du jour : `intakeSoundMs`, 343 m/s à 20 °C.
constexpr double kSoundExhaustMs = 691.0;
// fill(Z) = (1 + (Z / 0,8)^4)^(-1/4) : l'ajustement de l'intégration, à 1,8 % près de Z = 0,2 à 2.
constexpr double kFillingKnee = 0.8;
// Le travail perdu, intégré tous les 0,05 de Z depuis 0 : tools/valve_filling.py.
constexpr double kExhaustStep = 0.05;
constexpr std::array<double, 25> kExhaustLossTable{
    0.000, -0.144, -0.188, -0.178, -0.129, -0.041, 0.092, 0.284, 0.549, 0.877, 1.214, 1.547, 1.874,
    2.193, 2.502, 2.801, 3.090, 3.368, 3.636, 3.894, 4.142, 4.380, 4.609, 4.829, 5.041,
};
// Cette table vaut pour 5 bar à l'ouverture, soit une pression moyenne indiquée de cet ordre (supposé).
constexpr double kExhaustLossReferenceBar = 12.0;

/** Les orifices en série : la jupe de la levée, puis la gorge du siège. 1/A² = 1/A₁² + 1/A₂². */
double valveAreaMm2(double headMm, double liftMm) {
    const double curtain = kPi * headMm * liftMm * kMeanLift;
    const double throat = kThroatPerHead * headMm * headMm;
    return kDischarge / std::sqrt(1.0 / (curtain * curtain) + 1.0 / (throat * throat));
}

/** Une paire de soupapes dans un quart d'alésage : la tête touche le bord d'un côté, le pont de l'autre. */
double quarterValveMm(double boreMm, double reach) {
    return (boreMm / 2.0 - kEdgeMm - reach * kBridgeFourValvesMm) / (reach + 0.5);
}

}  // namespace

ValveTrainGeometry valveTrainOf(Camshaft camshaft, double boreMm) {
    ValveTrainGeometry v;
    const double lift = kLiftPerBore * boreMm;
    if (camshaft == Camshaft::Sohc) {
        v.perCylinder = 2;
        v.intakeMm = (boreMm - 2.0 * kEdgeMm - kBridgeTwoValvesMm) / (1.0 + kExhaustShare);
        v.exhaustMm = kExhaustShare * v.intakeMm;
        v.intakeAreaMm2 = valveAreaMm2(v.intakeMm, lift);
        v.exhaustAreaMm2 = valveAreaMm2(v.exhaustMm, lift);
    } else {
        v.perCylinder = 4;
        v.intakeMm = quarterValveMm(boreMm, kIntakeReach);
        v.exhaustMm = quarterValveMm(boreMm, kExhaustReach);
        v.intakeAreaMm2 = 2.0 * valveAreaMm2(v.intakeMm, lift);
        v.exhaustAreaMm2 = 2.0 * valveAreaMm2(v.exhaustMm, lift);
    }
    return v;
}

double machIndex(double areaMm2, double boreMm, double strokeMm, double rpm, double soundMs) {
    const double piston = kPi / 4.0 * boreMm * boreMm;
    const double pistonSpeed = 2.0 * strokeMm / 1000.0 * rpm / 60.0;
    return piston * pistonSpeed / (areaMm2 * soundMs);
}

double valveFilling(double z) {
    const double x = z / kFillingKnee;
    const double x2 = x * x;
    return 1.0 / std::sqrt(std::sqrt(1.0 + x2 * x2));
}

double exhaustLossBar(double z) {
    if (z <= 0.0) return 0.0;
    const double position = z / kExhaustStep;
    const int last = static_cast<int>(kExhaustLossTable.size()) - 1;
    const int index = position >= static_cast<double>(last) ? last - 1 : static_cast<int>(position);
    const double a = kExhaustLossTable[static_cast<std::size_t>(index)];
    const double b = kExhaustLossTable[static_cast<std::size_t>(index + 1)];
    return a + (b - a) * (position - static_cast<double>(index));
}

double sohcFillingRatio(double boreMm, double strokeMm, double rpm, double intakeSoundMs) {
    const ValveTrainGeometry two = valveTrainOf(Camshaft::Sohc, boreMm);
    const ValveTrainGeometry four = valveTrainOf(Camshaft::Dohc, boreMm);
    return valveFilling(machIndex(two.intakeAreaMm2, boreMm, strokeMm, rpm, intakeSoundMs)) /
           valveFilling(machIndex(four.intakeAreaMm2, boreMm, strokeMm, rpm, intakeSoundMs));
}

double sohcBreathingRatio(double boreMm, double strokeMm, double rpm, double intakeSoundMs) {
    const ValveTrainGeometry two = valveTrainOf(Camshaft::Sohc, boreMm);
    const ValveTrainGeometry four = valveTrainOf(Camshaft::Dohc, boreMm);
    const double lossTwo = exhaustLossBar(machIndex(two.exhaustAreaMm2, boreMm, strokeMm, rpm, kSoundExhaustMs));
    const double lossFour = exhaustLossBar(machIndex(four.exhaustAreaMm2, boreMm, strokeMm, rpm, kSoundExhaustMs));
    const double lost = lossTwo + sohcFrictionDeltaBar(strokeMm, rpm);
    return sohcFillingRatio(boreMm, strokeMm, rpm, intakeSoundMs) * (1.0 - lost / kExhaustLossReferenceBar) / (1.0 - lossFour / kExhaustLossReferenceBar);
}

double sohcTuningRatio(double boreMm) {
    return std::sqrt(valveTrainOf(Camshaft::Sohc, boreMm).intakeAreaMm2 / valveTrainOf(Camshaft::Dohc, boreMm).intakeAreaMm2);
}

double sohcBlowdownStretch(double boreMm) {
    return valveTrainOf(Camshaft::Dohc, boreMm).exhaustAreaMm2 / valveTrainOf(Camshaft::Sohc, boreMm).exhaustAreaMm2;
}

double sohcFrictionDeltaBar(double strokeMm, double rpm) {
    // La came frotte sur son poussoir, et ce frottement vaut C·(1 + 500/N)·(soupapes / cylindre) / course (kPa, course en mm),
    // avec C = 133 en attaque directe : Patton, Nitschke et Heywood, 1989 (de mémoire, non relu). Le ressort total reste le
    // même : deux soupapes deux fois plus lourdes ont un ressort deux fois plus fort. Mais le culbuteur multiplie cette force
    // sur la came de son rapport, et ajoute son axe : 50 % de plus (leur C = 400 pour un culbuteur, sur deux soupapes).
    // Un arbre de moins ôte ses paliers : moins de 0,01 bar par la loi de Petroff, négligé.
    constexpr double kFollowerDohcKpa = 133.0 * 4.0;
    constexpr double kRockerExtra = 0.5;
    const double speed = 1.0 + 500.0 / std::fmax(rpm, 300.0);
    return kRockerExtra * kFollowerDohcKpa * speed / strokeMm / 100.0;
}

double sohcInertiaDelta(const EngineConfig& e) {
    // Une roue d'acier de deux fois le rayon du pignon de vilebrequin, de 8 mm d'épaisseur, qui tourne à demi-vitesse.
    // Un arbre par culasse : une seule sur un VR, qui couvre ses deux rangées (voir architecture.hpp).
    const double banks = e.layout == Layout::Inline ? 1.0 : (e.layout == Layout::Vr ? static_cast<double>(architectureOf(e).heads) : 2.0);
    const double radius = 0.56 * static_cast<double>(e.boreMm) / 1000.0;
    const double r2 = radius * radius;
    const double wheel = 0.5 * 7800.0 * kPi * r2 * r2 * 0.008;
    return -banks * wheel * 0.25;
}

}  // namespace revforge
