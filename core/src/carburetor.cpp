#include "revforge/carburetor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "portable_math.hpp"
#include "revforge/atmosphere.hpp"

namespace revforge {

namespace {

constexpr double kAirKgM3 = 1.2;
constexpr double kSoundMs = 340.0;
constexpr double kFuelKgM3 = 745.0;
constexpr double kStoichiometric = 14.7;

// Dimensionnement, à la puissance maximale (88 % du régime maximal) : d = k √(V × N), en mm, litres et milliers de tr/min.
// Un corps par cylindre : la règle de Weber, k = 20 sur le volume d'un cylindre. Un col partagé par tous : k = 10 sur toute
// la cylindrée, soit 32 mm sur le 2 L à 5500 tr/min d'une BMW 2002 (Solex 40 PDSI). Voir docs/modele.md.
constexpr double kPerCylinderChoke = 20.0;
constexpr double kSharedChoke = 10.0;
constexpr double kRatedShare = 0.88;
// Ce que perd le venturi, en part de la pression dynamique au col : un carburateur a un diffuseur court, un gicleur et
// un papillon dans le passage. Holley annonce ses débits à 1,5 inHg sur un quatre corps (environ 0,7) et à 3 inHg sur un
// double corps (environ 1,2). Valeur supposée entre les deux.
constexpr double kThroatLoss = 0.9;
constexpr double kPipeFriction = 0.03;
constexpr double kPipeOverThroat = 1.25;
// Un cylindre aspire sur 240° de vilebrequin, un peu plus avec l'élan de la colonne d'air : 40 % du cycle.
constexpr double kInductionShare = 0.4;

// Ce qui se dépose dépend de ce que l'air a parcouru avant la soupape : part = 1 − exp(−L/L₀). Longueur supposée.
constexpr double kDepositLengthM = 0.5;
constexpr double kSharedWetPathM = 0.45;
constexpr double kPerCylinderWetPathM = 0.18;
// Gouttes fines à forte vitesse d'air (Nukiyama et Tanasawa : diamètre en 1/v) : elles restent dans le courant.
constexpr double kAtomizeMs = 40.0;
// Le film repart en s'évaporant et en glissant : τ en v^−0,75. 0,35 s à 50 m/s, dans la plage publiée à chaud (voir la doc).
constexpr double kFilmTauS = 0.35;
constexpr double kFilmRefMs = 50.0;
constexpr double kFilmMinMs = 5.0;
// La colonne de carburant dans le puits et l'émulsion : τ = 2L/v, v tiré de la dépression au col par Bernoulli.
constexpr double kFuelColumnM = 0.05;
constexpr double kFuelDischarge = 0.7;
constexpr double kFuelLagMaxS = 0.35;
// La pompe : de 0,7 à 1 cm³ par coup sur un 2 L dans les fiches de réglage, donnée en quelques dixièmes de seconde.
constexpr double kPumpCm3PerLitre = 0.4;
constexpr double kPumpTauS = 0.25;
// Un mouvement lent repart au réservoir par le gicleur de décharge : seul un pied franc fait jaillir (une course en moins de 2 s).
constexpr double kPumpMinRatePerS = 0.5;
constexpr double kPumpRateTauS = 0.1;
// Un jet mal atomisé : il mouille surtout la pipe, et n'arrive au cylindre que par le film.
constexpr double kPumpWallShare = 0.7;

// Le meilleur couple, sans écart entre cylindres ; le ralenti enrichit, car les gaz résiduels y gênent la combustion.
constexpr double kBestLambda = 0.88;
constexpr double kBestLambdaTurbo = 0.82;
constexpr double kIdleLambda = 0.9;
// Écart de mélange entre cylindres : de ± 5 à 10 % sur une pipe commune, moins avec un corps par cylindre (Heywood).
constexpr double kSharedSpread = 0.07;
constexpr double kPerCylinderSpread = 0.03;
// Volume derrière le papillon : un collecteur commun vaut environ la cylindrée, un conduit par cylindre bien moins.
constexpr double kSharedPlenum = 1.0;
constexpr double kPerCylinderPlenum = 0.15;
constexpr double kMinAheadG = 0.01;
// Le circuit de ralenti ne sert qu'à très faible débit, en part de celui du régime maximal à pleine charge : ralenti, et un filet de gaz.
constexpr double kIdleCircuitFlow = 0.06;
constexpr double kIdleCircuitSpan = 0.04;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/** Le dépôt selon l'atomisation : la vitesse efficace au col, celle qui fait la dépression. */
double wallShareAt(const CarburetorSpec& spec, double speed, double fill) {
    return spec.wallShare * kAtomizeMs / (kAtomizeMs + speed) * (0.4 + 0.6 * fill);
}

double filmTauAt(double speed, double fill) {
    const double root = std::sqrt(kFilmRefMs / std::max(speed, kFilmMinMs));
    // Sous vide, le film s'évapore plus vite.
    return kFilmTauS * root * std::sqrt(root) * std::max(fill, 0.3);
}

}  // namespace

FuelSystem fuelSystemOf(const EngineConfig& e) { return e.fuel == Fuel::Diesel ? FuelSystem::Injection : e.fuelSystem; }

CarburetorSpec carburetorSpecOf(const EngineConfig& e) {
    CarburetorSpec s;
    // Une injection, indirecte ou directe : pas de venturi.
    s.kind = hasCarburetor(e) ? fuelSystemOf(e) : FuelSystem::Injection;
    if (s.kind == FuelSystem::Injection) return s;
    // Le volume avalé en deux tours d'arbre, et le nombre de bouffées qu'il fait : un rotor en donne deux, chacune d'une chambre.
    const double litres = e.sweptCcPerTwoTurns() / 1000.0;
    const double cylinders = static_cast<double>(e.cylinders);
    const int induction = e.firingsPerTwoTurns();
    s.redlineRpm = static_cast<double>(e.redlineRpm);
    const double krpm = kRatedShare * s.redlineRpm / 1000.0;
    const bool shared = s.kind == FuelSystem::Carburetor;
    s.throats = shared ? 1 : e.cylinders;
    s.sharing = shared ? induction : induction / e.cylinders;
    s.throatM = (shared ? kSharedChoke * std::sqrt(litres * krpm) : kPerCylinderChoke * std::sqrt(litres / cylinders * krpm)) / 1000.0;
    s.pipeM = s.throatM * kPipeOverThroat;
    // Une pipe commune se partage et se coude pour atteindre chaque soupape ; un corps par cylindre va presque droit.
    s.pipeLengthM = shared ? 0.3 : 0.15;
    s.pipeBendsK = shared ? 1.1 : 0.15;
    s.plenumShare = shared ? kSharedPlenum : kPerCylinderPlenum;
    s.wallShare = 1.0 - portable::exp(-(shared ? kSharedWetPathM : kPerCylinderWetPathM) / kDepositLengthM);
    s.spread = shared ? kSharedSpread : kPerCylinderSpread;
    // Le gicleur vise le cylindre le plus pauvre : la moyenne est d'autant plus riche que l'écart est grand.
    s.partLambda = 1.0 - s.spread;
    s.fullLambda = (e.aspiration != Aspiration::Natural ? kBestLambdaTurbo : kBestLambda) * (1.0 - s.spread);
    s.pumpG = kPumpCm3PerLitre * litres * kFuelKgM3 / 1000.0;
    const AmbientAir air = ambientAirOf(e);
    s.airDensityShare = air.densityRatio;
    s.airTemperatureShare = air.temperatureRatio;
    return s;
}

double throatPulsation(int sharing) {
    static const std::array<double, kMaxCylinders + 1> table = [] {
        std::array<double, kMaxCylinders + 1> out{};
        constexpr int kSamples = 720;
        for (int n = 1; n <= kMaxCylinders; ++n) {
            double one = 0.0, three = 0.0;
            for (int i = 0; i < kSamples; ++i) {
                const double at = static_cast<double>(i) / kSamples;
                double flow = 0.0;
                for (int c = 0; c < n; ++c) {
                    double since = at - static_cast<double>(c) / n;
                    if (since < 0.0) since += 1.0;
                    if (since < kInductionShare) flow += portable::sin(portable::kPi * since / kInductionShare);
                }
                one += flow;
                three += flow * flow * flow;
            }
            one /= kSamples;
            three /= kSamples;
            out[static_cast<std::size_t>(n)] = three / (one * one * one);
        }
        return out;
    }();
    return table[static_cast<std::size_t>(std::clamp(sharing, 1, kMaxCylinders))];
}

double carburetorDropPa(const CarburetorSpec& spec, double displacementM3, double rpm, double volumetric) {
    if (spec.kind == FuelSystem::Injection) return 0.0;
    const double area = portable::kPi / 4.0 * spec.throatM * spec.throatM;
    // Le débit moyen d'un corps. Sa pression dynamique compte selon le débit : pas le carré de la moyenne, car l'à-coup pèse.
    const double speed = displacementM3 * rpm / 120.0 * volumetric / spec.throats / area;
    // Le même volume d'air du jour : sa masse volumique et sa vitesse du son (exactement celles de référence à 20 °C, 0 m).
    const double head = 0.5 * kAirKgM3 * speed * speed * throatPulsation(spec.sharing) * spec.airDensityShare;
    // Au-delà de Mach 0,3, le gaz se comprime : la dépression croît plus vite que v².
    const double mach2 = 2.0 * head / (kAirKgM3 * spec.airDensityShare) / (kSoundMs * kSoundMs * spec.airTemperatureShare);
    const double compressible = head * (1.0 + mach2 / 4.0);
    const double overPipe = spec.throatM / spec.pipeM;
    const double pipeK = kPipeFriction * spec.pipeLengthM / spec.pipeM + spec.pipeBendsK;
    return compressible * (kThroatLoss + pipeK * overPipe * overPipe * overPipe * overPipe);
}

double carburetorRichness(const CarburetorSpec& spec, double share, double rpm) {
    double richness = 1.0 / spec.partLambda;
    const double low = clamp((kIdleCircuitFlow - share * rpm / spec.redlineRpm) / kIdleCircuitSpan, 0.0, 1.0);
    richness += (1.0 / kIdleLambda - richness) * low;
    // La soupape d'enrichissement s'ouvre quand la dépression tombe : elle ne connaît ni la puissance ni la température.
    const double nearFull = clamp((share - 0.85) / 0.15, 0.0, 1.0);
    richness += (1.0 / spec.fullLambda - richness) * nearFull;
    // Des gicleurs choisis dans l'air de référence : un air moins dense passe moins vite qu'eux, et le mélange s'enrichit.
    if (spec.airDensityShare != 1.0) richness /= std::sqrt(spec.airDensityShare);
    return richness;
}

double torqueOfRichness(double richness) {
    constexpr double kBest = 1.15;
    if (richness >= kBest) {
        const double over = richness - kBest;
        // Très riche, la flamme hésite aussi, sans jamais tout perdre : ce surplus s'évapore en quelques tours.
        return std::max(1.0 - 0.5 * over * over, 0.5);
    }
    const double under = kBest - richness;
    double out = 1.0 - 0.3 * under - under * under;
    // Sous λ = 1,6, la flamme se propage mal : des cycles entiers ne brûlent plus.
    constexpr double kMisfire = 0.62;
    constexpr double kNoFlame = 0.45;
    if (richness < kMisfire) out *= clamp((richness - kNoFlame) / (kMisfire - kNoFlame), 0.0, 1.0);
    return out;
}

void Carburetor::configure(const CarburetorSpec& spec, double displacementM3) {
    spec_ = spec;
    throatArea_ = portable::kPi / 4.0 * spec.throatM * spec.throatM;
    pulsationRoot_ = std::sqrt(throatPulsation(spec.sharing));
    chargeG_ = kAirKgM3 * 1000.0 * displacementM3 * spec.plenumShare * spec.airDensityShare;
    torqueShare_ = 1.0;
    primed_ = false;
}

void Carburetor::step(const CarburetorInput& in, double dt) {
    // Ce qui remplit le collecteur passe par le venturi avant d'atteindre les cylindres.
    const double through = std::max(in.airGps + chargeG_ * in.fillRate, 0.0);
    const double settled = carburetorRichness(spec_, in.fill, in.rpm);
    const double wanted = through / kStoichiometric * settled;
    // La dépression au col suit la crête des bouffées : la vitesse efficace, pas la moyenne.
    const double density = kAirKgM3 * in.density;
    const double speed = through / 1000.0 / density / spec_.throats / throatArea_ * pulsationRoot_;
    double share = wallShareAt(spec_, speed, in.fill);
    double tau = filmTauAt(speed, in.fill);
    // À froid, l'essence distille moins sur la paroi : plus de dépôt, qui repart plus lentement. Moteur chaud, rien ne change.
    if (in.coolantC != kWarmCoolantC) {
        const FilmParameters cold = coldFilmShare(in.coolantC, in.fill);
        share = std::min(share * cold.deposit, 0.95);
        tau *= cold.tauS;
    }
    if (!primed_) {
        metered_ = wanted;
        film_.massG = share * wanted * tau;
        suspended_ = in.airGps > 0.0 ? wanted * std::max(chargeG_ * in.fill, kMinAheadG) / in.airGps : 0.0;
        pumpLeft_ = 0.0;
        pumpRate_ = 0.0;
        lastThrottle_ = in.throttle;
        primed_ = true;
    }
    const double fuelSpeed = kFuelDischarge * std::max(speed, 1.0) * std::sqrt(density / kFuelKgM3);
    const double lag = std::min(2.0 * kFuelColumnM / fuelSpeed, kFuelLagMaxS);
    metered_ += (wanted - metered_) * std::min(dt / lag, 1.0);

    // La pompe suit le papillon : une course à l'ouverture, rien à la fermeture.
    const double opened = in.throttle - lastThrottle_;
    lastThrottle_ = in.throttle;
    pumpRate_ += (opened / dt - pumpRate_) * std::min(dt / kPumpRateTauS, 1.0);
    if (opened > 0.0 && pumpRate_ > kPumpMinRatePerS) pumpLeft_ += spec_.pumpG * opened;
    pumpGps_ = pumpLeft_ / kPumpTauS;
    pumpLeft_ -= pumpGps_ * dt;

    const double toWall = share * metered_ + kPumpWallShare * pumpGps_;
    const double fromWall = film_.release(tau);
    film_.advance(toWall, fromWall, dt);
    // Ce qui reste en suspension attend dans le collecteur : ce qu'on y met en le remplissant n'atteint les cylindres qu'avec l'air qui le quitte.
    const double airAhead = std::max(chargeG_ * in.fill, kMinAheadG);
    const double drawn = in.airGps / airAhead;
    const double airborne = (1.0 - share) * metered_ + (1.0 - kPumpWallShare) * pumpGps_ + fromWall;
    // Implicite : à bas remplissage, le temps de séjour tombe sous le pas.
    suspended_ = (suspended_ + airborne * dt) / (1.0 + drawn * dt);
    const double intoCylinders = suspended_ * drawn;

    if (in.airGps <= 0.0) {
        torqueShare_ = 1.0;
        lambda_ = 0.0;
        return;
    }
    const double now = intoCylinders * kStoichiometric / in.airGps;
    lambda_ = now > 0.0 ? 1.0 / now : 0.0;
    // Rapporté au mélange pour lequel les gicleurs ont été choisis : celui de l'air de référence.
    const double calibrated = spec_.airDensityShare != 1.0 ? settled * std::sqrt(spec_.airDensityShare) : settled;
    torqueShare_ = torqueOfRichness(now) / torqueOfRichness(calibrated);
}

}  // namespace revforge
