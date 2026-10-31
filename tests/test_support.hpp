#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "revforge/engine_sound.hpp"

namespace test {

using Samples = std::vector<float>;
inline constexpr int kRate = 44100;

inline double rms(const Samples& x) {
    double sum = 0.0;
    for (float s : x) sum += static_cast<double>(s) * s;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

inline float peak(const Samples& x) {
    float top = 0.0f;
    for (float s : x) top = std::max(top, std::fabs(s));
    return top;
}

inline Samples slice(const Samples& x, std::size_t from, std::size_t to) {
    return Samples(x.begin() + static_cast<std::ptrdiff_t>(from), x.begin() + static_cast<std::ptrdiff_t>(to));
}

/** Régime tenu pendant `seconds`, sans la première demi-seconde. */
inline Samples steady(const revforge::EngineProfile& p, float rpm, float seconds = 3.0f, float volume = 0.7f) {
    revforge::EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(volume);
    core.setTargetRpm(rpm);
    Samples out(static_cast<std::size_t>(seconds * kRate));
    core.render(out.data(), static_cast<int>(out.size()));
    return slice(out, kRate / 2, out.size());
}

/** Montée puis descente, avec un régime actualisé à 25 Hz et arrondi comme sur un bus CAN. */
inline Samples sweep(const revforge::EngineProfile& p, int block = 1024, float volume = 1.0f) {
    revforge::EngineSound core(kRate);
    core.setProfile(p);
    core.setVolume(volume);
    Samples out(kRate * 10);
    const int perUpdate = kRate / 25;
    int done = 0;
    while (done < static_cast<int>(out.size())) {
        if (done % perUpdate == 0) {
            const float t = static_cast<float>(done) / static_cast<float>(out.size());
            const float shape = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
            const float raw = p.idleRpm + (p.redlineRpm - p.idleRpm) * shape;
            core.setTargetRpm(static_cast<float>(static_cast<int>(raw / 8.0f)) * 8.0f);
        }
        const int n = std::min({perUpdate - done % perUpdate, block, static_cast<int>(out.size()) - done});
        core.render(out.data() + done, n);
        done += n;
    }
    return out;
}

/** Plus grand écart entre deux échantillons voisins. */
inline float worstJump(const Samples& x) {
    float worst = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i) worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
    return worst;
}

inline double slopeRms(const Samples& x) {
    Samples d(x.size() - 1);
    for (std::size_t i = 0; i + 1 < x.size(); ++i) d[i] = x[i + 1] - x[i];
    return rms(d);
}

}  // namespace test
