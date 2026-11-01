#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "kotlin_math.hpp"

// Les briques communes aux sons du moteur et du démarreur.
namespace revforge::dsp {

inline constexpr int kSineSize = 2048;

inline const std::array<float, kSineSize + 1>& sineTable() {
    static const auto table = [] {
        std::array<float, kSineSize + 1> t{};
        for (int i = 0; i <= kSineSize; ++i) {
            t[i] = static_cast<float>(std::sin(2.0 * km::kPi * i / kSineSize));
        }
        return t;
    }();
    return table;
}

/** Sinus d'une phase exprimée en tours, par table interpolée. */
inline float sinTurns(float turns) {
    const auto& sine = sineTable();
    const float position = (turns - km::floor(turns)) * static_cast<float>(kSineSize);
    const int index = static_cast<int>(position);
    const float t = position - static_cast<float>(index);
    return sine[index] + (sine[index + 1] - sine[index]) * t;
}

/** Filtre d'état à deux pôles (trapézoïdal) : stable quand sa fréquence bouge. */
struct Svf {
    float lp = 0.0f;
    float bp = 0.0f;
    float k = 1.0f;
    float g = 0.0f;
    float a1 = 0.0f;
    float ic1 = 0.0f;
    float ic2 = 0.0f;

    void set(float hz, float q, float sampleRate) {
        g = static_cast<float>(std::tan(km::kPi * static_cast<double>(hz) / static_cast<double>(sampleRate)));
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
    }

    void process(float x) {
        const float v3 = x - ic2;
        const float v1 = a1 * (ic1 + g * v3);
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        bp = v1;
        lp = v2;
    }
};

/** Disperse l'onde sans la colorer : l'écho d'un habitacle. */
struct AllPass {
    std::vector<float> line;
    float g = 0.0f;
    int at = 0;

    void init(float sampleRate, float seconds, float gain) {
        line.assign(std::max(static_cast<int>(sampleRate * seconds), 1), 0.0f);
        g = gain;
        at = 0;
    }

    void reset() {
        std::fill(line.begin(), line.end(), 0.0f);
        at = 0;
    }

    float process(float x) {
        const float delayed = line[at];
        const float input = x + delayed * g;
        line[at] = input;
        at = at + 1 == static_cast<int>(line.size()) ? 0 : at + 1;
        return delayed - input * g;
    }
};

/** Générateur déterministe, dans [-1, 1[. */
struct Noise {
    std::uint32_t state = 0x51A7E;

    float next() {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * 4.656613e-10f;
    }
};

/** FFT complexe en place, radix 2, taille puissance de deux. Déterministe et sans allocation après `init`. */
class Fft {
public:
    void init(int size) {
        size_ = size;
        cosine_.resize(static_cast<std::size_t>(size / 2));
        sine_.resize(static_cast<std::size_t>(size / 2));
        reversed_.resize(static_cast<std::size_t>(size));
        int bits = 0;
        while ((1 << bits) < size) ++bits;
        for (int i = 0; i < size; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) {
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            }
            reversed_[static_cast<std::size_t>(i)] = r;
        }
        for (int i = 0; i < size / 2; ++i) {
            const double angle = -2.0 * km::kPi * i / size;
            cosine_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(angle));
            sine_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(angle));
        }
    }

    int size() const { return size_; }

    /** `inverse` : sans division par la taille, à la charge de l'appelant. */
    void transform(float* re, float* im, bool inverse) const {
        for (int i = 0; i < size_; ++i) {
            const int r = reversed_[static_cast<std::size_t>(i)];
            if (r > i) {
                std::swap(re[i], re[r]);
                std::swap(im[i], im[r]);
            }
        }
        const float sign = inverse ? -1.0f : 1.0f;
        for (int half = 1; half < size_; half *= 2) {
            const int step = size_ / (2 * half);
            for (int start = 0; start < size_; start += 2 * half) {
                for (int k = 0; k < half; ++k) {
                    const float wr = cosine_[static_cast<std::size_t>(k * step)];
                    const float wi = sign * sine_[static_cast<std::size_t>(k * step)];
                    const int a = start + k;
                    const int b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

private:
    int size_ = 0;
    std::vector<float> cosine_, sine_;
    std::vector<int> reversed_;
};

/**
 * Convolution uniforme partitionnée : toute une réponse impulsionnelle pour environ 1 % d'un cœur.
 * Échantillon par échantillon, avec son propre compteur : le résultat ne dépend pas de la taille des
 * blocs. Latence : une partition. Aucune allocation après `init`.
 */
class Convolver {
public:
    /** `partition` : puissance de deux. Capacité : `partition * partitions` points de réponse. */
    void init(int partition, int partitions) {
        partition_ = partition;
        maxPartitions_ = partitions;
        const int fftSize = partition * 2;
        bins_ = partition + 1;
        fft_.init(fftSize);
        const auto sized = static_cast<std::size_t>(bins_ * partitions);
        irRe_.assign(sized, 0.0f);
        irIm_.assign(sized, 0.0f);
        lineRe_.assign(sized, 0.0f);
        lineIm_.assign(sized, 0.0f);
        const auto full = static_cast<std::size_t>(fftSize);
        workRe_.assign(full, 0.0f);
        workIm_.assign(full, 0.0f);
        input_.assign(full, 0.0f);
        output_.assign(static_cast<std::size_t>(partition), 0.0f);
        reset();
    }

    void reset() {
        std::fill(lineRe_.begin(), lineRe_.end(), 0.0f);
        std::fill(lineIm_.begin(), lineIm_.end(), 0.0f);
        std::fill(input_.begin(), input_.end(), 0.0f);
        std::fill(output_.begin(), output_.end(), 0.0f);
        at_ = 0;
        head_ = 0;
    }

    /** Charge une réponse de `count` points. Sans allocation, mais à appeler hors du son : c'est long. */
    void setImpulse(const float* h, int count) {
        used_ = std::min((count + partition_ - 1) / partition_, maxPartitions_);
        const int fftSize = partition_ * 2;
        for (int p = 0; p < used_; ++p) {
            for (int i = 0; i < fftSize; ++i) {
                const int source = p * partition_ + i;
                workRe_[static_cast<std::size_t>(i)] = i < partition_ && source < count ? h[source] : 0.0f;
                workIm_[static_cast<std::size_t>(i)] = 0.0f;
            }
            fft_.transform(workRe_.data(), workIm_.data(), false);
            for (int b = 0; b < bins_; ++b) {
                const auto to = static_cast<std::size_t>(p * bins_ + b);
                irRe_[to] = workRe_[static_cast<std::size_t>(b)];
                irIm_[to] = workIm_[static_cast<std::size_t>(b)];
            }
        }
        reset();
    }

    float process(float x) {
        input_[static_cast<std::size_t>(partition_ + at_)] = x;
        const float y = output_[static_cast<std::size_t>(at_)];
        if (++at_ == partition_) {
            at_ = 0;
            block();
        }
        return y;
    }

private:
    void block() {
        const int fftSize = partition_ * 2;
        for (int i = 0; i < fftSize; ++i) {
            workRe_[static_cast<std::size_t>(i)] = input_[static_cast<std::size_t>(i)];
            workIm_[static_cast<std::size_t>(i)] = 0.0f;
        }
        fft_.transform(workRe_.data(), workIm_.data(), false);
        head_ = head_ == 0 ? maxPartitions_ - 1 : head_ - 1;
        for (int b = 0; b < bins_; ++b) {
            const auto to = static_cast<std::size_t>(head_ * bins_ + b);
            lineRe_[to] = workRe_[static_cast<std::size_t>(b)];
            lineIm_[to] = workIm_[static_cast<std::size_t>(b)];
        }
        // Somme des produits, de la partition la plus récente à la plus ancienne.
        std::fill(workRe_.begin(), workRe_.end(), 0.0f);
        std::fill(workIm_.begin(), workIm_.end(), 0.0f);
        for (int p = 0; p < used_; ++p) {
            const int slot = (head_ + p) % maxPartitions_;
            const float* ar = lineRe_.data() + slot * bins_;
            const float* ai = lineIm_.data() + slot * bins_;
            const float* br = irRe_.data() + p * bins_;
            const float* bi = irIm_.data() + p * bins_;
            float* wr = workRe_.data();
            float* wi = workIm_.data();
            for (int b = 0; b < bins_; ++b) {
                wr[b] += ar[b] * br[b] - ai[b] * bi[b];
                wi[b] += ar[b] * bi[b] + ai[b] * br[b];
            }
        }
        // Symétrie hermitienne : le signal est réel.
        for (int b = 1; b < partition_; ++b) {
            workRe_[static_cast<std::size_t>(fftSize - b)] = workRe_[static_cast<std::size_t>(b)];
            workIm_[static_cast<std::size_t>(fftSize - b)] = -workIm_[static_cast<std::size_t>(b)];
        }
        fft_.transform(workRe_.data(), workIm_.data(), true);
        const float scale = 1.0f / static_cast<float>(fftSize);
        for (int i = 0; i < partition_; ++i) {
            output_[static_cast<std::size_t>(i)] = workRe_[static_cast<std::size_t>(partition_ + i)] * scale;
            input_[static_cast<std::size_t>(i)] = input_[static_cast<std::size_t>(partition_ + i)];
        }
    }

    int partition_ = 128;
    int maxPartitions_ = 0;
    int used_ = 0;
    int bins_ = 0;
    int at_ = 0;
    int head_ = 0;
    Fft fft_;
    std::vector<float> irRe_, irIm_, lineRe_, lineIm_, workRe_, workIm_, input_, output_;
};

}  // namespace revforge::dsp
