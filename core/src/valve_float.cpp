#include "revforge/valve_float.hpp"

#include <algorithm>
#include <cmath>

#include "portable_math.hpp"
#include "revforge/cam.hpp"
#include "revforge/valvetrain.hpp"

namespace revforge {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kSteelE = 210e9;
constexpr double kSteelDensity = 7850.0;
constexpr double kRpmToRad = 2.0 * kPi / 60.0;
constexpr double kDegToRad = kPi / 180.0;

// ── Les pièces, en part de la masse de la soupape (voir docs/modele.md, « Les culbuteurs et l'affolement ») ──
// Une soupape pèse comme une tête pleine de 6,5 mm d'épaisseur équivalente, queue comprise : 44 g à 33 mm, 103 g à 50,8 mm,
// l'ordre des soupapes d'un 16 soupapes et d'un V8 à culbuteurs (masses publiées, de mémoire).
constexpr double kValveThicknessM = 0.0065;
// Coupelle et clavettes ; ressort entier, dont un tiers suit la soupape ; godet d'un double arbre. Supposés, d'après des
// pièces courantes : 8 g et 40 g sous une soupape de 44 g, 18 g et 90 g sous une de 105 g, un godet de 30 g.
constexpr double kRetainerShare = 0.18;
constexpr double kSpringShare = 0.9;
constexpr double kSpringMoving = 1.0 / 3.0;
constexpr double kBucketShare = 0.7;
// Le culbuteur d'un simple arbre (fonte ou alliage) et celui d'un moteur à culbuteurs (tôle emboutie), le poussoir hydraulique,
// dont le plongeur suit la tige quand le corps est poussé par la came. Supposés : 60 g, 90 g et 25 g sous une soupape de 105 g.
constexpr double kSohcRockerShare = 0.8;
constexpr double kOhvRockerShare = 0.6;
constexpr double kLifterShare = 0.85;
constexpr double kPlungerShare = 0.3;
// Rapports de culbuteur : celui que dessine `singleCamValves` (engineLayout.ts) ; 1,6 entre le 1,5 d'un Small Block Chevrolet
// et le 1,7 d'un LS (Ford Windsor : 1,6).
constexpr double kSohcRatio = 1.4;
constexpr double kOhvRatio = 1.6;
// Raideurs, ramenées à leur bout (N/m) : came, godet et culasse ; bras de culbuteur ; colonne d'huile d'un poussoir hydraulique.
// Ordres de grandeur supposés, qui donnent 2,2 kHz à un double arbre et 700 Hz à un V8 à culbuteurs (de 500 à 900 Hz publiés).
constexpr double kCamStiffness = 30e6;
constexpr double kRockerStiffness = 12e6;
constexpr double kLifterStiffness = 40e6;
// La queue de soupape : un diamètre de 0,16 fois la tête, une longueur de 2,8 fois (5,5 sur 100 mm sous 33 mm, 8 sur 125 sous 51).
constexpr double kStemShare = 0.16;
constexpr double kStemLength = 2.8;
// La tige : 2,1 fois la course de long (183 mm sur un Small Block de 88 mm de course, 188 sur un LS de 92), un tube de
// 0,155 fois la tête de diamètre (7,9 mm sous 51 mm), 6 mm au moins, alésé à 60 %.
constexpr double kPushrodPerStroke = 2.1;
constexpr double kPushrodPerHead = 0.155;
constexpr double kPushrodMinM = 0.006;
constexpr double kPushrodBore = 0.6;

// ── La came ──
// La levée d'origine : `kLiftPerBore` de valvetrain.cpp. La durée : de `kEvo` à `kEvc` dans le son.
constexpr double kStockLiftPerBore = 0.12;
constexpr double kStockDurationDeg = 240.0;
// La loi de levée : une accélération en demi-sinus sur chaque flanc, puis une décélération en demi-sinus sur les 60 % du
// milieu de l'ouverture (supposé : une came de route décélère sur la moitié à deux tiers de son ouverture, deux fois moins
// fort qu'elle n'accélère). La levée vaut v_max · T · ((1 − f)/4 + f/π), et la décélération au nez π · v_max / (f · T).
constexpr double kDecelShare = 0.6;
constexpr double kLiftShape = 0.25 * (1.0 - kDecelShare) + kDecelShare / kPi;
// La rampe de fermeture : 0,013 mm par degré de vilebrequin à la soupape (0,0005 pouce, l'ordre des rampes de route).
constexpr double kRampMmPerDeg = 0.013;

// ── Le ressort ──
// Le constructeur le dimensionne en cinématique : la force à pleine levée dépasse de 25 % l'inertie à la zone rouge. Assez
// pour retrouver les affolements connus, de 5 à 15 % au-dessus du rupteur (LS1 : de 6300 à 6500 tr/min pour 6000).
constexpr double kSpringMargin = 1.25;
// Force à pleine levée sur force posée : 200 et 80 lb sur un Small Block, 280 et 105 sur un LS6.
constexpr double kOpenToSeat = 2.5;
// Des ressorts raides : 30 % de force en plus, posés comme à pleine levée (24 à 50 % sur les ressorts de préparation courants).
constexpr double kStiffShare = 1.3;
// Le rebond sur le siège : coefficient de restitution supposé, acier sur siège rapporté (de 0,3 à 0,6).
constexpr double kRestitution = 0.5;

// ── Le frottement ──
// Celui du contact came-poussoir d'un double arbre : C·(1 + 500/N)·(soupapes)/course, C = 133 (voir `sohcFrictionDeltaBar`).
// Le simple arbre en a 50 % de plus. Sa charge suit la force du ressort sur la came, donc la masse ramenée et le rapport.
constexpr double kFollowerKpaMm = 133.0 * 4.0;
constexpr double kSohcFollowerShare = 1.5;
// La pression moyenne indiquée à laquelle `sohcBreathingRatio` rapporte ce qu'il retire (`kExhaustLossReferenceBar`).
constexpr double kIndicatedReferenceBar = 12.0;

// ── Le cylindre ──
// La bielle fait 1,6 fois la course (tools/valve_filling.py, engineLayout.ts). L'admission ferme 50° après le point mort bas
// (`kIvc` du son : 590° depuis l'explosion).
constexpr double kRodPerStroke = 1.6;
constexpr double kStockIvcDeg = 50.0;
// Au-delà, la soupape n'est plus tenue : une surcharge plafonnée, pour rester fini près de la résonance de la chaîne.
constexpr double kMaxOverload = 20.0;

double stockCompression(const EngineConfig& e) {
    if (e.compressionRatio > 0.0f) return static_cast<double>(e.compressionRatio);
    // La même règle que le démarreur (`EngineConfig::toProfile`).
    return e.burnsDiesel() ? 18.0 : (e.aspiration != Aspiration::Natural ? 9.5 : 10.5);
}

/** Le volume du cylindre à `deg` degrés du point mort haut, en parts de cylindrée. */
double cylinderVolume(double deg, double compression) {
    const double a = deg * kDegToRad;
    const double crank = 0.5;
    const double rod = kRodPerStroke;
    const double s = crank * portable::sin(a);
    return 1.0 / (compression - 1.0) + crank * (1.0 - portable::cos(a)) + rod - std::sqrt(rod * rod - s * s);
}

/** La raideur de la queue de soupape, en traction-compression. */
double stemStiffness(double headM) {
    const double stem = kStemShare * headM;
    return kSteelE * kPi / 4.0 * stem * stem / (kStemLength * headM);
}

/** Un bras de rapport R, pivot entre ses bouts, de masse uniforme : I / L_soupape² = m (R² − R + 1) / (3 R²). */
double rockerAtValve(double rockerKg, double ratio) { return rockerKg * (ratio * ratio - ratio + 1.0) / (3.0 * ratio * ratio); }

double inv(double x) { return 1.0 / x; }

/** La décélération au nez, par (tr/min)² (m/s²). */
double decelPerRpm2(const CamLobe& cam) {
    const double duration = cam.durationDeg * kDegToRad;
    return kPi * (cam.liftMm / 1000.0) / (kDecelShare * kLiftShape * duration * duration) * kRpmToRad * kRpmToRad;
}

/** (f_d / f_n)² par (tr/min)² : f_d, la fréquence du demi-sinus de décélération, 1 / (2 f T), avec T = D / ω. */
double stiffeningPerRpm2(const ValvetrainParts& parts, const CamLobe& cam) {
    const double hzPerRpm = kRpmToRad / (2.0 * kDecelShare * cam.durationDeg * kDegToRad);
    const double share = hzPerRpm / parts.naturalHz;
    return share * share;
}

}  // namespace

bool hasValves(const EngineConfig& e) { return e.layout != Layout::Rotary && e.cycle == EngineCycle::FourStroke; }

CamLobe stockCamLobe(double boreMm) { return CamLobe{kStockLiftPerBore * boreMm, kStockDurationDeg}; }

CamLobe camLobeOf(const EngineConfig& e) {
    // Intégration : le profil de la came (cam.hpp), à 1 mm de levée comme la durée d'origine (240°, de `kEvo` à `kEvc`). Sur une came
    // fixe, c'est exactement la levée et la durée réglées ; sur deux profils, celui de la bascule, plus haut et plus long.
    if (camIsStock(e)) return stockCamLobe(static_cast<double>(e.boreMm));
    const CamProfile profile = highCamProfileOf(e);
    return CamLobe{profile.liftMm, profile.durationDeg};
}

ValvetrainParts valvetrainParts(Camshaft valvetrain, double boreMm, double strokeMm) {
    ValvetrainParts p;
    p.headMm = valveTrainOf(valvetrain, boreMm).intakeMm;
    const double head = p.headMm / 1000.0;
    p.valveKg = kSteelDensity * kPi / 4.0 * head * head * kValveThicknessM;
    p.retainerKg = kRetainerShare * p.valveKg;
    p.springKg = kSpringShare * p.valveKg;
    const double valveSide = p.valveKg + p.retainerKg + kSpringMoving * p.springKg;
    const double stem = stemStiffness(head);
    switch (valvetrain) {
        case Camshaft::Dohc:
            p.ratio = 1.0;
            p.bucketKg = kBucketShare * p.valveKg;
            p.effectiveKg = valveSide + p.bucketKg;
            p.stiffnessNPerM = inv(inv(stem) + inv(kCamStiffness));
            break;
        case Camshaft::Sohc:
            p.ratio = kSohcRatio;
            p.rockerKg = kSohcRockerShare * p.valveKg;
            p.rockerAtValveKg = rockerAtValve(p.rockerKg, p.ratio);
            p.effectiveKg = valveSide + p.rockerAtValveKg;
            p.stiffnessNPerM = inv(inv(stem) + inv(kRockerStiffness) + p.ratio * p.ratio / kCamStiffness);
            break;
        case Camshaft::Ohv: {
            p.ratio = kOhvRatio;
            p.rockerKg = kOhvRockerShare * p.valveKg;
            p.rockerAtValveKg = rockerAtValve(p.rockerKg, p.ratio);
            p.lifterKg = kLifterShare * p.valveKg;
            p.pushrodLengthM = kPushrodPerStroke * strokeMm / 1000.0;
            p.pushrodOuterM = std::max(kPushrodPerHead * head, kPushrodMinM);
            const double inner = kPushrodBore * p.pushrodOuterM;
            const double area = kPi / 4.0 * (p.pushrodOuterM * p.pushrodOuterM - inner * inner);
            p.pushrodKg = kSteelDensity * area * p.pushrodLengthM;
            // Ce qui est côté came se voit de la soupape divisé par le carré du rapport ; ses raideurs, multipliées.
            const double r2 = p.ratio * p.ratio;
            p.effectiveKg = valveSide + p.rockerAtValveKg + (p.lifterKg + p.pushrodKg) / r2;
            const double pushrod = kSteelE * area / p.pushrodLengthM;
            p.stiffnessNPerM = inv(inv(stem) + inv(kRockerStiffness) + r2 / pushrod + r2 / kLifterStiffness);
            break;
        }
    }
    p.naturalHz = std::sqrt(p.stiffnessNPerM / p.effectiveKg) / (2.0 * kPi);
    return p;
}

double noseDecelerationMs2(const CamLobe& cam, double rpm) { return decelPerRpm2(cam) * rpm * rpm; }

double valvetrainStiffening(const ValvetrainParts& parts, const CamLobe& cam, double rpm) { return stiffeningPerRpm2(parts, cam) * rpm * rpm; }

ValveSpring stockValveSpring(const ValvetrainParts& parts, const CamLobe& stock, double redlineRpm) {
    const double open = kSpringMargin * parts.effectiveKg * noseDecelerationMs2(stock, redlineRpm);
    ValveSpring spring;
    spring.preloadN = open / kOpenToSeat;
    spring.rateNPerMm = (open - spring.preloadN) / stock.liftMm;
    return spring;
}

ValveSpring valveSpringOf(const EngineConfig& e) {
    const ValvetrainParts parts = valvetrainParts(e.valvetrain(), static_cast<double>(e.boreMm), static_cast<double>(e.strokeMm()));
    ValveSpring spring = stockValveSpring(parts, stockCamLobe(static_cast<double>(e.boreMm)), static_cast<double>(e.redlineRpm));
    if (e.valveSprings == ValveSprings::Stiff) {
        spring.preloadN *= kStiffShare;
        spring.rateNPerMm *= kStiffShare;
    }
    return spring;
}

double valveFloatRpm(const ValvetrainParts& parts, const CamLobe& cam, const ValveSpring& spring) {
    // m · α · N² / (1 − β · N²) = F au nez, d'où N² = F / (m · α + β · F).
    const double force = spring.forceN(cam.liftMm);
    return std::sqrt(force / (parts.effectiveKg * decelPerRpm2(cam) + stiffeningPerRpm2(parts, cam) * force));
}

double valveFloatRpm(const EngineConfig& e) {
    if (!hasValves(e)) return 0.0;
    const ValvetrainParts parts = valvetrainParts(e.valvetrain(), static_cast<double>(e.boreMm), static_cast<double>(e.strokeMm()));
    return valveFloatRpm(parts, camLobeOf(e), valveSpringOf(e));
}

ValvetrainProfile valvetrainProfileOf(const EngineConfig& e) {
    ValvetrainProfile v;
    if (!hasValves(e)) return v;
    const double bore = static_cast<double>(e.boreMm);
    const double stroke = static_cast<double>(e.strokeMm());
    const ValvetrainParts parts = valvetrainParts(e.valvetrain(), bore, stroke);
    const CamLobe cam = camLobeOf(e);
    const ValveSpring spring = valveSpringOf(e);
    v.floatRpm = valveFloatRpm(parts, cam, spring);
    v.stiffening = valvetrainStiffening(parts, cam, v.floatRpm);
    v.decelSpanDeg = kDecelShare * cam.durationDeg;
    v.maxLateDeg = 0.5 * (1.0 - kDecelShare) * cam.durationDeg;
    v.flankMsPerRpm = cam.liftMm / 1000.0 / (kLiftShape * cam.durationDeg * kDegToRad) * kRpmToRad;
    v.rampMsPerRpm = kRampMmPerDeg / 1000.0 * 6.0;
    v.bounceSPerMs = 2.0 * kRestitution * parts.effectiveKg / spring.preloadN;
    v.compression = stockCompression(e);
    // Une came plus longue ferme d'autant plus tard, à cheval sur son axe (la came réglable la décale) : ce qui compte ici.
    v.ivcAfterBdcDeg = kStockIvcDeg + 0.5 * (cam.durationDeg - kStockDurationDeg);
    if (e.valvetrain() == Camshaft::Ohv) {
        // Le choc au poussoir : à chaque ouverture, le corps poussé par la came rattrape la course de fuite du plongeur, à la
        // vitesse de la rampe divisée par le rapport, et frappe tout ce qui est au-dessus (vu du poussoir : multiplié par R²).
        // Son impulsion μ·v, face à celle du culbuteur d'un simple arbre qui rattrape son jeu sur la queue, à la même tête.
        const double r2 = parts.ratio * parts.ratio;
        const double valveSide = parts.valveKg + parts.retainerKg + kSpringMoving * parts.springKg;
        const double body = (1.0 - kPlungerShare) * parts.lifterKg;
        const double rest = kPlungerShare * parts.lifterKg + parts.pushrodKg + r2 * (parts.rockerAtValveKg + valveSide);
        const double pushrodImpulse = body * rest / (body + rest) / parts.ratio;
        const ValvetrainParts single = valvetrainParts(Camshaft::Sohc, bore, stroke);
        const double singleSide = single.valveKg + single.retainerKg + kSpringMoving * single.springKg;
        const double rockerImpulse = single.rockerAtValveKg * singleSide / (single.rockerAtValveKg + singleSide);
        v.pushrodTick = pushrodImpulse / rockerImpulse;
        // Le deuxième mode de flexion de la tige, appuyée à ses deux bouts : 4 fois (π / 2L²)·√(EI / ρA).
        const double outer = parts.pushrodOuterM;
        const double inner = kPushrodBore * outer;
        const double area = kPi / 4.0 * (outer * outer - inner * inner);
        const double inertia = kPi / 64.0 * (outer * outer * outer * outer - inner * inner * inner * inner);
        const double length = parts.pushrodLengthM;
        v.pushrodHz = 4.0 * kPi / (2.0 * length * length) * std::sqrt(kSteelE * inertia / (kSteelDensity * area));
    }
    return v;
}

double floatOverload(const ValvetrainProfile& v, double rpm) {
    if (v.floatRpm <= 0.0 || rpm <= v.floatRpm) return 0.0;
    const double x = (rpm / v.floatRpm) * (rpm / v.floatRpm);
    const double room = 1.0 - v.stiffening * x;
    if (room <= (1.0 - v.stiffening) / kMaxOverload) return kMaxOverload;
    return std::min(x * (1.0 - v.stiffening) / room, kMaxOverload);
}

double floatLateDeg(const ValvetrainProfile& v, double rpm) {
    const double overload = floatOverload(v, rpm);
    if (overload <= 1.0) return 0.0;
    // La soupape ne décélère plus qu'à 1/ρ de ce que demande la came : elle reste en l'air ρ fois plus longtemps.
    return std::min(v.decelSpanDeg * (overload - 1.0), v.maxLateDeg);
}

double floatTrappedShare(const ValvetrainProfile& v, double rpm) {
    const double late = floatLateDeg(v, rpm);
    if (late <= 0.0) return 1.0;
    // Le piston remonte pendant que l'admission reste ouverte : il repousse ce qu'il avait aspiré.
    const double closing = 180.0 + v.ivcAfterBdcDeg;
    const double lateClosing = std::min(closing + late, 355.0);
    return cylinderVolume(lateClosing, v.compression) / cylinderVolume(closing, v.compression);
}

double floatLandingMs(const ValvetrainProfile& v, double rpm) {
    const double overload = floatOverload(v, rpm);
    if (overload <= 1.0) return 0.0;
    // Au premier ordre, l'écart de décélération entre la soupape et la came, sur le même temps : v_flanc · (1 − 1/ρ).
    return v.flankMsPerRpm * rpm * (1.0 - 1.0 / overload);
}

double valveFloatShare(const EngineConfig& e, double rpm) {
    const double floatRpm = valveFloatRpm(e);
    if (floatRpm <= 0.0 || rpm <= floatRpm) return 1.0;
    return floatTrappedShare(valvetrainProfileOf(e), rpm);
}

double followerFrictionBar(const EngineConfig& e, double rpm) {
    if (!hasValves(e)) return 0.0;
    const double stroke = static_cast<double>(e.strokeMm());
    const double base = kFollowerKpaMm * (1.0 + 500.0 / std::fmax(rpm, 300.0)) / stroke / 100.0;
    switch (e.valvetrain()) {
        case Camshaft::Dohc: return base;
        case Camshaft::Sohc: return kSohcFollowerShare * base;
        case Camshaft::Ohv: {
            // Le ressort suit la masse ramenée (il est dimensionné sur elle), et la came le voit multiplié par le rapport. Deux
            // soupapes comme le simple arbre : on part de lui, et de ce qui change d'une distribution à l'autre.
            const double bore = static_cast<double>(e.boreMm);
            const ValvetrainParts pushrods = valvetrainParts(Camshaft::Ohv, bore, stroke);
            const ValvetrainParts single = valvetrainParts(Camshaft::Sohc, bore, stroke);
            return kSohcFollowerShare * base * (pushrods.effectiveKg * pushrods.ratio) / (single.effectiveKg * single.ratio);
        }
    }
    return base;
}

double ohvFrictionDeltaBar(const EngineConfig& e, double rpm) {
    if (!hasValves(e) || e.valvetrain() != Camshaft::Ohv) return 0.0;
    EngineConfig twin = e;
    twin.camshaft = Camshaft::Dohc;
    return followerFrictionBar(e, rpm) - followerFrictionBar(twin, rpm);
}

double springFrictionDeltaBar(const EngineConfig& e, double rpm) {
    if (!hasValves(e) || e.valveSprings != ValveSprings::Stiff) return 0.0;
    return (kStiffShare - 1.0) * followerFrictionBar(e, rpm);
}

double valvetrainBrakeShare(const EngineConfig& e, double rpm) {
    if (!hasValves(e)) return 1.0;
    double extra = springFrictionDeltaBar(e, rpm);
    // `sohcBreathingRatio` retire déjà au couple le culbuteur d'un simple arbre : seul ce que les culbuteurs ont en plus reste.
    if (e.valvetrain() == Camshaft::Ohv) extra += ohvFrictionDeltaBar(e, rpm) - sohcFrictionDeltaBar(static_cast<double>(e.strokeMm()), rpm);
    if (extra <= 0.0) return 1.0;
    return 1.0 - extra / kIndicatedReferenceBar;
}

double ohvInertiaDelta(const EngineConfig& e) {
    if (!hasValves(e) || e.valvetrain() != Camshaft::Ohv) return 0.0;
    // `sohcInertiaDelta` retire un arbre par rangée ; ici, il n'en reste qu'un pour tout le moteur.
    const double banks = e.layout == Layout::Inline ? 1.0 : 2.0;
    return sohcInertiaDelta(e) * (2.0 * banks - 1.0) / banks;
}

}  // namespace revforge
