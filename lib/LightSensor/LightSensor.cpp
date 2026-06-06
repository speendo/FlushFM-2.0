// LightSensor.cpp – Concrete TEMT6000 light sensor on ADC1
#include "LightSensor.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

LightSensor::LightSensor(int pin, uint16_t brightToDark, uint16_t darkToBright)
    : pin_(pin)
    , brightToDarkThreshold_(brightToDark)
    , darkToBrightThreshold_(darkToBright)
    , latchedState_(LightState::DARK)
{}

bool LightSensor::begin() {
#if defined(ARDUINO)
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
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

LightZone LightSensor::readZone() const {
    const uint16_t raw = readRaw();
    if (raw > darkToBrightThreshold_) return LightZone::BRIGHT;
    if (raw < brightToDarkThreshold_) return LightZone::DARK;
    return LightZone::HYSTERESIS_GAP;
}

LightState LightSensor::readState() const {
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
