// LightSensor.cpp – Concrete TEMT6000 light sensor on ADC1
#include "LightSensor.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <math.h>

constexpr float LightSensor::kValidAttenuations[];

LightSensor::LightSensor(int pin,
                         uint16_t brightToDark,
                         uint16_t darkToBright,
                         float attenuation,
                         uint8_t filterShift,
                         uint16_t rawReadingIntervalMs)
    : pin_(pin)
    , brightToDarkThreshold_(brightToDark)
    , darkToBrightThreshold_(darkToBright)
    , latchedState_(LightState::DARK)
    , attenuationVolts_(attenuation)
    , filterShift_(filterShift)
    , rawReadingIntervalMs_(rawReadingIntervalMs)
    , filteredAccum_(0)
    , lastSampleMs_(0)
    , seeded_(false)
{}

bool LightSensor::begin() {
#if defined(ARDUINO)
    analogReadResolution(12);
    applyAttenuation();
    filteredAccum_ = static_cast<uint32_t>(analogRead(pin_)) << (filterShift_ * 2);
    lastSampleMs_ = millis();
    seeded_ = true;
#endif
    return true;
}

uint16_t LightSensor::readRaw() const {
#if defined(ARDUINO)
    return static_cast<uint16_t>(analogRead(pin_));
#else
    return 0;
#endif
}

uint16_t LightSensor::readFiltered() {
#if defined(ARDUINO)
    const uint32_t now = millis();
    if (now - lastSampleMs_ >= rawReadingIntervalMs_ || !seeded_) {
        const uint32_t raw = static_cast<uint32_t>(analogRead(pin_));
        filteredAccum_ = filteredAccum_
                       - (filteredAccum_ >> filterShift_)
                       + (raw << filterShift_);
        lastSampleMs_ = now;
        seeded_ = true;
    }
    return static_cast<uint16_t>(filteredAccum_ >> (filterShift_ * 2));
#else
    return 0;
#endif
}

LightZone LightSensor::readZone() {
    const uint16_t filtered = readFiltered();
    if (filtered > darkToBrightThreshold_) return LightZone::BRIGHT;
    if (filtered < brightToDarkThreshold_) return LightZone::DARK;
    return LightZone::HYSTERESIS_GAP;
}

LightState LightSensor::readState() {
    const LightZone zone = readZone();
    if (zone == LightZone::BRIGHT) latchedState_ = LightState::BRIGHT;
    else if (zone == LightZone::DARK) latchedState_ = LightState::DARK;
    return latchedState_;
}

bool LightSensor::setThresholds(uint16_t brightToDark, uint16_t darkToBright) {
    if (brightToDark > darkToBright) return false;
    brightToDarkThreshold_ = brightToDark;
    darkToBrightThreshold_ = darkToBright;
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

bool LightSensor::setFilterShift(uint8_t shift) {
    if (shift < 1 || shift > 6) return false;
    filterShift_ = shift;
    return true;
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
