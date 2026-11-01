#include "revforge/turbo.hpp"

#include <cmath>

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAtmosphere = 101325.0;
constexpr double kAmbientK = 293.0;
constexpr double kAirR = 287.0;
constexpr double kAirCp = 1005.0;
constexpr double kGasCp = 1150.0;

// ── Le compresseur : une ligne de vitesse réaliste, à la manière de Moore et Greitzer ──
// Hauteur isentropique rapportée à U², selon le débit réduit x = ṁ/(ρ·φ·U·D²). Sommet à x = 1 : la limite de pompage.
// À gauche la courbe retombe, et c'est cette pente positive qui rend le pompage possible ; à droite, elle plonge vers le blocage.
constexpr double kHeadPeak = 0.56;
constexpr double kHeadShutOff = 0.40;
constexpr double kReverseGrowth = 0.5;
constexpr double kFlowCoefficient = 0.065;
constexpr double kChokeX = 3.2;
constexpr double kHoldX = 0.9;
// Vitesse de bout de pale qu'une roue en aluminium tient sans casser : un ordre de grandeur (m/s).
constexpr double kTipLimit = 520.0;
constexpr double kReverseX = -2.0;
// Le point nominal : 2,4 fois le débit de pompage, encore loin du blocage. Une ligne de plein gaz tient alors sur la carte.
constexpr double kRatedX = 2.4;
constexpr double kCompressorEta = 0.74;
// Ni vent ni débit : la roue brasse quand même de l'air (coefficient de pertes supposé).
constexpr double kWindage = 0.003;
// Sous cette vitesse de bout de pale, la roue n'est plus qu'un obstacle dans le conduit.
constexpr double kMinTipSpeed = 15.0;

// ── La turbine ──
constexpr double kTurbineEta = 0.68;
constexpr double kBestSpeedRatio = 0.65;
// Ce que les bouffées ajoutent à la pression moyenne, à bas régime (supposé) : de l'ordre de ce qu'on mesure dans un collecteur.
constexpr double kPulsePa = 0.25e5;
constexpr double kExhaustHeatShare = 0.32;
constexpr double kPetrolHeatValue = 44e6;
constexpr double kDieselHeatValue = 42.5e6;
constexpr double kPetrolFuelPerAir = 1.0 / 14.7;
constexpr double kDieselFuelPerAirAtFull = 1.0 / 19.0;
// Le gaz réchauffe les parois du collecteur avant de donner toute son énergie : c'est aussi ça, le retard (constantes supposées).
constexpr double kHeatUpS = 0.35;
constexpr double kCoolDownS = 0.9;
// Au point nominal, la moitié du gaz passe par la turbine : une petite turbine donne du couple tôt, et la soupape jette le reste.
// Un diesel n'a que peu d'énergie dans ses gaz : presque tout passe par elle.
constexpr double kRatedTurbineShare = 0.5;
constexpr double kRatedTurbineShareDiesel = 0.7;

// ── L'échappement et le moteur ──
constexpr double kVolumetric = 0.92;
constexpr double kRatedLineBar = 0.2;
constexpr double kRatedCoolerBar = 0.12;
constexpr double kRatedFilterPa = 3000.0;
constexpr double kDuctLossAtRated = 1e5 * 0.2;
constexpr double kDuctDampedShare = 0.25;

// ── L'échangeur ──
constexpr double kCoolerEffectLow = 0.86;
constexpr double kCoolerEffectFall = 0.14;
constexpr double kOutletS = 0.08;
constexpr double kCoolerMassS = 1.5;
constexpr double kCoolerRestS = 8.0;

// ── Les soupapes ──
constexpr double kWastegateBand = 0.12;
constexpr double kWastegateS = 0.05;
constexpr double kWastegateLeadS = 0.12;
constexpr double kBlowOffS = 0.012;
// Sous le piston la pression, au-dessus le vide du collecteur : elle n'ouvre que papillon fermé, et il faut de la pression à vider.
constexpr double kBlowOffVacuumPa = 80000.0;
constexpr double kBlowOffVacuumSpanPa = 15000.0;
constexpr double kBlowOffOpensAbovePa = 15000.0;
constexpr double kBlowOffAreaAtRated = 2.2e-3;  // m² par kg/s de débit nominal : une soupape de 25 mm pour 2 L
constexpr double kBlowOffDischarge = 0.65;

// Rapports des tailles : le diamètre, d'où découlent la section de la turbine (D²) et l'inertie (D⁵).
constexpr double kSizeSmall = 0.85;
constexpr double kSizeLarge = 1.2;
// La mesure : rotor de 1,844e-5 + roue de 8,786e-6 kg·m² sur un 2,0 L (Chung et Lee, 2016). Son diamètre n'y est pas donné :
// on suppose celui que ce calcul donne à un 2,0 L de 1,3 bar.
constexpr double kReferenceInertia = 2.7226e-5;
constexpr double kReferenceWheel = 0.0485;
constexpr double kReferenceFrictionW = 800.0;
// Le plus petit compresseur de série : sous ce diamètre, on n'en fabrique pas pour une voiture (ordre de grandeur).
constexpr double kSmallestWheel = 0.034;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }
double squared(double v) { return v * v; }

/** x^(2/7), pour le rapport de pression : 1/4 + 1/32 + 1/256 + 1/2048. Fidèle à 1e-4 près de 1 à 5, et rien que des racines. */
double powerTwoSevenths(double x) {
    const double a = std::sqrt(std::sqrt(x));
    const double b = std::sqrt(std::sqrt(std::sqrt(a)));
    const double c = std::sqrt(std::sqrt(std::sqrt(b)));
    const double d = std::sqrt(std::sqrt(std::sqrt(c)));
    return a * b * c * d;
}

/** x^(7/2). */
double powerSevenHalves(double x) { return x * x * x * std::sqrt(x); }

double headCoefficient(double x) {
    if (x >= 1.0) {
        const double over = (x - 1.0) / (kChokeX - 1.0);
        return kHeadPeak * (1.0 - over * over * over);
    }
    if (x >= 0.0) return kHeadShutOff + (kHeadPeak - kHeadShutOff) * x * (2.0 - x);
    return kHeadShutOff + kReverseGrowth * x * x;
}

/** Le rendement chute des deux côtés du point de meilleur rendement. */
double compressorEta(double x) {
    const double off = (x - 2.0) / 1.7;
    return clamp(kCompressorEta * (1.0 - 0.45 * off * off), 0.3, kCompressorEta);
}

/** Hauteur isentropique (J/kg) pour ce débit et cette vitesse de bout de pale. Négative : la roue n'est plus qu'une perte. */
double compressorHead(const TurboSpec& spec, double flow, double tipSpeed, double* xOut = nullptr) {
    const double D2 = spec.wheelDiameterM * spec.wheelDiameterM;
    const double rho = kAtmosphere / (kAirR * kAmbientK);
    const double u = tipSpeed < kMinTipSpeed ? kMinTipSpeed : tipSpeed;
    const double unit = rho * kFlowCoefficient * u * D2;
    const double x = flow / unit;
    if (xOut != nullptr) *xOut = x;
    const double area = 0.7854 * D2;
    const double dynamic = 0.5 * squared(flow / (rho * area));
    if (x >= kChokeX) {
        const double atChoke = 0.5 * squared(kChokeX * unit / (rho * area));
        return -(dynamic - atChoke) * 2.0;
    }
    if (x <= kReverseX) {
        const double atLimit = 0.5 * squared(kReverseX * unit / (rho * area));
        return headCoefficient(kReverseX) * tipSpeed * tipSpeed + (dynamic - atLimit) * 2.0;
    }
    // La vitesse minimale ne sert qu'à définir le débit réduit : un arbre arrêté ne donne aucune pression.
    return headCoefficient(x) * tipSpeed * tipSpeed;
}

double pressureRatio(double head) {
    const double base = 1.0 + head / (kAirCp * kAmbientK);
    return powerSevenHalves(base < 0.5 ? 0.5 : base);
}

double fuelPerAir(const TurboSpec& spec, double fill, bool burning) {
    if (!burning) return 0.0;
    return spec.diesel ? kDieselFuelPerAirAtFull * clamp(fill, 0.04, 1.0) : kPetrolFuelPerAir;
}

/** Ce que le gaz emporte d'énergie : d'autant plus que le moteur tourne vite et chargé, moins il a le temps de la céder aux parois. */
double exhaustTemperature(const TurboSpec& spec, double intakeK, double rpm, double fill, double fuel) {
    if (fuel <= 0.0) return intakeK + 60.0;
    const double speed = clamp(rpm / spec.redlineRpm, 0.0, 1.0);
    const double load = spec.diesel ? 1.0 : 0.6 + 0.4 * clamp(fill, 0.0, 1.0);
    const double share = kExhaustHeatShare * (0.85 + 0.25 * speed) * load;
    const double value = spec.diesel ? kDieselHeatValue : kPetrolHeatValue;
    return intakeK + share * value * fuel / (1.0 + fuel) / kGasCp;
}

double coolerOutlet(double hotK, double flow, double rated) {
    const double share = clamp(flow / rated, 0.0, 1.5);
    const double effect = kCoolerEffectLow - kCoolerEffectFall * share;
    return hotK - effect * (hotK - kAmbientK);
}

/**
 * Puissance sur l'arbre (W), pour ce débit, cette détente et cette vitesse. Le gaz n'arrive pas à débit moyen : chaque
 * bouffée pousse plus fort que la moyenne, d'autant plus que le moteur tourne doucement, et c'est ce qui fait tourner
 * l'arbre au ralenti. `pulsePa` ajoute cet écart à la détente moyenne.
 */
double turbinePower(const TurboSpec& spec, double flow, double inletK, double inPa, double outPa, double omega, double pulsePa) {
    if (flow <= 0.0) return 0.0;
    const double ratio = outPa / (inPa + pulsePa);
    if (ratio >= 1.0) return 0.0;
    // γ = 4/3 : l'exposant vaut exactement 1/4.
    const double expansion = 1.0 - std::sqrt(std::sqrt(ratio));
    const double enthalpy = kGasCp * inletK * expansion;
    const double jet = std::sqrt(2.0 * enthalpy);
    double y = omega * spec.turbineRadiusM / (jet * kBestSpeedRatio);
    if (y < 0.05) y = 0.05;
    if (y >= 2.0) return 0.0;
    return flow * enthalpy * kTurbineEta * y * (2.0 - y);
}

double bearingPower(const TurboSpec& spec, double omega) {
    const double share = omega / spec.referenceOmega;
    return spec.frictionW * share * share;
}

double windagePower(const TurboSpec& spec, double tipSpeed) {
    const double rho = kAtmosphere / (kAirR * kAmbientK);
    return kWindage * rho * tipSpeed * tipSpeed * tipSpeed * spec.wheelDiameterM * spec.wheelDiameterM;
}

}  // namespace

// ── Le dimensionnement ──

TurboSpec turboSpecFor(const EngineConfig& e, double breathingAtRated) {
    TurboSpec s;
    s.diesel = e.fuel == Fuel::Diesel;
    s.displacementM3 = static_cast<double>(e.displacementCc) * 1e-6;
    s.targetBar = static_cast<double>(e.boostBar);
    s.redlineRpm = static_cast<double>(e.redlineRpm);
    s.blowOffValve = e.turboRelease == TurboRelease::DumpValve;

    // Le point nominal : à 88 % du régime maximal, à la pression demandée.
    const double rated = 0.88 * s.redlineRpm;
    const double chargePa = kAtmosphere + s.targetBar * 1e5;
    const double ratio = chargePa / kAtmosphere;
    const double head = kAirCp * kAmbientK * (powerTwoSevenths(ratio) - 1.0);
    const double outK = kAmbientK + head / kCompressorEta / kAirCp;
    const double coolK = kAmbientK + (1.0 - (kCoolerEffectLow - kCoolerEffectFall)) * (outK - kAmbientK);
    const double chargeRho = chargePa / (kAirR * coolK);
    const double flow = kVolumetric * breathingAtRated * chargeRho * s.displacementM3 * rated / 120.0;
    s.ratedFlow = flow;

    const double tipSpeed = std::sqrt(head / headCoefficient(kRatedX));
    const double atmosphereRho = kAtmosphere / (kAirR * kAmbientK);
    const double nominal = std::sqrt(flow / (kRatedX * kFlowCoefficient * atmosphereRho * tipSpeed));
    const double size = e.turboSize == TurboSize::Small ? kSizeSmall : (e.turboSize == TurboSize::Large ? kSizeLarge : 1.0);
    s.wheelDiameterM = clamp(nominal * size, kSmallestWheel, 0.12);
    s.turbineRadiusM = 0.82 * 0.5 * s.wheelDiameterM;
    const double share = s.wheelDiameterM / kReferenceWheel;
    s.inertia = kReferenceInertia * share * share * share * share * share;
    s.frictionW = kReferenceFrictionW * share * share;
    // La vitesse de l'arbre au point nominal, pour un turbo de la bonne taille.
    s.referenceOmega = tipSpeed / (0.5 * nominal);

    const double area = 0.7854 * 0.055 * 0.055 * s.displacementM3 / 0.002;
    s.chargeVolumeM3 = clamp(1.5e-3 + 2.5 * s.displacementM3, 2.5e-3, 14e-3);
    s.ductInertance = 1.2 / area;
    s.blowOffArea = kBlowOffAreaAtRated * flow;
    s.filterDrop = kRatedFilterPa / (flow * flow);
    s.coolerDrop = kRatedCoolerBar * 1e5 * chargeRho / (flow * flow);

    // La turbine : ce qu'il faut de détente pour tenir ce point, trois quarts du gaz passant par elle.
    const double fuel = fuelPerAir(s, 1.0, true);
    const double gas = flow * (1.0 + fuel);
    const double hotK = exhaustTemperature(s, coolK, rated, 1.0, fuel);
    double lineFactor = 1.0;
    switch (e.exhaust) {
        case Exhaust::Stock: lineFactor = 1.0; break;
        case Exhaust::Sport: lineFactor = 0.6; break;
        case Exhaust::Straight: lineFactor = 0.25; break;
        case Exhaust::MufflerDelete: lineFactor = 0.4; break;
        case Exhaust::ResonatorDelete: lineFactor = 0.9; break;
        case Exhaust::Quiet: lineFactor = 1.3; break;
    }
    s.lineDrop = kRatedLineBar * lineFactor * 1e5 / (gas * gas);
    const double outPa = kAtmosphere + kRatedLineBar * 1e5;
    const double through = (s.diesel ? kRatedTurbineShareDiesel : kRatedTurbineShare) * gas;
    const double needed = flow * head / kCompressorEta + kReferenceFrictionW;
    // Rendement au point de meilleure vitesse réduite : la turbine est dessinée pour ce point.
    const double expansion = clamp(needed / (through * kGasCp * hotK * kTurbineEta), 0.02, 0.6);
    const double pressureDrop = squared(squared(1.0 - expansion));  // p4/p3 = (1 - Δ)⁴
    const double inPa = outPa / pressureDrop;
    const double law = std::sqrt(1.0 - pressureDrop * pressureDrop);
    const double nominalK = through * std::sqrt(hotK) / (inPa * law);
    s.turbineK = nominalK * size * size;
    s.ratedBackPa = inPa > chargePa ? inPa - chargePa : 0.0;
    s.wastegateK = 2.0 * s.turbineK;
    s.tipLimit = 1.1 * tipSpeed > kTipLimit ? 1.1 * tipSpeed : kTipLimit;
    return s;
}

// ── L'équilibre ──

namespace {

struct Balance {
    double surplusW = 0.0;
    double omega = 0.0;
    double turbineInPa = kAtmosphere;
    double flow = 0.0;
};

/** La vitesse de bout de pale qu'il faut pour cette hauteur, à ce débit. La hauteur croît avec U : une dichotomie suffit. */
double tipSpeedFor(const TurboSpec& spec, double flow, double head) {
    double low = kMinTipSpeed;
    double high = 900.0;
    if (head <= 0.0) return low;
    for (int i = 0; i < 14; ++i) {
        const double middle = 0.5 * (low + high);
        (compressorHead(spec, flow, middle) >= head ? high : low) = middle;
    }
    return high;
}

/** Ce qu'il reste sur l'arbre si la pression avant le papillon vaut `chargePa` à ce régime, soupape de décharge ouverte à `gate`. */
Balance balanceAt(const TurboSpec& spec, double rpm, double breathing, double chargePa, double gate) {
    Balance b;
    // Point fixe sur la température : l'air chauffe avec la compression, et sa densité en dépend. Deux passes suffisent.
    double coolK = kAmbientK;
    double flow = 0.0;
    double inletPa = kAtmosphere;
    double outletPa = chargePa;
    double head = 0.0;
    double tip = kMinTipSpeed;
    double x = 0.0;
    for (int pass = 0; pass < 2; ++pass) {
        const double rho = chargePa / (kAirR * coolK);
        flow = kVolumetric * breathing * rho * spec.displacementM3 * rpm / 120.0;
        inletPa = kAtmosphere - spec.filterDrop * flow * flow;
        // Mêmes pertes qu'à chaque pas : celles de l'échangeur, et celles du conduit, plafonnées.
        outletPa = chargePa + spec.coolerDrop * flow * flow / rho + kDuctLossAtRated / spec.ratedFlow * (flow < kDuctDampedShare * spec.ratedFlow ? flow : kDuctDampedShare * spec.ratedFlow);
        head = kAirCp * kAmbientK * (powerTwoSevenths(outletPa / inletPa) - 1.0);
        tip = tipSpeedFor(spec, flow, head);
        compressorHead(spec, flow, tip, &x);
        const double outK = kAmbientK + (head > 0.0 ? head : 0.0) / compressorEta(x) / kAirCp;
        coolK = coolerOutlet(outK, flow, spec.ratedFlow);
    }
    const double fuel = fuelPerAir(spec, 1.0, true);
    const double gas = flow * (1.0 + fuel);
    const double hotK = exhaustTemperature(spec, coolK, rpm, 1.0, fuel);
    const double outPa = kAtmosphere + spec.lineDrop * gas * gas;
    const double passage = spec.turbineK + spec.wastegateK * gate;
    const double inPa = std::sqrt(outPa * outPa + gas * gas * hotK / (passage * passage));
    const double omega = tip / (0.5 * spec.wheelDiameterM);
    const double through = gas * spec.turbineK / passage;
    const double power = turbinePower(spec, through, hotK, inPa, outPa, omega, kPulsePa * (1.0 - clamp(rpm / spec.redlineRpm, 0.0, 1.0)));
    const double compressor = (head > 0.0 ? flow * head / compressorEta(x) : 0.0) + windagePower(spec, tip);
    b.surplusW = power - compressor - bearingPower(spec, omega);
    // Sous la limite de pompage, le compresseur ne tient pas cette pression, quelle que soit l'énergie disponible.
    if (x < kHoldX) b.surplusW = -1.0;
    // Ni au-delà de la vitesse que supportent les roues : le calculateur ouvre la soupape pour les protéger.
    if (tip > spec.tipLimit) b.surplusW = -1.0;
    b.omega = omega;
    b.turbineInPa = inPa;
    b.flow = flow;
    return b;
}

}  // namespace

TurboSteady turboSteady(const TurboSpec& spec, double rpm, double breathing) {
    TurboSteady out;
    const double target = kAtmosphere + spec.targetBar * 1e5;
    // Le régime n'y fait rien : sans gaz, pas de pression.
    if (rpm <= 0.0 || breathing <= 0.0) return out;
    const Balance atTarget = balanceAt(spec, rpm, breathing, target, 0.0);
    double chargePa = target;
    double gate = 0.0;
    Balance found = atTarget;
    if (atTarget.surplusW >= 0.0) {
        // Assez d'énergie pour la pression demandée : la soupape de décharge rejette le surplus.
        double low = 0.0;
        double high = 1.0;
        if (balanceAt(spec, rpm, breathing, target, 1.0).surplusW > 0.0) {
            gate = 1.0;
        } else {
            for (int i = 0; i < 10; ++i) {
                const double middle = 0.5 * (low + high);
                (balanceAt(spec, rpm, breathing, target, middle).surplusW > 0.0 ? low : high) = middle;
            }
            gate = 0.5 * (low + high);
        }
        found = balanceAt(spec, rpm, breathing, target, gate);
    } else {
        // Sinon, l'arbre s'équilibre plus bas. Sans rien à l'atmosphère, il reste à l'arrêt ou presque.
        double low = kAtmosphere;
        double high = target;
        found = balanceAt(spec, rpm, breathing, low, 0.0);
        if (found.surplusW <= 0.0) {
            chargePa = kAtmosphere;
        } else {
            for (int i = 0; i < 12; ++i) {
                const double middle = 0.5 * (low + high);
                (balanceAt(spec, rpm, breathing, middle, 0.0).surplusW > 0.0 ? low : high) = middle;
            }
            chargePa = 0.5 * (low + high);
            found = balanceAt(spec, rpm, breathing, chargePa, 0.0);
        }
    }
    out.chargePa = chargePa;
    out.boost = clamp((chargePa - kAtmosphere) / (spec.targetBar * 1e5), 0.0, 1.0);
    out.turbineInPa = found.turbineInPa;
    out.shaftRpm = found.omega * 60.0 / (2.0 * kPi);
    out.wastegate = gate;
    out.flow = found.flow;
    return out;
}

double turboBackPressureNm(const TurboSpec& spec, double chargePa, double turbineInPa, double flow, double fill) {
    // Le gaz qu'il faut pousser contre la turbine, moins celui que la pression d'admission pousse déjà sur le piston.
    // Les couples de référence viennent de vrais moteurs, bien appariés : ils comptent déjà ce que coûte un turbo de la bonne
    // taille à ce débit. Seul l'écart compte : un petit turbo étouffe, un gros soulage.
    const double share = flow / spec.ratedFlow;
    const double difference = clamp(turbineInPa - chargePa - spec.ratedBackPa * share * share, -5e4, 4e5);
    return difference * spec.displacementM3 / (4.0 * kPi) * clamp(fill, 0.0, 1.0);
}

// ── Le pas de temps ──

void TurboCharger::setSpec(const TurboSpec& spec) {
    spec_ = spec;
    state_.shaftShare = state_.shaftOmega / spec_.referenceOmega;
}

double TurboCharger::boost() const { return (state_.chargePa - kAtmosphere) / (spec_.targetBar * 1e5); }

double TurboCharger::backPressureNm(double fill) const { return turboBackPressureNm(spec_, state_.chargePa, state_.turbineInPa, state_.engineFlow, fill); }

TurboDrive TurboCharger::drive() const {
    TurboDrive d;
    const double rated = spec_.ratedFlow;
    d.shaftHz = static_cast<float>(state_.shaftOmega / (2.0 * kPi));
    d.shaftShare = static_cast<float>(state_.shaftShare);
    d.flowShare = static_cast<float>(state_.compressorFlow > 0.0 ? state_.compressorFlow / rated : 0.0);
    // Ce qui s'entend, c'est le débit et la pression qu'il perd : puissance du jet en ṁ·Δp, amplitude en racine de la pression.
    const double gauge = (state_.chargePa - kAtmosphere) / (spec_.targetBar * 1e5);
    const double pressure = std::sqrt(gauge > 0.0 ? gauge : 0.0);
    d.blowOffShare = static_cast<float>(state_.blowOffFlow / rated * pressure);
    d.backflowShare = static_cast<float>((state_.compressorFlow < 0.0 ? -state_.compressorFlow / rated : 0.0) * pressure);
    d.wastegate = static_cast<float>(state_.wastegate);
    const double passage = spec_.turbineK + spec_.wastegateK * state_.wastegate;
    d.wastegateFlowShare = static_cast<float>(state_.engineFlow / rated * spec_.wastegateK * state_.wastegate / passage);
    return d;
}

void TurboCharger::step(double dt, const TurboInput& input) {
    TurboState& s = state_;
    const TurboSpec& spec = spec_;
    const double rpm = input.rpm > 0.0 ? input.rpm : 0.0;
    const double fill = clamp(input.fill, 0.0, 1.0);

    // ── L'air : du compresseur au papillon ──
    const double tip = s.shaftOmega * 0.5 * spec.wheelDiameterM;
    const double flowNow = s.compressorFlow;
    s.inletPa = kAtmosphere - spec.filterDrop * flowNow * flowNow;
    const double coolK = s.coolerOutK;
    const double chargeRho = s.chargePa / (kAirR * coolK);
    s.outletPa = s.chargePa + spec.coolerDrop * flowNow * (flowNow < 0.0 ? -flowNow : flowNow) / chargeRho;

    // La colonne d'air du conduit : la différence de pression l'accélère. Implicite sur la pente descendante, seule raide.
    double x = 0.0;
    const double headNow = compressorHead(spec, flowNow, tip, &x);
    const double delta = 1e-4 + 1e-3 * (flowNow < 0.0 ? -flowNow : flowNow);
    const double headNext = compressorHead(spec, flowNow + delta, tip);
    const double supplied = s.inletPa * pressureRatio(headNow);
    double slope = s.inletPa * (pressureRatio(headNext) - pressureRatio(headNow)) / delta;
    if (slope > 0.0) slope = 0.0;
    // Les pertes du conduit à faible débit : sans elles, le moindre filet d'air pomperait dès que le débit passe sous la limite.
    const double damped = clamp(flowNow, -kDuctDampedShare * spec.ratedFlow, kDuctDampedShare * spec.ratedFlow);
    const double ductLoss = kDuctLossAtRated / spec.ratedFlow * damped;
    const double lossSlope = flowNow > -kDuctDampedShare * spec.ratedFlow && flowNow < kDuctDampedShare * spec.ratedFlow ? kDuctLossAtRated / spec.ratedFlow : 0.0;
    const double rate = dt / spec.ductInertance;
    const double flowNext = flowNow + rate * (supplied - s.outletPa - ductLoss) / (1.0 + rate * (lossSlope - slope));
    s.compressorFlow = flowNext;

    // Après le papillon : le remplissage dit la part de la pression qui passe. Un diesel n'a pas de papillon.
    s.plenumPa = spec.diesel ? s.chargePa : s.chargePa * fill;
    const double plenumRho = s.plenumPa / (kAirR * coolK);
    const double engine = kVolumetric * input.breathing * plenumRho * spec.displacementM3 * rpm / 120.0;
    s.engineFlow = engine;

    // La soupape de décharge : le vide du collecteur l'ouvre, et elle reste ouverte tant qu'il y a quelque chose à vider.
    double wantedValve = 0.0;
    if (spec.blowOffValve && !spec.diesel) {
        const double vacuum = clamp((kBlowOffVacuumPa - s.plenumPa) / kBlowOffVacuumSpanPa, 0.0, 1.0);
        const double gauge = s.chargePa - kAtmosphere;
        if (vacuum > 0.0 && gauge > kBlowOffOpensAbovePa) valveHeld_ = true;
        if (vacuum <= 0.0 || (s.blowOff > 0.5 && s.blowOffFlow < 0.05 * spec.ratedFlow)) valveHeld_ = false;
        if (valveHeld_) wantedValve = vacuum;
    }
    s.blowOff += (wantedValve - s.blowOff) * (dt / kBlowOffS < 1.0 ? dt / kBlowOffS : 1.0);
    double valveFlow = 0.0;
    const double across = s.chargePa - kAtmosphere;
    if (s.blowOff > 1e-4 && across > 0.0) {
        // Plus de 1,9 fois la pression d'aval, le col serait sonique : le débit suit encore p, pas √Δp. Ici, une simple loi d'orifice régularisée.
        valveFlow = kBlowOffDischarge * spec.blowOffArea * s.blowOff * std::sqrt(2.0 * chargeRho) * across / std::sqrt(across + 500.0);
    }
    s.blowOffFlow = valveFlow;

    // Le volume entre compresseur et papillon : c'est lui qui fait que la pression n'arrive pas d'un coup.
    const double throughThrottle = engine;
    const double gain = kAirR * coolK / spec.chargeVolumeM3;
    const double chargeBefore = s.chargePa;
    s.chargePa += gain * (flowNext - throughThrottle - valveFlow) * dt;
    if (s.chargePa < 0.5 * kAtmosphere) s.chargePa = 0.5 * kAtmosphere;

    // ── Les températures ──
    const double headNew = compressorHead(spec, flowNext, tip, &x);
    const double eta = compressorEta(x);
    // Le carter a sa masse : la sortie ne suit pas chaque aller-retour du pompage.
    const double outTarget = flowNext > 0.0 ? kAmbientK + (headNew > 0.0 ? headNew : 0.0) / eta / kAirCp : s.compressorOutK;
    s.compressorOutK += (outTarget - s.compressorOutK) * dt / kOutletS;
    const double outK = s.compressorOutK;
    if (flowNext > 0.02 * spec.ratedFlow) {
        s.coolerOutK += (coolerOutlet(outK, flowNext, spec.ratedFlow) - s.coolerOutK) * dt / kCoolerMassS;
    } else {
        s.coolerOutK += (kAmbientK - s.coolerOutK) * dt / kCoolerRestS;
    }

    // ── L'échappement : ce que le moteur a avalé, plus le carburant, à la température de la combustion ──
    const double fuel = fuelPerAir(spec, fill, input.burning);
    const double gas = engine * (1.0 + fuel);
    const double hotTarget = rpm > 50.0 ? exhaustTemperature(spec, coolK, rpm, fill, fuel) : kAmbientK;
    const double hotTau = hotTarget > s.turbineInK ? kHeatUpS : kCoolDownS;
    s.turbineInK += (hotTarget - s.turbineInK) * dt / hotTau;

    // La soupape de décharge de la turbine : un ressort et une membrane, qui s'ouvrent à la pression demandée.
    // Le calculateur anticipe : la pression qui monte vite ouvre plus tôt. Sans cela, l'arbre et la soupape se renvoient la balle.
    const double anticipated = s.chargePa + kWastegateLeadS * (s.chargePa - chargeBefore) / dt;
    const double overBar = (anticipated - (kAtmosphere + (1.0 - 0.2 * kWastegateBand) * spec.targetBar * 1e5)) / (kWastegateBand * spec.targetBar * 1e5);
    // Le calculateur ouvre aussi pour protéger les roues : un petit turbo à bout de débit y arrive avant la pression demandée.
    const double overSpeed = (tip - spec.tipLimit) / (0.02 * spec.tipLimit);
    const double wantedGate = clamp(overBar > overSpeed ? overBar : overSpeed, 0.0, 1.0);
    s.wastegate += (wantedGate - s.wastegate) * dt / kWastegateS;

    const double passage = spec.turbineK + spec.wastegateK * s.wastegate;
    const double outPa = kAtmosphere + spec.lineDrop * gas * gas;
    s.turbineInPa = std::sqrt(outPa * outPa + gas * gas * s.turbineInK / (passage * passage));
    const double through = gas * spec.turbineK / passage;

    // ── L'arbre : tout se joue ici ──
    const double pulse = input.burning ? kPulsePa * (spec.diesel ? 0.5 + 0.5 * fill : fill) * (1.0 - clamp(rpm / spec.redlineRpm, 0.0, 1.0)) : 0.0;
    const double power = turbinePower(spec, through, s.turbineInK, s.turbineInPa, outPa, s.shaftOmega, pulse);
    const double compressor = (flowNext > 0.0 && headNew > 0.0 ? flowNext * headNew / eta : 0.0) + windagePower(spec, tip);
    s.turbinePowerW = power;
    s.compressorPowerW = compressor;
    omegaSquared_ += 2.0 * (power - compressor - bearingPower(spec, s.shaftOmega)) / spec.inertia * dt;
    if (omegaSquared_ < 0.0) omegaSquared_ = 0.0;
    s.shaftOmega = std::sqrt(omegaSquared_);
    s.shaftShare = s.shaftOmega / spec.referenceOmega;
}

}  // namespace revforge
