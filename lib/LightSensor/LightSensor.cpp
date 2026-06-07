// LightSensor.cpp – Dual-EMA edge-detection TEMT6000 light sensor on ADC1
#include "LightSensor.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <math.h>

constexpr float LightSensor::kValidAttenuations[];

LightSensor::LightSensor(int pin, float attenuation, uint16_t rawReadingIntervalMs)
    : pin_(pin)
    , isLightOn_(false)
    , baseThreshold_(LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD)
    , fastWeight_(LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT)
    , trendWeight_(LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT)
    , fastEma_(0)
    , trendEma_(0)
    , attenuationVolts_(attenuation)
    , rawReadingIntervalMs_(rawReadingIntervalMs)
    , lastSampleMs_(0)
{}

bool LightSensor::begin() {
#if defined(ARDUINO)
    analogReadResolution(12);
    applyAttenuation();
    int32_t initial = static_cast<int32_t>(analogRead(pin_));
    // Seed both EMAs to the first reading so they start equal (flank = 0).
    fastEma_ = initial;
    trendEma_ = initial;
    // Always start OFF. The component layer (US-0046) records the first reading
    // as a baseline and does not trigger a transition on it, so the initial
    // state is irrelevant for boot — only actual changes matter.
    lastSampleMs_ = millis();
#endif
    return true;
}

void LightSensor::poll() {
#if defined(ARDUINO)
    const uint32_t now = millis();
    // Non-blocking: return immediately if the sample interval hasn't elapsed.
    if (now - lastSampleMs_ < rawReadingIntervalMs_) return;

    int32_t raw = static_cast<int32_t>(analogRead(pin_));

    // Update both EMAs with their respective weights and divisors.
    fastEma_ = ((raw * fastWeight_)
             + (fastEma_ * (LIGHT_SENSOR_FAST_EMA_DIVISOR - fastWeight_)))
             / LIGHT_SENSOR_FAST_EMA_DIVISOR;

    trendEma_ = ((raw * trendWeight_)
              + (trendEma_ * (LIGHT_SENSOR_TREND_EMA_DIVISOR - trendWeight_)))
              / LIGHT_SENSOR_TREND_EMA_DIVISOR;

    // The flank (differential) isolates the fast edge.
    // Positive flank = sudden brightness increase.
    // Negative flank = sudden brightness drop.
    // Gradual changes keep both EMAs together -> flank near zero.
    int32_t flank = fastEma_ - trendEma_;

    // Latching state machine: only toggles on threshold crossings.
    if (!isLightOn_ && flank > baseThreshold_)
        isLightOn_ = true;
    else if (isLightOn_ && flank < -baseThreshold_)
        isLightOn_ = false;

    lastSampleMs_ = now;
#endif
}

uint16_t LightSensor::readRaw() const {
#if defined(ARDUINO)
    return static_cast<uint16_t>(analogRead(pin_));
#else
    return 0;
#endif
}

bool LightSensor::setBaseThreshold(int32_t threshold) {
    if (threshold < 0) return false;
    baseThreshold_ = threshold;
    return true;
}

bool LightSensor::setEmaWeights(int32_t fastWeight, int32_t trendWeight) {
    // Both weights must be strictly between 0 and their divisor.
    if (fastWeight <= 0 || fastWeight >= LIGHT_SENSOR_FAST_EMA_DIVISOR) return false;
    if (trendWeight <= 0 || trendWeight >= LIGHT_SENSOR_TREND_EMA_DIVISOR) return false;
    // Trend ratio must be slower than fast ratio. Cross-multiplied avoid floats:
    //   trend / TREND_DIVISOR >= fast / FAST_DIVISOR  -> reject
    if (trendWeight * LIGHT_SENSOR_FAST_EMA_DIVISOR
        >= fastWeight * LIGHT_SENSOR_TREND_EMA_DIVISOR) return false;
    fastWeight_ = fastWeight;
    trendWeight_ = trendWeight;
    return true;
}

bool LightSensor::setAttenuation(float volts) {
    for (float v : kValidAttenuations) {
        if (fabsf(volts - v) < 0.05f) {
            attenuationVolts_ = v;
            applyAttenuation();
            return true;
        }
    }
    return false;
}

void LightSensor::applyAttenuation() {
#if defined(ARDUINO)
    adc_attenuation_t atten;
    if (fabsf(attenuationVolts_ - 1.1f) < 0.05f)      atten = ADC_0db;
    else if (fabsf(attenuationVolts_ - 1.5f) < 0.05f) atten = ADC_2_5db;
    else if (fabsf(attenuationVolts_ - 2.2f) < 0.05f) atten = ADC_6db;
    else                                                atten = ADC_11db;
    analogSetPinAttenuation(pin_, atten);
#endif
}

void LightSensor::configureUlpWake() {
#if LIGHT_SENSOR_WAKE_ENABLED
    // TODO: configure ULP RISC-V coprocessor to monitor ADC1 on pin_
    // and wake the ESP32 on threshold crossing. Deferred to follow-up story.
#endif
}
