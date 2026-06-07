// LightSensor.h – Concrete TEMT6000 light sensor on ADC1
// ADC attenuation, exponential filter, and non-blocking sample interval.
#pragma once

#include "ILightSensor.h"
#include <stdint.h>

// -- Compile-time defaults ---------------------------------------------------

/// Crossover point: raw reading below this = considered dark (falling edge).
#ifndef LIGHT_SENSOR_DEFAULT_BTD
#define LIGHT_SENSOR_DEFAULT_BTD 1200
#endif

/// Crossover point: raw reading above this = considered bright (rising edge).
#ifndef LIGHT_SENSOR_DEFAULT_DTB
#define LIGHT_SENSOR_DEFAULT_DTB 2800
#endif

/// ADC input range in volts. Valid: 1.1, 1.5, 2.2, 3.3.
/// 3.3 V covers the TEMT6000 full output range (0–VCC).
#ifndef LIGHT_SENSOR_DEFAULT_ATTENUATION
#define LIGHT_SENSOR_DEFAULT_ATTENUATION 3.3f
#endif

/// Exponential filter shift factor (1–6).
///  Each increment doubles the effective smoothing window.
///  Response times at the default 20 ms raw reading interval:
///   1: τ≈0.04s  95%≈0.12s  fastest response, least smoothing
///   2: τ≈0.08s  95%≈0.24s  response in under 0.25s, still smooth (default)
///   3: τ≈0.16s  95%≈0.48s  moderate smoothing
///   4: τ≈0.32s  95%≈0.96s  heavy smoothing
///   5: τ≈0.64s  95%≈1.9s   very heavy smoothing, ambient-only
///   6: τ≈1.28s  95%≈3.8s   extreme smoothing, glacial changes only
///  Lower = faster to react to light changes but more noise.
///  Higher = smoother signal but slower to react.
#ifndef LIGHT_SENSOR_FILTER_SHIFT
#define LIGHT_SENSOR_FILTER_SHIFT 2
#endif

/// How often a new raw ADC reading is taken (ms). At 20 ms the filter gets
/// ~50 samples/s and 95% response in 0.24s at shift 2 — near-instant.
#ifndef LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS
#define LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS 20
#endif

/// Fixed-point scale for the filter accumulator (raw << FILTER_SCALE).
#define LIGHT_SENSOR_FILTER_SCALE 8

// ---------------------------------------------------------------------------

class LightSensor final : public ILightSensor {
public:
    LightSensor(int pin,
                uint16_t brightToDark = LIGHT_SENSOR_DEFAULT_BTD,
                uint16_t darkToBright = LIGHT_SENSOR_DEFAULT_DTB,
                float attenuation = LIGHT_SENSOR_DEFAULT_ATTENUATION,
                uint8_t filterShift = LIGHT_SENSOR_FILTER_SHIFT,
                uint16_t rawReadingIntervalMs = LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS);

    // -- ILightSensor --------------------------------------------------------
    bool begin() override;

    uint16_t readRaw() const override;
    uint16_t readFiltered() override;
    LightZone readZone() override;
    LightState readState() override;

    bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) override;
    uint16_t getBrightToDarkThreshold() const override { return brightToDarkThreshold_; }
    uint16_t getDarkToBrightThreshold() const override { return darkToBrightThreshold_; }

    bool setAttenuation(float volts) override;
    float getAttenuation() const override { return attenuationVolts_; }

    bool setFilterShift(uint8_t shift) override;
    uint8_t getFilterShift() const override { return filterShift_; }

    void setRawReadingIntervalMs(uint16_t ms) override { rawReadingIntervalMs_ = ms; }
    uint16_t getRawReadingIntervalMs() const override { return rawReadingIntervalMs_; }

private:
    void applyAttenuation();

    int pin_;
    uint16_t brightToDarkThreshold_;
    uint16_t darkToBrightThreshold_;
    LightState latchedState_;

    float attenuationVolts_;
    uint8_t filterShift_;
    uint16_t rawReadingIntervalMs_;

    uint32_t filteredAccum_;
    uint32_t lastSampleMs_;
    bool seeded_;

    static constexpr float kValidAttenuations[] = {1.1f, 1.5f, 2.2f, 3.3f};
};
