#pragma once

#include "revforge/engine_config.hpp"

namespace revforge {

/**
 * L'injection : directe ou indirecte sur un moteur à essence, à pompe mécanique ou à rampe commune sur un diesel, et la
 * recirculation des gaz d'échappement. Des fonctions pures, qui reçoivent ce qui vient d'ailleurs (température d'eau,
 * carburant, air de charge) en paramètres : voir « L'injection » dans docs/modele.md. Pas d'allocation, et pas de
 * bibliothèque mathématique en dehors de `sqrt` : ce qui sert à `DrivingModel` doit donner le même résultat partout.
 */

/** L'eau d'un moteur chaud (°C) : la référence de toute la calibration, et celle à laquelle le calculateur est réglé. */
inline constexpr double kWarmCoolantC = 90.0;

/**
 * Ce que l'injection doit savoir du carburant. Les valeurs par défaut sont celles d'une essence RON 95 : l'agent de la
 * combustion calcule celles des autres carburants (`FuelGrade`), et les passe ici.
 */
struct InjectedFuel {
    double vaporizationJPerKg = 305000.0;  // chaleur de vaporisation (Heywood, annexe D : essence, 305 kJ/kg)
    double stoichiometricAfr = 14.7;
    double densityKgM3 = 745.0;
    double lowerHeatingJPerKg = 42.9e6;
    double vaporCpJPerKgK = 1700.0;        // vapeur d'essence vers 350 K
    /** Constante de Clausius-Clapeyron de la fraction qui reste en film, L·M/R (K) : 305 kJ/kg × 0,1 kg/mol / 8,314. */
    double volatilityK = 3668.0;
};

/** Le gazole EN 590 : ce qui sert au diesel (indice de cétane, pouvoir calorifique, densité). */
struct DieselFuel {
    double cetane = 51.0;
    double densityKgM3 = 832.0;
    double lowerHeatingJPerKg = 43.1e6;
    double stoichiometricAfr = 14.5;
};

// ── Le refroidissement de la charge ──

/**
 * Ce que coûte à l'air la vaporisation de tout le carburant, à cette richesse λ (K) :
 * ΔT = (m_f/m_a)·L_v / (c_p,air + (m_f/m_a)·c_p,vap), avec m_f/m_a = 1/(AFR·λ).
 */
double vaporizationDropK(double lambda, const InjectedFuel& fuel = {});

/** Part du carburant vaporisé avant la fermeture de l'admission, par la loi en d² d'une goutte (injection directe). */
double evaporatedBeforeIvc(double rpm, double railBar);

/** Pression de la rampe d'une injection directe d'essence (bar) : 40 au ralenti, 200 à pleine charge. */
double directRailBar(double load, double rpm, double redlineRpm);

/** Le refroidissement de la charge par l'injection, pour le cliquetis. */
struct ChargeCooling {
    double atIvcK = 0.0;           // l'air, à la fermeture de l'admission
    double atTdcK = 0.0;           // les gaz frais au point mort haut, compression comprise
    double airShare = 0.0;         // part de la chaleur de vaporisation prise à l'air (le reste aux parois)
    double evaporatedShare = 1.0;  // part vaporisée avant la fermeture de l'admission
};

/**
 * Ce que l'injection retire à la température des gaz frais, selon l'alimentation. Une injection directe prend presque toute
 * sa chaleur de vaporisation à l'air ; une injection indirecte la prend surtout à la soupape et au conduit. Pour l'agent de
 * la combustion (cliquetis) : `effectiveCompression` est le taux effectif, depuis la fermeture de l'admission.
 */
ChargeCooling chargeCooling(FuelSystem system, double rpm, double load, double redlineRpm, double lambda, double effectiveCompression,
                            const InjectedFuel& fuel = {});

/**
 * Ce que l'injection directe ajoute au remplissage d'un moteur conçu pour l'injection indirecte : la charge plus froide est
 * plus dense, T/(T − ΔT), à pleine charge et à ce régime. `intakeK` : l'air d'admission (K).
 */
double directFillGain(double rpm, double redlineRpm, double intakeK, const InjectedFuel& fuel = {});

/** Couple que prend la pompe haute pression d'une injection directe (Nm) : débit de carburant × pression / rendement / ω. */
double directPumpNm(double fuelGps, double railBar, double rpm, const InjectedFuel& fuel = {});

/** L'injecteur d'une injection directe : section efficace (m²), dimensionnée sur la cylindrée d'un cylindre. */
double directNozzleAreaM2(double cylinderM3);
/** Durée d'une injection (s) : la masse, sur le débit d'un orifice, ρ·Cd·A·√(2Δp/ρ). */
double injectionSeconds(double fuelKg, double railBar, double areaM2, double densityKgM3);

// ── Le film de carburant (x-τ d'Aquino) ──

/** Part d'une essence EN 228 distillée à cette température (°C), à la pression atmosphérique. */
double distilledShare(double tempC);

/** Ce qui se dépose sur les parois (X), et le temps que met le film à repartir (τ, s). */
struct FilmParameters {
    double deposit = 0.0;
    double tauS = 1.0;
};

/**
 * Le film d'une injection indirecte, qui vise la soupape fermée : X et τ selon la température de la paroi (l'eau), la
 * pression du collecteur (en part de l'atmosphère) et la vitesse de l'air dans le conduit (m/s). Voir docs/modele.md.
 */
FilmParameters portFilmParameters(double coolantC, double manifoldShare, double portSpeedMs, const InjectedFuel& fuel = {});

/**
 * Ce que le froid change au film d'un carburateur, rapporté au moteur chaud : part déposée et constante de temps. 1 et 1 à
 * 90 °C. Le carburateur a son propre modèle d'atomisation : seule la volatilité de l'essence suit la température ici.
 */
FilmParameters coldFilmShare(double coolantC, double manifoldShare, const InjectedFuel& fuel = {});

/** Le film d'Aquino : ce qui s'y dépose, et ce qui en repart à chaque pas. Partagé par le carburateur et l'injection. */
struct WallFilm {
    double massG = 0.0;
    /** Ce qui quitte la paroi en ce moment (g/s). */
    double release(double tauS) const { return massG / tauS; }
    void advance(double toWallGps, double fromWallGps, double dt) {
        const double next = massG + (toWallGps - fromWallGps) * dt;
        massG = next < 0.0 ? 0.0 : next;
    }
};

/** Ce que voit l'injection indirecte à ce pas. */
struct PortFilmInput {
    double wantedGps = 0.0;      // carburant que demande l'air de ce pas, à la richesse visée
    double manifoldShare = 1.0;  // pression du collecteur, en part de l'atmosphère
    double portSpeedMs = 30.0;   // vitesse moyenne de l'air dans le conduit d'admission
    double coolantC = kWarmCoolantC;
};

/**
 * L'injection indirecte et son film : le calculateur compense par le modèle inverse (Aquino), avec X et τ de sa
 * calibration, à 90 °C. Moteur chaud, il tombe juste et le cylindre reçoit ce qu'il demande ; à froid, la paroi garde plus,
 * plus longtemps : le mélange s'appauvrit à l'accélération et s'enrichit au lever de pied.
 */
class PortFuelFilm {
public:
    void prime() { primed_ = false; }
    void step(const PortFilmInput& in, double dt);

    /** Carburant reçu par les cylindres, en part de celui que demande l'air : 1 si la compensation tombe juste. */
    double richnessShare() const { return richnessShare_; }
    /** Le couple que permet ce mélange, rapporté à celui du mélange visé (λ = 1). */
    double torqueShare() const { return torqueShare_; }
    double filmG() const { return film_.massG; }
    double injectedGps() const { return injectedGps_; }

private:
    WallFilm film_;
    WallFilm estimate_;  // le film tel que le calculateur le croit
    bool primed_ = false;
    double richnessShare_ = 1.0;
    double torqueShare_ = 1.0;
    double injectedGps_ = 0.0;
};

// ── Le diesel ──

/** Ce que voit la combustion d'un diesel à ce point de fonctionnement. */
struct DieselPoint {
    double rpm = 2000.0;
    double load = 1.0;             // carburant injecté, en part de la pleine charge
    double fullFuelMg = 40.0;      // carburant par cycle et par cylindre à pleine charge (mg)
    double cylinderM3 = 0.0005;
    double strokeM = 0.088;
    double redlineRpm = 4500.0;
    double intakeK = 313.0;        // air d'admission (après l'échangeur sur un turbo)
    double intakeBar = 1.0;        // pression absolue d'admission
    double compressionRatio = 18.0;
    double egrRate = 0.0;          // part de gaz recirculés dans la charge
    double lambda = 2.0;           // excès d'air global, pour l'oxygène des gaz recirculés
    bool commonRail = false;
};

/** La combustion d'un diesel, du délai d'allumage au dégagement de chaleur. */
struct DieselBurn {
    double injectionBar = 0.0;
    double delayDeg = 0.0;         // délai d'allumage de l'injection principale (°V)
    double premixShare = 0.0;      // part de l'injection principale brûlée en prémélange
    double peakRiseBarPerDeg = 0.0;  // pente de pression la plus forte (bar/°V) : le claquement en suit le carré
    double durationDeg = 0.0;      // durée de la combustion diffusive (°V)
    double efficiency = 0.0;       // degré d'isochore : rendement du dégagement, rapporté au cycle idéal
};

/** Pression d'injection (bar) : la pompe mécanique suit son régime, la rampe commune est tenue par le calculateur. */
double dieselInjectionBar(bool commonRail, double rpm, double redlineRpm, double load);
/** Délai d'allumage de Hardenberg et Hase (°V), d'après la pression (bar) et la température (K) au point mort haut. */
double ignitionDelayDeg(double tdcBar, double tdcK, double meanPistonMs, double cetane);
/** Délai physique d'un jet qui se brise (Hiroyasu) : t = 28,65·ρ_l·d/√(ρ_a·Δp) (s). */
double sprayBreakupSeconds(double nozzleM, double fuelKgM3, double airKgM3, double injectionBar);
/** Le calcul, point par point : le délai, le prémélange, le dégagement et son rendement. */
DieselBurn dieselBurn(const DieselPoint& point, const DieselFuel& fuel = {});
/** Excès d'air sous lequel le moteur fume, selon la pression d'injection : la limite de fumée. */
double smokeLimitLambda(double injectionBar);
/**
 * Le couple à pleine charge d'une rampe commune, rapporté à la pompe mécanique d'origine, à ce régime : plus de carburant
 * avant la fumée (pression d'injection plus haute), et un dégagement plus court. 1 pour la pompe mécanique.
 */
double commonRailTorqueShare(const DieselPoint& fullLoad);
/** Pente de pression la plus forte, rapportée à la pompe mécanique au même point : le claquement suit son carré. */
double dieselKnockShare(const DieselPoint& point);

// ── La recirculation des gaz d'échappement (EGR) ──

/** Part de gaz recirculés selon la charge et le régime : rien au ralenti d'un essence, ni à pleine charge. */
double egrRate(bool diesel, double load, double rpm, double redlineRpm);
/** Vitesse de flamme laminaire diluée, en part de celle sans gaz recirculés (Metghalchi et Keck, 1982). */
double egrFlameSpeedShare(double rate);
/** Allongement de la combustion d'un moteur à essence : en S_L^−1/3 (Heywood, durée de combustion rapide). */
double egrBurnStretch(double rate);
/**
 * Ce que la dilution retire aux pertes aux parois, en part de celles sans recirculation (Woschni : h ∝ T^−0,53). `dilution` : la
 * part de gaz inerte dans la charge d'un essence, la part d'oxygène en moins d'un diesel (x/λ), qui brûle à la flamme.
 */
double egrWallLossShare(double dilution);
/** Rendement indiqué avec recirculation, en part de celui sans, pompage à part. */
double egrEfficiencyShare(bool diesel, double rate, double lambda);
/** Écart du bruit de combustion (dB) : la pente de pression suit la vitesse de combustion. */
double egrCombustionNoiseDb(bool diesel, double rate, double lambda);

/** Ce que le bruit de combustion doit savoir du moteur. */
struct CombustionNoiseEngine {
    bool diesel = false;
    bool commonRail = false;
    bool egr = false;
    bool charged = false;
    double cylinderM3 = 0.0005;
    double strokeM = 0.088;
    double redlineRpm = 4500.0;
    double boostBar = 1.0;
};
/**
 * L'écart du bruit de combustion (dB) à cette charge et à ce régime, face au même moteur à pompe mécanique et sans
 * recirculation : la pente de pression d'un diesel, la vitesse de flamme d'un essence. Pas pour chaque échantillon : le son en
 * tire une table à chaque changement de moteur.
 */
double combustionNoiseDb(const CombustionNoiseEngine& engine, double load, double rpm);

// ── Pour les vues et la fiche ──

/** Ce que l'injection change à ce moteur, pour la fiche : un résumé chiffré. */
struct InjectionSummary {
    double coolingK = 0.0;          // injection directe : refroidissement de la charge à pleine charge
    double fillGainShare = 0.0;     // injection directe : remplissage en plus
    double railBar = 0.0;           // pression d'injection à pleine charge, au régime du couple maximal
    double lowEndTorqueShare = 0.0; // rampe commune : couple en plus à 1500 tr/min
    double knockDb = 0.0;           // rampe commune : claquement au ralenti, face à la pompe mécanique
    double egrShare = 0.0;          // recirculation : part la plus forte, à charge partielle
};
InjectionSummary injectionSummary(const EngineConfig& engine);

/** Une injection directe d'essence : sur un moteur à essence, rotatif compris. */
inline bool directInjected(const EngineConfig& e) { return e.fuelSystem == FuelSystem::Direct && !e.burnsDiesel(); }
/** Une rampe commune sur un diesel. */
inline bool commonRailed(const EngineConfig& e) { return e.burnsDiesel() && e.dieselInjection == DieselInjection::CommonRail; }
/** Un filtre à particules : sur un diesel, ou une injection directe d'essence, les seuls à faire de la suie. */
inline bool particulateFiltered(const EngineConfig& e) { return e.particulateFilter && (e.burnsDiesel() || directInjected(e)); }
/** La recirculation : un rotatif n'en a pas (il garde ses gaz brûlés d'une chambre à l'autre). */
inline bool recirculated(const EngineConfig& e) { return e.egr && !isRotary(e); }

}  // namespace revforge
