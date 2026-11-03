#include "revforge/particulate_filter.hpp"

#include <algorithm>
#include <cmath>

#include "portable_math.hpp"

namespace revforge {

namespace {

constexpr double kGasR = 287.0;
constexpr double kExhaustCp = 1100.0;
constexpr double kExhaustPa = 102000.0;
// Le gaz chaud de la ligne, tel que le voit ExhaustLine : la résistance acoustique se rapporte à son ρc/S.
constexpr double kLineDensity = 0.6;
constexpr double kLineSoundMs = 500.0;
constexpr double kLineK = 650.0;
// Le gâteau de suie : densité tassée et perméabilité (Konstandopoulos et al. : de 50 à 150 kg/m³, de 5e-15 à 1e-13 m²).
constexpr double kSootDensity = 100.0;
constexpr double kSootPermeabilityM2 = 3e-14;
// Une essence à filtre (GPF) : parois plus minces et plus ouvertes, pour une contre-pression moindre (supposé).
constexpr double kPetrolWallM = 2.0e-4;
constexpr double kPetrolPermeabilityM2 = 3e-12;
// L'entrée des canaux d'entrée : un tiers de la section frontale, une hauteur dynamique perdue.
constexpr double kInletOpenShare = 0.3;
constexpr double kInletLoss = 1.0;
// La suie brûle dans l'oxygène, loi d'Arrhenius : 150 kJ/mol, et la moitié d'un plein en 10 min à 620 °C sous λ = 2.
constexpr double kSootActivationJPerMol = 150000.0;
constexpr double kMolarR = 8.3143;
constexpr double kSootRateAt893 = 1.0 / 300.0;
constexpr double kSootHeatJPerG = 32800.0;
// La post-injection vise 620 °C à l'entrée ; le catalyseur d'oxydation en brûle 95 %.
constexpr double kRegenK = 893.0;
constexpr double kOxidationShare = 0.95;
constexpr double kFuelHeatJPerG = 43000.0;
// Part de l'énergie du carburant qui part à l'échappement, après la turbine s'il y en a une (Heywood : de 30 à 40 % avant).
constexpr double kExhaustHeatDiesel = 0.28;
constexpr double kExhaustHeatPetrol = 0.32;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

double wallTerm(const ParticulateFilterSpec& s, double sootG) {
    // Darcy : Δp = μ·u·(w/k_paroi + e/k_suie), e l'épaisseur du gâteau.
    const double cake = sootG * 1e-3 / (kSootDensity * s.filtrationM2);
    return s.wallM / s.wallPermeabilityM2 + cake / kSootPermeabilityM2;
}

}  // namespace

ParticulateFilterSpec particulateFilterSpecOf(const EngineConfig& e) {
    ParticulateFilterSpec s;
    s.diesel = e.burnsDiesel();
    s.displacementM3 = e.sweptCcPerTwoTurns() * 1e-6;
    s.volumeL = 1.25 * e.sweptCcPerTwoTurns() / 1000.0;
    s.filtrationM2 = 0.8 * s.volumeL;
    // Un monolithe de 15 cm de long : 144 mm de diamètre pour 2,5 L, la taille courante derrière un 2 L.
    s.frontM2 = s.volumeL * 1e-3 / 0.15;
    if (!s.diesel) {
        s.wallM = kPetrolWallM;
        s.wallPermeabilityM2 = kPetrolPermeabilityM2;
    }
    s.heatJPerK = 1000.0 * s.volumeL;
    return s;
}

double exhaustViscosity(double kelvin) {
    const double k = std::max(kelvin, 200.0);
    return 1.716e-5 * (k / 273.15) * std::sqrt(k / 273.15) * (273.15 + 110.4) / (k + 110.4);
}

double filterBackPressurePa(const ParticulateFilterSpec& s, double sootG, double exhaustKgS, double exhaustK) {
    const double density = kExhaustPa / (kGasR * std::max(exhaustK, 250.0));
    const double flow = std::max(exhaustKgS, 0.0) / density;
    const double through = flow / s.filtrationM2;
    const double channel = flow / (kInletOpenShare * s.frontM2);
    return exhaustViscosity(exhaustK) * through * wallTerm(s, sootG) + kInletLoss * 0.5 * density * channel * channel;
}

double filterPumpingNm(const ParticulateFilterSpec& s, double backPressurePa) { return backPressurePa * s.displacementM3 / (4.0 * 3.141592653589793); }

double filterLineResistance(const ParticulateFilterSpec& s, double sootG) {
    // La part linéaire : R = μ·(w/k + e/k_s)/A_filtrante, face à ρc/S du tube de la ligne (50 mm pour 2 L, comme ExhaustLine).
    const double pipeDiameter = std::clamp(0.05 * std::sqrt(s.displacementM3 / 0.002), 0.032, 0.09);
    const double pipeArea = 3.141592653589793 * pipeDiameter * pipeDiameter / 4.0;
    const double resistance = exhaustViscosity(kLineK) * wallTerm(s, sootG) / s.filtrationM2;
    return resistance / (kLineDensity * kLineSoundMs / pipeArea);
}

double sootIndexGPerKg(bool diesel, double lambda, double smokeLambda) {
    if (diesel) return 0.4 + 6.0 * portable::exp(-(lambda - smokeLambda) / 0.15);
    // Une injection directe d'essence : quelques milligrammes par kilomètre, bien plus riche ou à froid.
    return 0.01 + 0.2 * clamp((1.0 - lambda) / 0.12, 0.0, 1.0);
}

double exhaustGasK(bool diesel, double intakeK, double fuelPerAir) {
    const double heat = (diesel ? kExhaustHeatDiesel : kExhaustHeatPetrol) * kFuelHeatJPerG * 1000.0 * fuelPerAir;
    return intakeK + heat / (kExhaustCp * (1.0 + fuelPerAir));
}

void ParticulateFilter::configure(const ParticulateFilterSpec& spec) {
    spec_ = spec;
    // Un filtre en service est en moyenne à mi-chemin entre deux régénérations.
    sootG_ = 0.5 * (spec.startGPerL + spec.endGPerL) * spec.volumeL;
    if (!spec.diesel) sootG_ = 0.1 * spec.startGPerL * spec.volumeL;
    temperatureK_ = 500.0;
    backPressurePa_ = 0.0;
    extraFuelGps_ = 0.0;
    regenerating_ = false;
}

double ParticulateFilter::loadShare() const {
    const double low = spec_.endGPerL * spec_.volumeL;
    const double high = spec_.startGPerL * spec_.volumeL;
    return clamp((sootG_ - low) / (high - low), 0.0, 1.5);
}

void ParticulateFilter::step(const FilterInput& in, double dt) {
    const double flow = std::max(in.exhaustKgS, 0.0);
    // Le calculateur d'un diesel demande la régénération au plein, et la tient jusqu'à ce qu'il soit vide.
    if (spec_.diesel) {
        if (loadGPerL() >= spec_.startGPerL) regenerating_ = true;
        else if (loadGPerL() <= spec_.endGPerL) regenerating_ = false;
    }
    double inletK = in.burning ? in.exhaustK : std::min(in.exhaustK, 400.0);
    extraFuelGps_ = 0.0;
    if (regenerating_ && in.running && in.burning && flow > 0.0) {
        // La post-injection brûle dans le catalyseur d'oxydation et chauffe le gaz jusqu'à la cible.
        const double lift = std::max(kRegenK - in.exhaustK, 0.0);
        extraFuelGps_ = flow * 1000.0 * kExhaustCp * lift / (kFuelHeatJPerG * 1000.0 * kOxidationShare);
        inletK = std::max(in.exhaustK, kRegenK);
    }
    // L'oxygène : l'excès d'air d'un diesel, presque rien à λ = 1 sur un essence, de l'air pur pendant une coupure.
    const double oxygen = !in.burning ? 1.0 : clamp(1.0 - 1.0 / std::max(in.lambda, 1.0), 0.005, 1.0);
    const double rate = kSootRateAt893 * portable::exp(-kSootActivationJPerMol / kMolarR * (1.0 / temperatureK_ - 1.0 / kRegenK)) * oxygen / 0.5;
    const double burnt = std::min(rate * sootG_ * dt, sootG_);
    const double made = in.burning && in.running ? sootIndexGPerKg(spec_.diesel, in.lambda, in.smokeLambda) * in.fuelGps * 1e-3 * dt : 0.0;
    sootG_ = std::max(sootG_ - burnt + made, 0.0);
    // Le monolithe suit le gaz, avec sa masse thermique, et reçoit la chaleur de la suie qui brûle.
    const double exchange = flow * kExhaustCp * (inletK - temperatureK_);
    temperatureK_ += (exchange + burnt / dt * kSootHeatJPerG) / spec_.heatJPerK * dt;
    backPressurePa_ = filterBackPressurePa(spec_, sootG_, flow, std::max(inletK, 300.0));
}

}  // namespace revforge
