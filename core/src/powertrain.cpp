#include "revforge/powertrain.hpp"

#include <algorithm>
#include <cmath>

#include "revforge/air_filter.hpp"
#include "revforge/atmosphere.hpp"
#include "revforge/cam.hpp"
#include "revforge/carburetor.hpp"
#include "revforge/combustion.hpp"
#include "revforge/charge_air.hpp"
#include "revforge/driving_model.hpp"
#include "revforge/ecu.hpp"
#include "revforge/injection.hpp"
#include "revforge/particulate_filter.hpp"
#include "revforge/valve_float.hpp"
#include "revforge/twin_turbo.hpp"
#include "revforge/two_stroke.hpp"
#include "revforge/valvetrain.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kAirDensity = 1.2;
constexpr double kRollingResistance = 0.0095;  // pneus de route récents, environ 8 à 10 kg par tonne
constexpr double kGravity = 9.81;

// Pression moyenne au frein à pleine charge (bar).
constexpr double kBmepDieselNa = 8.5;
// Chaque bar de pression ajoute presque un remplissage de plus : l'échangeur ne rattrape pas tout.
constexpr double kBoostEfficiency = 0.9;
// Un moteur suralimenté a une compression plus basse, pour ne pas détoner.
constexpr double kTurboCompressionLoss = 0.95;
// Le calculateur tient le couple constant au sommet : le plateau des moteurs turbo.
constexpr double kPlateauShare = 0.97;
constexpr double kPistonSpeedLimit = 24.0;  // m/s, au-delà le moteur respire mal
constexpr double kAtmosphere = 101325.0;
constexpr double kInletDensity = 1.2;
constexpr double kInletK = 293.0;
constexpr double kVolumetric = 0.92;
constexpr double kRatedShare = 0.88;
constexpr double kCarburetorVolumetric = 0.9;  // remplissage au pic de couple : c'est lui qui fixe le débit au col
// Sur un rotatif : ni distribution ni bielles, mais des segments d'apex. Ramené au volume balayé, un quart de frottements en moins (supposé).
constexpr double kRotaryFrictionShare = 0.75;
// Sur un deux-temps, les frottements d'un tour sont ceux d'un quatre-temps de même cylindrée, sans distribution ni pompage par le
// cylindre : un quart de moins (supposé, la distribution compte pour 20 à 30 % chez Heywood). Ramenés à son volume balayé, double,
// ce sont 0,75/2. Le frein moteur ne vient que du carter qui pompe contre le papillon, le cylindre restant ouvert sur ses lumières :
// la moitié de celui d'un quatre-temps de même cylindrée (supposé), soit 0,5/2.
constexpr double kTwoStrokeFrictionShare = 0.375;
constexpr double kTwoStrokePumpingShare = 0.25;

constexpr double kElectricTorqueNm = 350.0;
constexpr double kElectricPowerW = 190000.0;
constexpr double kElectricMaxRpm = 12000.0;

double clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/** Le volume balayé en deux tours d'arbre (L) : deux fois la cylindrée d'un rotatif. */
double liters(const EngineConfig& e) { return e.sweptCcPerTwoTurns() / 1000.0; }

/**
 * Couple net d'une pression moyenne (bar) : p × V / 4π, avec V le volume balayé en deux tours d'arbre. Un rotatif en balaie
 * deux fois sa cylindrée : son couple est donc p × V / 2π, sans autre coefficient.
 */
double bmepToNm(const EngineConfig& e, double bar) { return bar * 1e5 * liters(e) * 1e-3 / (4.0 * kPi); }

/** Un moteur qui tourne haut est plus poussé : soupapes, calage, admission. */
double naturalBmep(const EngineConfig& e) {
    if (e.burnsDiesel()) return kBmepDieselNa;
    const double pushed = clamp(11.0 + 1.1 * (static_cast<double>(e.redlineRpm) - 6200.0) / 1000.0, 10.5, 14.5);
    // Même air que le moteur à pistons de même régime, mais un rendement moindre : chambre allongée, faible compression.
    return isRotary(e) ? pushed * rotaryEfficiencyShare(e.aspiration) : pushed;
}

/**
 * Le venturi et la pipe prennent de la pression à l'air admis : moins de gaz à brûler, plus de travail de pompage. Les
 * frottements restent, donc le couple au frein perd plus que la charge. Sur un montage soufflé, le débit volumique et le
 * rapport des pressions ne changent pas avec la suralimentation : la même part (voir docs/modele.md).
 */
double carburetorBrakeShare(const EngineConfig& e, double rpm, double shape) {
    const CarburetorSpec carb = carburetorSpecOf(e);
    constexpr double kAtmosphereBar = 1e5;
    // La perte, en part de l'atmosphère du jour, d'où part l'air.
    const double drop = carburetorDropPa(carb, liters(e) * 1e-3, rpm, kCarburetorVolumetric * shape) / (kAtmosphereBar * ambientAirOf(e).pressureRatio);
    const double k = rpm / 1000.0;
    const double friction = (0.9 + 0.12 * k + 0.03 * k * k) * (isRotary(e) ? kRotaryFrictionShare : 1.0);
    const double brake = naturalBmep(e) * shape;
    return std::max(((brake + friction) * (1.0 - drop) - friction - drop) / brake, 0.3);
}

/**
 * La came change la pression moyenne indiquée (voir cam.hpp) ; les frottements restent. Le même calcul que pour le
 * carburateur, sur l'indiqué : ce que le cycle gagne ou perd, moins ce qui frotte toujours.
 */
double camBrakeShare(const EngineConfig& e, double rpm, double shape) {
    const double ratio = camTorqueRatio(e, rpm);
    const double k = rpm / 1000.0;
    const double friction = (0.9 + 0.12 * k + 0.03 * k * k + (e.burnsDiesel() ? 0.3 : 0.0)) * (isRotary(e) ? kRotaryFrictionShare : 1.0);
    const double brake = naturalBmep(e) * shape;
    return std::max(((brake + friction) * ratio - friction) / brake, 0.1);
}

/**
 * Le couple indiqué d'un deux-temps à pleine charge (bar de son volume balayé en deux tours) : l'air frais que son cycle enferme
 * (`twoStrokeIndicatedBar`). Le carburateur retire de la densité à l'air qui entre dans le carter, et son travail de pompage.
 */
double twoStrokeIndicated(const EngineConfig& e, double rpm) {
    const double indicated = twoStrokeIndicatedBar(e, rpm);
    if (fuelSystemOf(e) == FuelSystem::Injection) return indicated;
    const CarburetorSpec carb = carburetorSpecOf(e);
    const double drop = carburetorDropPa(carb, liters(e) * 1e-3, rpm, twoStrokeFullLoad(e, rpm).delivery) / 1e5;
    return indicated * (1.0 - drop) - drop;
}

/** La respiration selon le régime : 1 au pic de couple. */
double breathing(const EngineConfig& e, double rpm) {
    // Un deux-temps : ce que son carter fait entrer, ramené au remplissage du quatre-temps de référence pour les débits.
    if (e.twoStroke()) return twoStrokeFullLoad(e, rpm).delivery / kVolumetric;
    const double redline = static_cast<double>(e.redlineRpm);
    // Un moteur suralimenté, turbo ou compresseur, a des cames plus sages : son couple culmine un peu plus tôt.
    double peakAt = (e.aspiration == Aspiration::Natural ? 0.62 : 0.6) * redline;
    // Moins de section, un gaz plus rapide : l'admission s'accorde plus bas (voir « Le simple arbre »). Les culbuteurs aussi ont deux soupapes.
    if (twoValves(e.valvetrain())) peakAt *= sohcTuningRatio(static_cast<double>(e.boreMm));
    double shape;
    if (rpm <= peakAt) {
        const double below = 1.0 - rpm / peakAt;
        shape = 1.0 - 0.4 * below * below;
    } else {
        const double above = (rpm - peakAt) / (redline - peakAt);
        shape = 1.0 - 0.22 * above * above;
    }
    // Une longue course à haut régime : le gaz n'a plus le temps de passer. Un rotatif n'a ni course ni soupapes à faire suivre.
    if (!isRotary(e)) {
        const double pistonSpeed = 2.0 * e.strokeMm() / 1000.0 * rpm / 60.0;
        const double excess = std::max(pistonSpeed - kPistonSpeedLimit, 0.0);
        shape *= std::max(1.0 - 0.012 * excess * excess, 0.55);
        // Deux soupapes au lieu de quatre : le gaz qui passe mal, calculé depuis la section et la vitesse du piston.
        // L'indice de Mach se compte dans l'air du jour : 343 m/s à 20 °C.
        if (twoValves(e.valvetrain())) shape *= sohcBreathingRatio(static_cast<double>(e.boreMm), static_cast<double>(e.strokeMm()), rpm, ambientAirOf(e).soundSpeedMs);
        // Les culbuteurs et des ressorts raides frottent plus sur la came : c'est autant de couple au frein en moins.
        const double cam = valvetrainBrakeShare(e, rpm);
        if (cam < 1.0) shape *= cam;
        // Au-delà de l'affolement, l'admission ferme en retard et le piston repousse la charge : exactement 1 en dessous.
        const double kept = valveFloatShare(e, rpm);
        if (kept < 1.0) shape *= kept;
    }
    // Une ligne libre gagne en haut, mais perd la contre-pression qui aide en bas.
    const double fraction = clamp(rpm / redline, 0.0, 1.0);
    switch (e.exhaust) {
        case Exhaust::Stock: shape *= 0.96 - 0.04 * fraction; break;
        case Exhaust::Sport: break;
        case Exhaust::Straight: shape *= 1.0 + 0.04 * fraction - 0.08 * (1.0 - fraction) * (1.0 - fraction); break;
        // Une chambre sur un quatre-temps (la CLI seule le permet) : les soupapes ferment le cylindre avant que l'onde de retour n'y
        // repousse quoi que ce soit. Reste un tube libre jusqu'à la pointe et un silencieux garni : la respiration d'une ligne sport.
        case Exhaust::Expansion: break;
        case Exhaust::MufflerDelete: shape *= 1.0 + 0.02 * fraction - 0.04 * (1.0 - fraction) * (1.0 - fraction); break;
        case Exhaust::ResonatorDelete: shape *= 0.97 - 0.03 * fraction; break;
        case Exhaust::Quiet: shape *= 0.95 - 0.05 * fraction; break;
    }
    if (hasCarburetor(e)) shape *= carburetorBrakeShare(e, rpm, shape);
    if (!camIsStock(e)) shape *= camBrakeShare(e, rpm, shape);
    return shape;
}

/** Le même moteur avec sa came d'origine : le turbo, le compresseur et la boîte restent ceux qu'il avait. */
EngineConfig withStockCam(const EngineConfig& e) {
    EngineConfig stock = e;
    stock.camDurationDeg = 0;
    stock.camLiftMm = 0.0f;
    stock.lobeSeparationDeg = 0;
    stock.valveTiming = ValveTiming::Fixed;
    return stock;
}

/** La came d'origine, ou celle-ci : à ne calculer qu'une fois s'il n'y a rien à changer. */
const EngineConfig& sizedEngine(const EngineConfig& e, EngineConfig& scratch) {
    if (camIsStock(e)) return e;
    scratch = withStockCam(e);
    return scratch;
}

}  // namespace

const BodySpec& bodySpec(Body body) {
    static const BodySpec city{0.68, 2.45, 0.55, 0.58, 0.85f, 0.38, 5, 3.75f, 1.68f, 1.5f, 0.6f, 0.32f, EnginePlacement::Front};
    static const BodySpec compact{0.70, 2.62, 0.55, 0.56, 1.0f, 0.3, 6, 4.3f, 1.8f, 1.45f, 0.75f, 0.33f, EnginePlacement::Front};
    static const BodySpec coupe{0.66, 2.72, 0.5, 0.52, 1.1f, 0.22, 6, 4.6f, 1.85f, 1.3f, 0.85f, 0.33f, EnginePlacement::Front};
    static const BodySpec supercar{0.62, 2.65, 0.42, 0.43, 0.45f, 0.12, 7, 4.55f, 1.95f, 1.15f, 0.95f, 0.5f, EnginePlacement::Mid};
    static const BodySpec van{1.35, 3.2, 0.85, 0.55, 1.25f, 0.45, 5, 4.9f, 1.9f, 1.95f, 1.0f, 0.38f, EnginePlacement::Front};
    // Un coupé trois volumes des années 80 : Cx de 0,40 sur 1,85 m². Voir « La carrosserie des années 80 » dans docs/modele.md.
    static const BodySpec retro{0.74, 2.5, 0.52, 0.53, 0.98f, 0.36, 5, 4.25f, 1.65f, 1.36f, 0.95f, 0.3f, EnginePlacement::Front};
    switch (body) {
        case Body::Compact: return compact;
        case Body::Coupe: return coupe;
        case Body::Supercar: return supercar;
        case Body::Van: return van;
        case Body::Retro: return retro;
        case Body::City: break;
    }
    return city;
}

EnginePlacement placementOf(const CarConfig& car) {
    return car.placement == EnginePlacement::Natural ? bodySpec(car.body).naturalPlacement : car.placement;
}

namespace {

/** Le moteur, par rapport à l'essieu arrière (m) : juste derrière le train avant, devant le train arrière, ou derrière lui. */
double engineFromRearAxle(const BodySpec& body, EnginePlacement placement) {
    switch (placement) {
        case EnginePlacement::Mid: return 0.6;
        case EnginePlacement::Rear: return -0.35;
        default: return body.wheelbaseM - 0.15;
    }
}

/**
 * Le moteur et sa boîte pèsent environ 16 % de la voiture, le reste est à peu près équilibré. Ce simple
 * calcul donne 55 % sur l'avant pour un moteur avant, 44 % au centre, 38 % à l'arrière (une 911 fait 38 à 39 %).
 */
double engineFrontShare(const BodySpec& body, EnginePlacement placement) {
    constexpr double kEngineShare = 0.16;
    constexpr double kRestOfCar = 0.48;
    return (1.0 - kEngineShare) * kRestOfCar + kEngineShare * engineFromRearAxle(body, placement) / body.wheelbaseM;
}

}  // namespace

double frontShareOf(const CarConfig& car) {
    const BodySpec& body = bodySpec(car.body);
    const EnginePlacement placement = placementOf(car);
    if (placement == body.naturalPlacement) return body.frontShare;
    return body.frontShare + engineFrontShare(body, placement) - engineFrontShare(body, body.naturalPlacement);
}

float exhaustScaleOf(const CarConfig& car) {
    const BodySpec& body = bodySpec(car.body);
    const EnginePlacement placement = placementOf(car);
    if (placement == body.naturalPlacement) return body.exhaustScale;
    // Le trajet du moteur jusqu'à l'arrière de la voiture. Un moteur arrière enroule quand même ses tubes.
    const double natural = static_cast<double>(body.rearAxleM) + engineFromRearAxle(body, body.naturalPlacement);
    const double here = static_cast<double>(body.rearAxleM) + engineFromRearAxle(body, placement);
    return std::max(body.exhaustScale * static_cast<float>(here / natural), 0.25f);
}

namespace {

CarAcoustics carAcousticsOf(const CarConfig& car, const EngineProfile& profile) {
    const BodySpec& body = bodySpec(car.body);
    const EnginePlacement placement = placementOf(car);
    CarAcoustics a;
    a.lengthM = body.lengthM;
    a.widthM = body.widthM;
    a.heightM = body.heightM;
    a.wheelbaseM = static_cast<float>(body.wheelbaseM);
    a.placement = placement;
    a.insulation = car.insulation;
    a.windowsOpen = car.windowsOpen;

    const float rearEnd = -body.lengthM / 2.0f;
    const float rearAxle = rearEnd + body.rearAxleM;
    const float frontAxle = rearAxle + static_cast<float>(body.wheelbaseM);
    const float engineX = rearAxle + static_cast<float>(engineFromRearAxle(body, placement));
    a.engine = {engineX, 0.0f, 0.6f};
    // La tête du conducteur : assis loin du train avant, sauf si un moteur central l'en rapproche.
    const float seatX = placement == EnginePlacement::Mid ? rearAxle + 1.45f : frontAxle - 1.7f;
    a.seat = {seatX, 0.37f, body.heightM - 0.22f};
    a.cabinFrontM = placement == EnginePlacement::Mid ? 0.95f : 0.85f;
    a.cabinBackM = placement == EnginePlacement::Mid ? 0.25f : 0.95f;

    // Sur de vrais passages, le peigne de sol ne dépasse pas 3 dB : une voiture n'est pas un point, et l'air bouge.
    constexpr float kGroundCoherence = 0.25f;
    const auto place = [&a](Source source, Point3 at, float extent) {
        a.sources[static_cast<std::size_t>(source)] = SourcePlace{at, extent, kGroundCoherence};
    };
    // Une sortie unique est décalée d'un côté ; deux sorties s'écartent.
    const float tailY = profile.routing == ExhaustRouting::Single ? -0.35f : 0.4f * body.widthM / 2.0f + 0.2f;
    // Rayon de sortie : le tube suit la cylindrée (voir ExhaustLine).
    const double litres = static_cast<double>(profile.boreM * profile.boreM * profile.strokeM) * 0.7854 * profile.cylinders * 1000.0;
    const double diameter = std::clamp(0.05 * std::sqrt(std::max(litres, 0.1) / 2.0), 0.032, 0.09);
    a.outletRadiusM = static_cast<float>(diameter / 2.0);
    // La pression au plan de sortie est celle d'un monopôle à 0,35 rayon : la source ne diverge jamais. Et le centre
    // acoustique est à 0,61 rayon en aval de la bouche : c'est la correction de bout.
    const float outletExtent = 0.35f * a.outletRadiusM;
    const float outletX = rearEnd - 0.03f - 0.61f * a.outletRadiusM;
    place(Source::ExhaustA, {outletX, tailY, body.outletHeightM}, outletExtent);
    place(Source::ExhaustB, {outletX, profile.routing == ExhaustRouting::Single ? tailY : -tailY, body.outletHeightM}, outletExtent);

    // L'admission : derrière la calandre sur un moteur avant, une prise d'air sur le flanc au centre, le capot arrière au fond.
    if (placement == EnginePlacement::Front) place(Source::Intake, {-rearEnd + 0.02f, 0.3f, 0.65f}, 0.06f);
    else if (placement == EnginePlacement::Mid) place(Source::Intake, {engineX + 0.25f, body.widthM / 2.0f + 0.02f, 0.75f}, 0.06f);
    else place(Source::Intake, {rearEnd - 0.02f, 0.25f, body.heightM * 0.7f}, 0.06f);

    // Le compartiment moteur rayonne par ses ouvertures : capot fermé, on l'entend par le bas et la calandre.
    if (placement == EnginePlacement::Front) place(Source::Bay, {-rearEnd + 0.02f, 0.0f, 0.35f}, 0.3f);
    else place(Source::Bay, {rearEnd - 0.02f, 0.0f, 0.45f}, 0.3f);
    // Sans boîte ni prise d'air, l'admission s'ouvre dans le compartiment : elle sort par les mêmes ouvertures.
    if (airFilterSpec(profile.airFilter).underBonnet) a.sources[static_cast<std::size_t>(Source::Intake)] = a.sources[static_cast<std::size_t>(Source::Bay)];

    // Sous le plancher, au milieu de la voiture : boîte et transmission. À l'arrière pour un moteur arrière.
    const float underX = placement == EnginePlacement::Rear ? rearAxle : (rearAxle + frontAxle) / 2.0f;
    place(Source::Underfloor, {underX, 0.0f, 0.12f}, 0.35f);
    // Les pneus et le vent : répartis sur toute la voiture.
    place(Source::Road, {0.0f, 0.0f, 0.12f}, static_cast<float>(body.wheelbaseM) / 2.0f);
    return a;
}

}  // namespace

EngineProfile Powertrain::profile() const {
    EngineProfile p = electric ? electricProfile() : engine.toProfile();
    // Un twincharger a aussi un compresseur, avec ses poches : son son en dépend (twin_turbo.cpp).
    if (!electric && usesTwinTurbo(engine) && engine.turboLayout == TurboLayout::Twincharger) twinchargerProfile(*this, p);
    p.exhaustScale = exhaustScaleOf(car);
    p.car = carAcousticsOf(car, p);
    p.car.air = airAcousticsOf(ambientAirOf(engine));
    return p;
}

Powertrain referencePowertrain() {
    Powertrain p;
    p.engine.cylinders = 4;
    p.engine.displacementCc = 1360;
    p.engine.boreMm = 75;
    p.engine.redlineRpm = 6200;
    p.engine.exhaust = Exhaust::Stock;
    return p;
}

TurboSpec turboSpecOf(const Powertrain& p) {
    // Dimensionné dans l'air de référence et sur la came d'origine : en altitude, par grand froid ou avec une autre came, c'est
    // le même turbo (turboSpecFor ne lit pas la came).
    EngineConfig scratch;
    const EngineConfig sized = withReferenceAir(sizedEngine(p.engine, scratch));
    TurboSpec spec = turboSpecFor(sized, breathing(sized, 0.88 * static_cast<double>(p.engine.redlineRpm)));
    // Le turbo reste le même, dimensionné sur la voiture d'origine : seul change l'air qu'il reçoit.
    if (p.engine.airFilter != AirFilter::Stock) {
        spec.filterDrop *= airFilterDropShare(p.engine.airFilter);
        spec.inletFilter = p.engine.airFilter;
        spec.inletPlacement = placementOf(p.car);
    }
    const AmbientAir air = ambientAirOf(p.engine);
    spec.ambientPa = air.pressurePa;
    spec.ambientK = air.temperatureK;
    spec.pipe.ambientK = air.temperatureK;
    // De même pour la carte du calculateur : elle ne change que le gaz qu'il reçoit.
    spec.ecu = ecuMapExhaust(p.engine);
    return spec;
}

namespace {

/** Le débit d'un moteur atmosphérique à pleine charge, à ce régime (kg/s). */
double naturalFlowKgS(const EngineConfig& e, double rpm) {
    return kVolumetric * breathing(e, rpm) * kInletDensity * liters(e) * 1e-3 * rpm / 120.0;
}

double ratedRegime(const EngineConfig& e, double rpm) { return rpm / (kRatedShare * static_cast<double>(e.redlineRpm)); }

/**
 * Ce que ce filtre change au couple à pleine charge, par rapport au papier d'origine sur lequel il est calibré. La masse
 * d'air suit la pression et la densité derrière le filtre, et n'agit que sur le travail des gaz : les frottements restent.
 * Sur un turbo, c'est la soupape qui tient la pression : il ne reste que la température de la charge.
 */
double withAirFilter(const Powertrain& p, double rpm, double boost, double brakeNm) {
    const EngineConfig& e = p.engine;
    const double friction = frictionTorqueNm(p, rpm);
    const double rise = airFilterRiseK(e.airFilter, placementOf(p.car), ratedRegime(e, rpm));
    if (e.aspiration == Aspiration::Turbo) {
        return (brakeNm + friction) * turboChargeDensityShare(static_cast<double>(e.boostBar), boost, rise, clamp(ratedRegime(e, rpm), 0.0, 1.5)) - friction;
    }
    const double coefficient = naturalFilterCoefficient(liters(e) * 1e-3, naturalFlowKgS(e, kRatedShare * static_cast<double>(e.redlineRpm)));
    const double flow = naturalFlowKgS(e, rpm) * kInletK / (kInletK + rise);
    const double stockDrop = coefficient * flow * flow;
    const double drop = stockDrop * airFilterDropShare(e.airFilter);
    double fill = (kAtmosphere - drop) / (kAtmosphere - stockDrop) * kInletK / (kInletK + rise);
    if (e.airFilter == AirFilter::Stacks) fill *= 1.0 + stackRamGain(e, rpm);
    // Moins de dépression à l'admission, c'est aussi moins de travail de pompage.
    return (brakeNm + friction) * fill - friction + (stockDrop - drop) * liters(e) * 1e-3 / (4.0 * kPi);
}

/**
 * L'air du jour : l'air piégé change, les frottements restent. Dans l'air de référence, rien ne s'exécute : le couple de
 * référence est celui que calibrent tous les moteurs réels de la doc.
 */
double withAmbientAir(const Powertrain& p, double rpm, double brakeNm, double boost) {
    const double share = ambientChargeShare(p, rpm, boost);
    if (share == 1.0) return brakeNm;
    const double friction = frictionTorqueNm(p, rpm);
    return (brakeNm + friction) * share - friction;
}

}  // namespace

Powertrain withReferenceAir(const Powertrain& p) {
    Powertrain out = p;
    out.engine = withReferenceAir(p.engine);
    return out;
}

double ambientChargeShare(const Powertrain& p, double rpm, double boost) {
    if (p.electric) return 1.0;
    const EngineConfig& e = p.engine;
    const AmbientAir air = ambientAirOf(e);
    if (air.reference()) return 1.0;
    // Le compartiment moteur réchauffe d'autant l'air de n'importe quel jour.
    const double rise = airFilterRiseK(e.airFilter, placementOf(p.car), ratedRegime(e, rpm));
    switch (e.aspiration) {
        case Aspiration::Turbo:
            return turboAmbientChargeShare(static_cast<double>(e.boostBar), boost, rise, clamp(ratedRegime(e, rpm), 0.0, 1.5), air.pressurePa, air.temperatureK);
        case Aspiration::Supercharger:
            return superchargerAmbientChargeShare(superchargerSpecOf(p), rpm, breathing(e, rpm), boost);
        case Aspiration::Natural: break;
    }
    return trappedAirShare(air.pressureRatio, kInletK + rise, air.temperatureK + rise);
}

double intakeAirK(const Powertrain& p, double rpm) {
    // L'air du jour : 293 K à 20 °C, comme l'air de référence.
    const double ambientK = ambientAirOf(p.engine).temperatureK;
    if (p.electric) return ambientK;
    return ambientK + airFilterRiseK(p.engine.airFilter, placementOf(p.car), ratedRegime(p.engine, rpm));
}

double intakeDropPa(const Powertrain& p, double rpm) {
    if (p.electric) return 0.0;
    const EngineConfig& e = p.engine;
    if (e.aspiration == Aspiration::Turbo) {
        if (usesTwinTurbo(e)) return twinIntakeDropPa(p, rpm);
        const TurboSpec spec = turboSpecOf(p);
        const double flow = turboSteady(spec, rpm, breathing(e, rpm)).flow;
        return spec.filterDrop * flow * flow;
    }
    if (e.aspiration == Aspiration::Supercharger) {
        const SuperchargerSpec spec = superchargerSpecOf(p);
        const double flow = superchargerSteady(spec, rpm, breathing(e, rpm)).flow;
        return spec.filterDrop * flow * flow;
    }
    // Le débit de l'air de référence, puis ce qu'en laisse l'air du jour.
    double flow = naturalFlowKgS(e, rpm) * kInletK / (kInletK + airFilterRiseK(e.airFilter, placementOf(p.car), ratedRegime(e, rpm)));
    if (!referenceAirOf(e)) flow *= ambientChargeShare(p, rpm, 0.0);
    return naturalFilterCoefficient(liters(e) * 1e-3, naturalFlowKgS(e, kRatedShare * static_cast<double>(e.redlineRpm))) * flow * flow * airFilterDropShare(e.airFilter);
}

double breathingOf(const Powertrain& p, double rpm) { return breathing(p.engine, rpm); }

namespace {

/**
 * Un autre échangeur sur un compresseur mécanique, ou aucun : le compresseur reste celui de l'échangeur d'origine, seuls changent
 * le chemin de l'air et ce qui le refroidit (charge_air.cpp). La sortie du compresseur au nominal demande son équilibre : trop lent
 * pour chaque pas de `fullLoadTorqueNm`, on garde la dernière.
 */
struct ChargerHotMemo {
    bool valid = false;
    EngineConfig engine;
    EnginePlacement placement = EnginePlacement::Front;
    double hotK = 293.0;
};
ChargerHotMemo g_chargerHot;

void withChargeCooler(const Powertrain& p, SuperchargerSpec& spec) {
    const double ratedRpm = kSuperchargerRatedShare * static_cast<double>(p.engine.redlineRpm);
    const EnginePlacement placement = placementOf(p.car);
    if (!g_chargerHot.valid || !(g_chargerHot.engine == p.engine) || g_chargerHot.placement != placement) {
        g_chargerHot.hotK = superchargerSteady(spec, ratedRpm, breathing(p.engine, ratedRpm)).outK;
        g_chargerHot.engine = p.engine;
        g_chargerHot.placement = placement;
        g_chargerHot.valid = true;
    }
    // Le conduit d'un 2 L fait 55 mm, comme celui du turbo.
    const double area = 0.7854 * 0.055 * 0.055 * spec.displacementM3 / 0.002;
    applyChargePath(p.engine.intercooler, false, area, spec.chargeVolumeM3, spec.coolerDrop);
    spec.cooler = chargeCoolerFor(p.engine.intercooler, spec.ratedFlow, g_chargerHot.hotK);
}

}  // namespace

SuperchargerSpec superchargerSpecOf(const Powertrain& p) {
    const double redline = static_cast<double>(p.engine.redlineRpm);
    // Comme le turbo : dimensionné dans l'air de référence, sur la came d'origine.
    EngineConfig scratch;
    const EngineConfig sized = withReferenceAir(sizedEngine(p.engine, scratch));
    SuperchargerSpec spec = superchargerSpecFor(sized, breathing(sized, kVolumetricSizingShare * redline), breathing(sized, kSuperchargerRatedShare * redline));
    // Comme le turbo : le compresseur reste celui du papier d'origine, seul change l'air qu'il reçoit.
    if (p.engine.airFilter != AirFilter::Stock) {
        spec.filterDrop *= airFilterDropShare(p.engine.airFilter);
        spec.inletFilter = p.engine.airFilter;
        spec.inletPlacement = placementOf(p.car);
    }
    const AmbientAir air = ambientAirOf(p.engine);
    spec.ambientPa = air.pressurePa;
    spec.ambientK = air.temperatureK;
    if (p.engine.intercooler != Intercooler::AirToAir) withChargeCooler(p, spec);
    return spec;
}

double superchargerDriveNm(const Powertrain& p, double rpm, double fill) {
    if (p.electric || p.engine.aspiration != Aspiration::Supercharger) return 0.0;
    return superchargerSteady(superchargerSpecOf(p), rpm, breathing(p.engine, rpm), fill).driveNm;
}

double steadyBoost(const Powertrain& p, double rpm) {
    if (p.electric || p.engine.aspiration == Aspiration::Natural) return 0.0;
    if (p.engine.aspiration == Aspiration::Supercharger) return superchargerSteady(superchargerSpecOf(p), rpm, breathing(p.engine, rpm)).boost;
    if (usesTwinTurbo(p.engine)) return twinSteadyBoost(p, rpm);
    return turboSteady(turboSpecOf(p), rpm, breathing(p.engine, rpm)).boost;
}

namespace {

/**
 * Le couple que donne l'air admis, brûlé comme le moteur d'origine : taux de compression d'origine, essence de référence,
 * carte d'origine du calculateur, et l'injection d'origine (indirecte, ou pompe mécanique sur un diesel).
 */
double airTorqueNm(const Powertrain& p, double rpm, double boost) {
    const EngineConfig& e = p.engine;
    // Sous le ralenti, la combustion devient instable : le couple s'effondre.
    const double idle = idleRpmOf(p);
    const double under = rpm < idle ? (rpm / idle) * (rpm / idle) : 1.0;
    // Tout ce qui suit est calibré dans l'air de référence ; l'air du jour passe ensuite par `withAmbientAir`.
    // Un deux-temps : le cycle intégré donne l'air frais enfermé, donc le couple indiqué ; les frottements se retranchent.
    if (e.twoStroke()) {
        const double natural = (bmepToNm(e, twoStrokeIndicated(e, rpm)) - frictionTorqueNm(p, rpm)) * under;
        return withAmbientAir(p, rpm, e.airFilter == AirFilter::Stock ? natural : withAirFilter(p, rpm, boost, natural), boost);
    }
    if (e.aspiration == Aspiration::Natural) {
        const double natural = bmepToNm(e, naturalBmep(e)) * breathing(e, rpm) * under;
        return withAmbientAir(p, rpm, e.airFilter == AirFilter::Stock ? natural : withAirFilter(p, rpm, boost, natural), boost);
    }
    const double base = bmepToNm(e, naturalBmep(e) * kTurboCompressionLoss);
    // Le compresseur : l'air admis, tel qu'il sort de son calcul. Ce qu'il prend au vilebrequin se compte à part, comme le pompage.
    if (e.aspiration == Aspiration::Supercharger) {
        const double shape = breathing(e, rpm);
        return withAmbientAir(p, rpm, base * shape * superchargerAirShare(superchargerSpecOf(p), rpm, shape, boost) * under, boost);
    }
    const double gain = kBoostEfficiency * static_cast<double>(e.boostBar);
    const double charged = std::min(base * breathing(e, rpm) * (1.0 + gain * boost), base * (1.0 + gain) * kPlateauShare) * under;
    return withAmbientAir(p, rpm, e.airFilter == AirFilter::Stock ? charged : withAirFilter(p, rpm, boost, charged), boost);
}

/** La pompe haute pression et le filtre à particules : ils ne changent rien sans injection directe ni filtre. */
bool injectionChanges(const Powertrain& p) { return !p.electric && (directInjected(p.engine) || particulateFiltered(p.engine)); }

/** Ce que coûtent à pleine charge la pompe haute pression et le filtre, à sa suie moyenne (Nm). */
double injectionLossNm(const Powertrain& p, double rpm, double boost) {
    return directPumpTorqueNm(p, rpm, 1.0, boost) + filterPumpingTorqueNm(p, rpm, 1.0, boost, meanSootG(p));
}

}  // namespace

/** La charge d'un cylindre à pleine charge (mg de gazole par cycle), à la limite de fumée de la pompe mécanique. */
DieselPoint dieselPointOf(const Powertrain& p, double rpm, double load, double boost) {
    const EngineConfig& e = p.engine;
    DieselPoint point;
    point.rpm = rpm;
    point.load = load;
    point.redlineRpm = static_cast<double>(e.redlineRpm);
    point.cylinderM3 = static_cast<double>(e.displacementCc) * 1e-6 / std::max(e.cylinders, 1);
    point.strokeM = static_cast<double>(e.strokeMm()) / 1000.0;
    point.intakeK = intakeAirK(p, rpm);
    point.intakeBar = e.aspiration == Aspiration::Natural ? 1.0 : 1.0 + static_cast<double>(e.boostBar) * boost;
    const double airMg = kInletDensity * point.intakeBar * point.cylinderM3 * kVolumetric * 1e6;
    point.fullFuelMg = airMg / (14.5 * smokeLimitLambda(dieselInjectionBar(false, rpm, point.redlineRpm, 1.0)));
    point.commonRail = commonRailed(e);
    return point;
}

double fullLoadTorqueNm(const Powertrain& p, double rpm, double boost) {
    if (p.electric) {
        const double omega = std::max(rpm, 1.0) * 2.0 * kPi / 60.0;
        return std::min(kElectricTorqueNm, kElectricPowerW / omega);
    }
    const EngineConfig& e = p.engine;
    const bool stockBurn = combustionStock(e);
    const bool stockMap = !ecuMapTuned(e);
    const bool direct = directInjected(e);
    const bool rail = commonRailed(e);
    if (stockBurn && stockMap && !direct && !rail) return airTorqueNm(p, rpm, boost);
    // Le même air, brûlé autrement. Seul le travail des gaz en dépend : les frottements restent. Chaque part est indépendante et
    // vaut exactement la sienne quand les autres sont d'origine :
    // - la combustion : le taux de compression change le rendement, le carburant l'énergie par kilogramme d'air (« La combustion ») ;
    // - la carte du calculateur : l'écart d'avance et de richesse à ce carburant (ecu.cpp) ;
    // - l'injection directe : la charge refroidie par le carburant est plus dense (injection.cpp) ;
    // - la rampe commune : elle brûle plus de gazole avant de fumer, et le brûle plus vite.
    const double friction = frictionTorqueNm(p, rpm);
    double share = 1.0;
    if (!stockBurn) share *= combustionTorqueShare(e);
    if (!stockMap) share *= ecuFullLoadShare(e);
    if (direct) share *= 1.0 + directFillGain(rpm, static_cast<double>(e.redlineRpm), intakeAirK(p, rpm), injectedFuelOf(e));
    if (rail) share *= commonRailTorqueShare(dieselPointOf(p, rpm, 1.0, boost));
    return (airTorqueNm(p, rpm, boost) + friction) * share - friction;
}

namespace {

double plainTorqueCurveNm(const Powertrain& p, double rpm) {
    // Le cliquetis : à pleine charge stabilisée, l'avance que laisse le capteur retire sa part du travail des gaz. Sans réglage de la
    // combustion, la carte d'origine reste sous sa limite : rien ne change.
    const KnockMap* knock = p.electric ? nullptr : knockMapOf(p);
    const auto full = [&](double boost) {
        const double torque = fullLoadTorqueNm(p, rpm, boost);
        if (knock == nullptr) return torque;
        const double friction = frictionTorqueNm(p, rpm);
        return (torque + friction) * knock->steadyTorqueShare(rpm) - friction;
    };
    // La pompe haute pression et le filtre à particules se paient au vilebrequin, comme l'entraînement d'un compresseur.
    const bool injection = injectionChanges(p);
    if (p.electric || p.engine.aspiration == Aspiration::Natural) {
        const double torque = full(0.0);
        return injection ? torque - injectionLossNm(p, rpm, 0.0) : torque;
    }
    // Pas d'arbre à lancer : la pression d'équilibre, moins ce que coûte l'entraînement.
    if (p.engine.aspiration == Aspiration::Supercharger) {
        const SuperchargerSteady steady = superchargerSteady(superchargerSpecOf(p), rpm, breathing(p.engine, rpm));
        const double torque = full(steady.boost) - steady.driveNm;
        return injection ? torque - injectionLossNm(p, rpm, steady.boost) : torque;
    }
    // L'arbre stabilisé : la courbe de la fiche ne dit rien du retard. La contre-pression, elle, se paie toujours.
    // Deux arbres (twin_turbo.cpp) : leur équilibre, le même cliquetis et les mêmes pertes de l'injection qu'un turbo simple.
    if (usesTwinTurbo(p.engine)) {
        const TwinTurboSteady steady = twinTurboSteady(twinTurboSpecOf(p), rpm, breathingOf(p, rpm));
        const double torque = full(steady.boost) - steady.backPressureNm - steady.chargerNm;
        return injection ? torque - injectionLossNm(p, rpm, steady.boost) : torque;
    }
    const TurboSpec spec = turboSpecOf(p);
    const TurboSteady steady = turboSteady(spec, rpm, breathing(p.engine, rpm));
    const double torque = full(steady.boost) - turboBackPressureNm(spec, steady.chargePa, steady.turbineInPa, steady.flow, 1.0);
    return injection ? torque - injectionLossNm(p, rpm, steady.boost) : torque;
}

}  // namespace

double torqueCurveNm(const Powertrain& p, double rpm) {
    const double plain = plainTorqueCurveNm(p, rpm);
    // L'échangeur, l'eau, le protoxyde : rien de cela ne s'exécute au défaut (charge_air.cpp).
    return chargeAirActive(p) ? chargeAirTorqueNm(p, rpm, plain) : plain;
}

double frictionTorqueNm(const Powertrain& p, double rpm) {
    // Fer et ventilation d'un moteur électrique, de l'ordre du kilowatt à 5000 tr/min : le reste est dans le rendement.
    if (p.electric) return 0.6 + rpm * 0.00035;
    const double k = rpm / 1000.0;
    double bar = 0.9 + 0.12 * k + 0.03 * k * k + (p.engine.burnsDiesel() ? 0.3 : 0.0);
    if (p.engine.valvetrain() == Camshaft::Sohc) bar += sohcFrictionDeltaBar(static_cast<double>(p.engine.strokeMm()), rpm);
    // Les culbuteurs chargent la came plus fort, et des ressorts raides aussi : voir « Les culbuteurs et l'affolement ».
    if (p.engine.valvetrain() == Camshaft::Ohv) bar += ohvFrictionDeltaBar(p.engine, rpm);
    if (p.engine.valveSprings == ValveSprings::Stiff) bar += springFrictionDeltaBar(p.engine, rpm);
    return bmepToNm(p.engine, isRotary(p.engine) ? bar * kRotaryFrictionShare : (p.engine.twoStroke() ? bar * kTwoStrokeFrictionShare : bar));
}

double pumpingTorqueNm(const Powertrain& p, double rpm, double manifold) {
    if (p.electric) return 0.0;
    const double idle = idleRpmOf(p);
    const double above = clamp((rpm - idle) / 1500.0, 0.0, 1.0);
    const double pumping = bmepToNm(p.engine, p.engine.twoStroke() ? 3.2 * kTwoStrokePumpingShare : 3.2) * above * (1.0 - clamp(manifold, 0.0, 1.0));
    // Le collecteur se vide sous l'atmosphère du jour, et le moteur refoule contre elle : (p_atm − p_coll) = p_atm·(1 − remplissage).
    const AmbientAir air = ambientAirOf(p.engine);
    return air.reference() ? pumping : pumping * air.pressureRatio;
}

double engineInertia(const Powertrain& p) {
    if (p.electric) return 0.05;
    const double light = clamp(6500.0 / static_cast<double>(p.engine.redlineRpm), 0.6, 1.2);
    // Un deux-temps allume deux fois plus souvent, de moitié moins fort : il lui faut le volant de sa cylindrée, pas de son volume balayé.
    const double inertia = (0.03 + 0.055 * (p.engine.twoStroke() ? static_cast<double>(p.engine.displacementCc) / 1000.0 : liters(p.engine))) * light;
    if (p.engine.valvetrain() == Camshaft::Ohv) return inertia + ohvInertiaDelta(p.engine);
    return p.engine.valvetrain() == Camshaft::Sohc ? inertia + sohcInertiaDelta(p.engine) : inertia;
}

double idleRpmOf(const Powertrain& p) { return static_cast<double>(p.profile().idleRpm); }

ExhaustState exhaustStateOf(const Powertrain& p, double rpm, double fill, double boost) {
    ExhaustState out;
    if (p.electric) return out;
    const EngineConfig& e = p.engine;
    const bool diesel = e.burnsDiesel();
    const double charged = e.aspiration == Aspiration::Natural ? 1.0 : 1.0 + static_cast<double>(e.boostBar) * boost;
    const double share = clamp(fill, 0.0, 1.0);
    // Un diesel n'a pas de papillon : il avale tout, et dose son gazole. Un essence dose l'air, à richesse 1, plus riche à fond.
    const double air = naturalFlowKgS(e, rpm) * charged * (diesel ? 1.0 : std::max(share, 0.08));
    if (diesel) {
        out.lambda = smokeLambdaOf(p, rpm) / std::max(share, 0.05);
    } else {
        out.lambda = share > 0.85 ? 0.88 : 1.0;
    }
    const double fuelPerAir = 1.0 / ((diesel ? 14.5 : 14.7) * out.lambda);
    out.fuelGps = air * fuelPerAir * 1000.0;
    out.kgS = air * (1.0 + fuelPerAir);
    out.kelvin = exhaustGasK(diesel, intakeAirK(p, rpm), fuelPerAir);
    return out;
}

double directPumpTorqueNm(const Powertrain& p, double rpm, double fill, double boost) {
    if (p.electric || !directInjected(p.engine)) return 0.0;
    const ExhaustState gas = exhaustStateOf(p, rpm, fill, boost);
    return directPumpNm(gas.fuelGps, directRailBar(fill, rpm, static_cast<double>(p.engine.redlineRpm)), rpm, injectedFuelOf(p.engine));
}

double filterPumpingTorqueNm(const Powertrain& p, double rpm, double fill, double boost, double sootG) {
    if (p.electric || !particulateFiltered(p.engine)) return 0.0;
    const ParticulateFilterSpec spec = particulateFilterSpecOf(p.engine);
    const ExhaustState gas = exhaustStateOf(p, rpm, fill, boost);
    return filterPumpingNm(spec, filterBackPressurePa(spec, sootG, gas.kgS, gas.kelvin));
}

double meanSootG(const Powertrain& p) {
    ParticulateFilter filter;
    filter.configure(particulateFilterSpecOf(p.engine));
    return filter.sootG();
}

double smokeLambdaOf(const Powertrain& p, double rpm) {
    return smokeLimitLambda(dieselInjectionBar(commonRailed(p.engine), rpm, static_cast<double>(p.engine.redlineRpm), 1.0));
}

double redlineRpmOf(const Powertrain& p) { return p.electric ? kElectricMaxRpm : static_cast<double>(p.engine.redlineRpm); }

PowerSummary powerSummary(const Powertrain& p) {
    PowerSummary s;
    const double redline = redlineRpmOf(p);
    for (double rpm = 500.0; rpm <= redline; rpm += 50.0) {
        const double torque = torqueCurveNm(p, rpm);
        const double kw = torque * rpm * 2.0 * kPi / 60.0 / 1000.0;
        if (torque > s.peakTorqueNm) {
            s.peakTorqueNm = torque;
            s.peakTorqueRpm = rpm;
        }
        if (kw > s.peakPowerKw) {
            s.peakPowerKw = kw;
            s.peakPowerRpm = rpm;
        }
    }
    return s;
}

double wheelRadiusM(const CarConfig& car) {
    const double rim = static_cast<double>(car.rimInch);
    return rim * 0.0127 + 0.095 - 0.002 * (rim - 14.0);
}

double roadLoadN(const Powertrain& p, double speedMs) {
    const BodySpec& body = bodySpec(p.car.body);
    const double rolling = speedMs > 0.1 ? kRollingResistance * static_cast<double>(p.car.massKg) * kGravity : 0.0;
    // La traînée suit la masse volumique de l'air du jour : exactement 1,2 kg/m³ à 20 °C au niveau de la mer.
    const double density = kAirDensity * airDensityRatioOf(p.engine);
    return 0.5 * density * body.dragArea * speedMs * speedMs + rolling;
}

Gearing gearingOf(const Powertrain& p) {
    const BodySpec& body = bodySpec(p.car.body);
    // Changer de filtre, de taux de compression, de carburant ou de came ne change pas la boîte : elle reste celle du moteur
    // d'origine. Ni monter en altitude : elle est choisie dans l'air de référence, pour la puissance et pour la traînée.
    Powertrain stock;
    const bool otherFilter = !p.electric && p.engine.airFilter != AirFilter::Stock;
    const bool otherAir = !referenceAirOf(p.engine);
    const bool otherCombustion = !p.electric && !combustionStock(p.engine);
    const bool otherCam = !p.electric && !camIsStock(p.engine);
    const bool otherEngine = otherFilter || otherAir || otherCombustion || otherCam;
    if (otherEngine) {
        stock = withReferenceAir(p);
        stock.engine.airFilter = AirFilter::Stock;
        stock.engine.compressionRatio = 0.0f;
        stock.engine.fuelGrade = FuelGrade::Ron95;
        if (otherCam) stock.engine = withStockCam(stock.engine);
    }
    // Ni l'échangeur, ni l'eau, ni le protoxyde non plus.
    const bool otherCharge = chargeAirActive(p);
    if (otherCharge) stock = chargeAirStock(otherEngine ? stock : p);
    const Powertrain& sized = otherEngine || otherCharge ? stock : p;
    const PowerSummary peak = powerSummary(sized);
    const bool rearOrAll = p.car.drive != Drive::Front;
    const bool allWheels = p.car.drive == Drive::All;
    const GearboxKind kind = p.electric ? GearboxKind::Reducer : p.gearbox.kind;
    const double efficiency = drivelineEfficiency(kind, p.gearbox.form, rearOrAll, allWheels);
    const double available = peak.peakPowerKw * 1000.0 * efficiency;
    // Vitesse où la puissance équilibre la résistance, par dichotomie.
    double low = 5.0, high = 150.0;
    for (int i = 0; i < 40; ++i) {
        const double middle = 0.5 * (low + high);
        (roadLoadN(sized, middle) * middle > available ? high : low) = middle;
    }
    const double radius = wheelRadiusM(p.car);
    GearboxContext context;
    context.defaultGears = body.gears;
    context.engineTorqueNm = peak.peakTorqueNm;
    context.rearOrAll = rearOrAll;
    context.allWheels = allWheels;
    context.electric = p.electric;
    // Un seul rapport, choisi pour le démarrage : il plafonne la vitesse avant la puissance.
    context.topOverall = p.electric ? kElectricMaxRpm * 2.0 * kPi / 60.0 * radius / std::min(low, 58.0)
                                    : peak.peakPowerRpm * 2.0 * kPi / 60.0 * radius / low;
    Gearing g;
    g.box = designGearbox(p.gearbox, context);
    g.gears = g.box.gears;
    for (int i = 0; i < g.gears; ++i) g.overall[static_cast<std::size_t>(i)] = g.box.boxRatio[static_cast<std::size_t>(i)] * g.box.finalDrive;
    g.reverseOverall = g.box.reverseRatio * g.box.finalDrive;
    return g;
}

Performance measurePerformance(const Powertrain& p) {
    Performance out;
    constexpr double kStep = 0.01;
    // La fiche se mesure moteur chaud, comme une homologation : un départ à froid ne change pas la voiture.
    Powertrain warm = p;
    warm.engine.startTemp = StartTemp::Warm;
    DrivingModel model(warm);
    // Avec le launch control, on part comme sur une piste : pied sur le frein et au plancher, le temps que le régime et la
    // pression montent ; le chronomètre part au lâcher du frein.
    if (!p.electric && p.engine.launchRpm > 0) {
        for (int i = 0; i < static_cast<int>(kLaunchStageS / kStep); ++i) model.stepPedal(kStep, 1.0, 1.0);
    }
    // Le chronomètre part une fois la première engagée, embrayage au point de patinage.
    for (int i = 0; i < 100 && !model.meshed(); ++i) model.stepPedal(kStep, 1.0, 0.0);
    for (int i = 0; i < 40 * 100; ++i) {
        model.stepPedal(kStep, 1.0, 0.0);
        if (model.speedKmh() >= 100.0) {
            out.zeroTo100S = static_cast<double>(i) * kStep;
            break;
        }
    }
    const Gearing gearing = gearingOf(p);
    const double radius = wheelRadiusM(p.car);
    const double redline = redlineRpmOf(p);
    for (int gear = 0; gear < gearing.gears; ++gear) {
        const double ratio = gearing.overall[static_cast<std::size_t>(gear)];
        for (double kmh = 20.0; kmh <= 500.0; kmh += 0.5) {
            const double speed = kmh / 3.6;
            const double rpm = speed * ratio * 60.0 / (2.0 * kPi * radius);
            if (rpm > redline) break;
            if (torqueCurveNm(p, rpm) * ratio * gearing.box.meshEfficiency / radius >= roadLoadN(p, speed)) {
                out.topSpeedKmh = std::max(out.topSpeedKmh, kmh);
            }
        }
    }
    return out;
}

}  // namespace revforge
