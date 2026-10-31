// Interface C du moteur pour WebAssembly : un seul moteur, piloté depuis l'AudioWorklet.
#include <new>

#include "revforge/driving_model.hpp"
#include "revforge/engine_sound.hpp"

using namespace revforge;

namespace {

constexpr int kMaxFrames = 8192;

enum Mode { kOff = 0, kNeutral = 1, kDrive = 2, kLap = 3, kManual = 4 };

EngineSound* g_sound = nullptr;
DrivingModel* g_model = nullptr;
EngineProfile g_profile;
float g_buffer[kMaxFrames];
float g_sampleRate = 48000.0f;
int g_mode = kOff;
double g_pedal = 0.0;
double g_brake = 0.0;
float g_manualRpm = 0.0f;

}  // namespace

extern "C" {

void rf_init(int sampleRate) {
    g_sampleRate = static_cast<float>(sampleRate);
    delete g_sound;
    delete g_model;
    g_sound = new EngineSound(sampleRate);
    g_model = new DrivingModel();
    g_sound->setVolume(0.7f);
    g_profile = EngineConfig{}.toProfile();
}

float* rf_buffer() { return g_buffer; }

void rf_set_config(int cylinders, int layout, int displacementCc, int boreMm, int redlineRpm, int exhaust, int aspiration, int fuel,
                   float multiplier, int pops, int release) {
    EngineConfig c;
    c.cylinders = cylinders;
    c.layout = static_cast<Layout>(layout);
    c.displacementCc = displacementCc;
    c.boreMm = boreMm;
    c.redlineRpm = redlineRpm;
    c.exhaust = static_cast<Exhaust>(exhaust);
    c.aspiration = static_cast<Aspiration>(aspiration);
    c.fuel = static_cast<Fuel>(fuel);
    c.revMultiplier = multiplier;
    c.pops = static_cast<PopLevel>(pops);
    c.turboRelease = static_cast<TurboRelease>(release);
    g_profile = c.toProfile();
    g_sound->setProfile(g_profile);
}

void rf_set_electric() {
    g_profile = electricProfile();
    g_sound->setProfile(g_profile);
}

/** Changer de mode repart d'une voiture à l'arrêt. */
void rf_set_mode(int mode) {
    if (mode == g_mode) return;
    if (mode == kLap || g_mode == kLap || g_mode == kOff) {
        g_model->~DrivingModel();
        new (g_model) DrivingModel();
    }
    g_mode = mode;
}

void rf_set_controls(float pedal, float brake) {
    g_pedal = pedal;
    g_brake = brake;
}

void rf_set_manual_rpm(float rpm) { g_manualRpm = rpm; }

void rf_set_volume(float volume) { g_sound->setVolume(volume); }

/** Écrit `frames` échantillons dans rf_buffer ; le modèle de conduite avance d'autant. */
void rf_render(int frames) {
    if (frames > kMaxFrames) frames = kMaxFrames;
    const double dt = static_cast<double>(frames) / static_cast<double>(g_sampleRate);
    float target = 0.0f;
    switch (g_mode) {
        case kNeutral:
            g_model->stepNeutral(dt, g_pedal);
            target = g_profile.fromCarRpm(static_cast<float>(g_model->rpm()));
            break;
        case kDrive:
            g_model->stepPedal(dt, g_pedal, g_brake);
            target = g_profile.fromCarRpm(static_cast<float>(g_model->rpm()));
            break;
        case kLap:
            g_model->step(dt);
            target = g_profile.fromCarRpm(static_cast<float>(g_model->rpm()));
            break;
        case kManual:
            target = g_manualRpm;
            break;
        default:
            break;
    }
    g_sound->setTargetRpm(target);
    g_sound->render(g_buffer, frames);
}

float rf_rpm() { return g_sound->targetRpm(); }
float rf_car_rpm() { return g_mode == kOff ? 0.0f : static_cast<float>(g_model->rpm()); }
float rf_speed() { return static_cast<float>(g_model->speedKmh()); }
int rf_gear() { return g_model->gear(); }
float rf_throttle() { return static_cast<float>(g_model->throttle()); }
float rf_load() { return g_sound->load(); }
float rf_boost() { return g_sound->boost(); }
float rf_from_car_rpm(float carRpm) { return g_profile.fromCarRpm(carRpm); }
float rf_idle_rpm() { return g_profile.idleRpm; }
float rf_multiplier() { return g_profile.revMultiplier; }

}  // extern "C"
