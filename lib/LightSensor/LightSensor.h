// LightSensor.h – Dual-EMA edge-detection TEMT6000 light sensor on ADC1
#pragma once

#include "ILightSensor.h"
#include <stdint.h>

// -- Compile-time defaults ---------------------------------------------------

/// Flank amplitude required to toggle state.
#ifndef LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD
#define LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD 150
#endif

/// Fast EMA weight (numerator, divisor below). 400/1000 = 40%.
#ifndef LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT
#define LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT 400
#endif

/// Trend EMA weight (numerator, divisor below). 20/1000 = 2%.
#ifndef LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT
#define LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT 20
#endif

/// ADC input range in volts. Valid: 1.1, 1.5, 2.2, 3.3.
#ifndef LIGHT_SENSOR_DEFAULT_ATTENUATION
#define LIGHT_SENSOR_DEFAULT_ATTENUATION 3.3f
#endif

/// How often a new raw ADC reading is taken (ms).
#ifndef LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS
#define LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS 20
#endif

/// Fast EMA divisor (compile-time constant, not exposed to CLI).
#ifndef LIGHT_SENSOR_FAST_EMA_DIVISOR
#define LIGHT_SENSOR_FAST_EMA_DIVISOR 1000
#endif

/// Trend EMA divisor (compile-time constant, not exposed to CLI).
#ifndef LIGHT_SENSOR_TREND_EMA_DIVISOR
#define LIGHT_SENSOR_TREND_EMA_DIVISOR 1000
#endif

// ---------------------------------------------------------------------------

/// Gate for ULP wake code path. Set to 1 once hardware-validated.
#ifndef LIGHT_SENSOR_WAKE_ENABLED
#define LIGHT_SENSOR_WAKE_ENABLED 0
#endif

class LightSensor final : public ILightSensor {
public:
    LightSensor(int pin,
                float attenuation = LIGHT_SENSOR_DEFAULT_ATTENUATION,
                uint16_t rawReadingIntervalMs = LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS);

    // -- ILightSensor --------------------------------------------------------
    bool begin() override;
    void poll() override;
    uint16_t readRaw() const override;
    bool isLightOn() const override { return isLightOn_; }

    bool setBaseThreshold(int32_t threshold) override;
    int32_t getBaseThreshold() const override { return baseThreshold_; }

    bool setEmaWeights(int32_t fastWeight, int32_t trendWeight) override;
    int32_t getEmaFastWeight() const override { return fastWeight_; }
    int32_t getEmaTrendWeight() const override { return trendWeight_; }

    bool setAttenuation(float volts) override;
    float getAttenuation() const override { return attenuationVolts_; }

    void setRawReadingIntervalMs(uint16_t ms) override { rawReadingIntervalMs_ = ms; }
    uint16_t getRawReadingIntervalMs() const override { return rawReadingIntervalMs_; }

    int32_t getFastEma() const override { return fastEma_; }
    int32_t getTrendEma() const override { return trendEma_; }
    int32_t getFlank() const override { return fastEma_ - trendEma_; }

    void configureUlpWake() override;

private:
    void applyAttenuation();

    int pin_;
    bool isLightOn_;
    int32_t baseThreshold_;
    int32_t fastWeight_;
    int32_t trendWeight_;
    int32_t fastEma_;
    int32_t trendEma_;
    float attenuationVolts_;
    uint16_t rawReadingIntervalMs_;
    uint32_t lastSampleMs_;

    static constexpr float kValidAttenuations[] = {1.1f, 1.5f, 2.2f, 3.3f};
};
