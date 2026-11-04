#include <algorithm>
#include <string>

#include "doctest.h"
#include "revforge/driving_model.hpp"
#include "revforge/powertrain.hpp"
#include "revforge/presets.hpp"

using namespace revforge;

namespace {

struct ColdRun {
    double idleLowRpm = 1e9;   // le plus bas au ralenti, une fois le moteur pris
    double driveLowRpm = 1e9;  // le plus bas en partant
    bool stalled = false;
    double kmh = 0.0;
};

/** Par ce froid, départ à froid : au démarreur ou moteur posé au ralenti, 10 s de ralenti, puis on part, pied à 30 %, 5 s. */
ColdRun coldRun(const Powertrain& base, double ambientC, bool starter) {
    Powertrain p = base;
    p.engine.startTemp = StartTemp::Cold;
    p.engine.ambientC = static_cast<float>(ambientC);
    DrivingModel model(p);
    if (starter) {
        model.halt();
        model.crank();
    }
    ColdRun run;
    for (int i = 0; i < 15000; ++i) {
        const bool driving = i >= 10000;
        if (driving) model.stepPedal(0.001, 0.3, 0.0);
        else model.stepNeutral(0.001, 0.0);
        if (i > 2000 && !driving) run.idleLowRpm = std::min(run.idleLowRpm, model.rpm());
        if (driving) run.driveLowRpm = std::min(run.driveLowRpm, model.rpm());
        run.stalled = run.stalled || model.stalled();
    }
    run.kmh = model.speedKmh();
    return run;
}

}  // namespace

TEST_CASE("démarrage à froid : chaque préréglage démarre par -20 °C, tient son ralenti et part sans caler") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t k = 0; k < count; ++k) {
        const Powertrain p = all[k].powertrain();
        if (p.electric) continue;
        for (const bool starter : {true, false}) {
            CAPTURE(std::string(all[k].id));
            CAPTURE(starter);
            const ColdRun run = coldRun(p, -20.0, starter);
            CHECK_FALSE(run.stalled);
            // Le ralenti accéléré du froid le tient au-dessus du ralenti chaud.
            CHECK(run.idleLowRpm > 0.95 * idleRpmOf(p));
            CHECK(run.driveLowRpm > 0.6 * idleRpmOf(p));
            CHECK(run.kmh > 5.0);
        }
    }
}

TEST_CASE("démarrage à froid : par -30 °C aussi, moteur posé au ralenti") {
    std::size_t count = 0;
    const Preset* all = presets(count);
    for (std::size_t k = 0; k < count; ++k) {
        const Powertrain p = all[k].powertrain();
        if (p.electric) continue;
        CAPTURE(std::string(all[k].id));
        const ColdRun run = coldRun(p, -30.0, false);
        CHECK_FALSE(run.stalled);
        CHECK(run.idleLowRpm > 0.95 * idleRpmOf(p));
    }
}
