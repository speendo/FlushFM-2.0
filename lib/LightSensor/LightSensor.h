// LightSensor.h – Concrete TEMT6000 light sensor on ADC1
#pragma once

#include "ILightSensor.h"
#include <stdint.h>

#ifndef LIGHT_SENSOR_DEFAULT_BTD
#define LIGHT_SENSOR_DEFAULT_BTD 1200
#endif

#ifndef LIGHT_SENSOR_DEFAULT_DTB
#define LIGHT_SENSOR_DEFAULT_DTB 2800
#endif

class LightSensor final : public ILightSensor {
public:
    LightSensor(int pin,
                uint16_t brightToDark = LIGHT_SENSOR_DEFAULT_BTD,
                uint16_t darkToBright = LIGHT_SENSOR_DEFAULT_DTB);

    bool begin();

    uint16_t readRaw() const override;
    LightZone readZone() const override;
    LightState readState() const override;
    bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) override;
    uint16_t getBrightToDarkThreshold() const override { return brightToDarkThreshold_; }
    uint16_t getDarkToBrightThreshold() const override { return darkToBrightThreshold_; }

private:
    int pin_;
    uint16_t brightToDarkThreshold_;
    uint16_t darkToBrightThreshold_;
    mutable LightState latchedState_;
};
