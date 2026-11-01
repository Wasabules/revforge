#include "doctest.h"

#include "portable_math.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "revforge/driving_model.hpp"

using revforge::DrivingModel;

namespace {

constexpr double kDt = 0.05;
constexpr double kLimiterRpm = 6200.0;
// Le rupteur coupe la combustion : l'élan du moteur dépasse à peine.
constexpr double kRedlineRpm = kLimiterRpm + 60.0;

struct Sample {
    double sec, rpm, speed;
    int gear;
};

std::vector<Sample> lap() {
    DrivingModel model;
    std::vector<Sample> samples;
    const int steps = static_cast<int>(DrivingModel::kLoopSec / kDt);
    for (int i = 0; i < steps; ++i) {
        model.step(kDt);
        samples.push_back({i * kDt, model.rpm(), model.speedKmh(), model.gear()});
    }
    return samples;
}

}  // namespace

TEST_CASE("régime et vitesse sont continus") {
    const auto samples = lap();
    for (std::size_t i = 1; i < samples.size(); ++i) {
        // Embrayage ouvert, le moteur prend ou perd plusieurs milliers de tours par seconde.
        REQUIRE(std::fabs(samples[i].rpm - samples[i - 1].rpm) <= 500.0);
        REQUIRE(std::fabs(samples[i].speed - samples[i - 1].speed) <= 1.5);
    }
}

TEST_CASE("reste dans les limites du moteur") {
    for (const Sample& s : lap()) {
        REQUIRE(s.rpm >= 700.0);
        REQUIRE(s.rpm <= kRedlineRpm);
    }
}

TEST_CASE("passe tous les rapports puis s'arrête") {
    const auto samples = lap();
    int topGear = 0;
    double topSpeed = 0.0;
    for (const Sample& s : samples) {
        topGear = std::max(topGear, s.gear);
        topSpeed = std::max(topSpeed, s.speed);
    }
    CHECK(topGear == 5);
    CHECK(topSpeed > 110.0);
    CHECK(samples.back().speed == doctest::Approx(0.0).epsilon(0.01));
    CHECK(samples.back().gear == 0);
}

TEST_CASE("le démarrage ne s'étouffe pas") {
    bool seen = false;
    for (const Sample& s : lap()) {
        if (s.sec >= 6.0 && s.sec <= 10.0 && s.gear == 1) {
            seen = true;
            CHECK(s.rpm > 1300.0);
        }
    }
    CHECK(seen);
}

TEST_CASE("la boîte n'hésite pas") {
    const auto samples = lap();
    double lastShift = -100.0;
    int lastDirection = 0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const int a = samples[i - 1].gear, b = samples[i].gear;
        if (a == b || a == 0 || b == 0) continue;
        const int direction = b > a ? 1 : -1;
        if (lastDirection != 0 && direction != lastDirection) CHECK(samples[i].sec - lastShift >= 2.5);
        lastShift = samples[i].sec;
        lastDirection = direction;
    }
}

TEST_CASE("lever le pied ne fait pas monter un rapport") {
    const auto samples = lap();
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].sec >= 26.0 && samples[i].sec <= 30.0) CHECK(samples[i].gear <= samples[i - 1].gear);
    }
}

TEST_CASE("un passage de rapport fait chuter le régime, puis il remonte") {
    const auto samples = lap();
    std::size_t shift = 1;
    while (!(samples[shift].gear == 2 && samples[shift - 1].gear == 1)) ++shift;
    double lowest = samples[shift].rpm;
    for (std::size_t i = shift; i < shift + 20; ++i) lowest = std::min(lowest, samples[i].rpm);
    CHECK(lowest < samples[shift].rpm);
    double highest = 0.0;
    for (const Sample& s : samples) {
        if (s.sec >= 36.0 && s.sec <= 55.0) highest = std::max(highest, s.rpm);
    }
    CHECK(highest > 5000.0);
}

TEST_CASE("pédale : à fond, la voiture accélère et passe les rapports") {
    DrivingModel model;
    int topGear = 0;
    for (int i = 0; i < 30 * 20; ++i) {
        model.stepPedal(kDt, 1.0, 0.0);
        topGear = std::max(topGear, model.gear());
        REQUIRE(model.rpm() <= kRedlineRpm);
    }
    CHECK(topGear >= 4);
    CHECK(model.speedKmh() > 100.0);
}

TEST_CASE("pédale : le frein arrête la voiture, le moteur revient au ralenti") {
    DrivingModel model;
    for (int i = 0; i < 12 * 20; ++i) model.stepPedal(kDt, 1.0, 0.0);
    REQUIRE(model.speedKmh() > 50.0);
    for (int i = 0; i < 20 * 20; ++i) model.stepPedal(kDt, 0.0, 1.0);
    CHECK(model.speedKmh() == doctest::Approx(0.0).epsilon(0.01));
    CHECK(model.gear() == 0);
    CHECK(model.rpm() < 1000.0);
}

TEST_CASE("point mort : un coup de gaz monte vite et revient au ralenti") {
    DrivingModel model;
    const double dt = 128.0 / 48000.0;
    double top = 0.0;
    for (int i = 0; i < static_cast<int>(0.5 / dt); ++i) {
        model.stepNeutral(dt, 1.0);
        top = std::max(top, model.rpm());
    }
    CHECK(top > 3500.0);
    for (int i = 0; i < static_cast<int>(2.5 / dt); ++i) model.stepNeutral(dt, 0.0);
    CHECK(model.rpm() < 1000.0);
    CHECK(model.speedKmh() == 0.0);
}

TEST_CASE("point mort : le rupteur coupe sans jamais dépasser") {
    DrivingModel model;
    const double dt = 128.0 / 48000.0;
    double lowAfterLimit = 1e9;
    bool reached = false;
    for (int i = 0; i < static_cast<int>(3.0 / dt); ++i) {
        model.stepNeutral(dt, 1.0);
        REQUIRE(model.rpm() <= kRedlineRpm);
        if (model.rpm() >= kLimiterRpm) reached = true;
        if (reached) lowAfterLimit = std::min(lowAfterLimit, model.rpm());
    }
    CHECK(reached);
    CHECK(lowAfterLimit < kLimiterRpm - 100.0);
}

// Le calcul portable doit rester juste, et ne jamais repasser par la bibliothèque C dans DrivingModel.
TEST_CASE("les mathématiques portables restent justes") {
    for (double x = -40.0; x <= 40.0; x += 0.0173) {
        CHECK(revforge::portable::sin(x) == doctest::Approx(std::sin(x)).epsilon(1e-9));
        CHECK(revforge::portable::cos(x) == doctest::Approx(std::cos(x)).epsilon(1e-9));
    }
    for (double x = 0.05; x <= 200.0; x *= 1.17) {
        CHECK(revforge::portable::pow(x, 1.13) == doctest::Approx(std::pow(x, 1.13)).epsilon(1e-12));
        CHECK(revforge::portable::log(x) == doctest::Approx(std::log(x)).epsilon(1e-12));
    }
}
