#pragma once

#include <stdint.h>

enum class LightZone : uint8_t {
    DARK,
    HYSTERESIS_GAP,
    BRIGHT
};

enum class LightState : uint8_t {
    DARK,
    BRIGHT
};

/// Abstract interface for a two-threshold light sensor.
///
/// The sensor exposes two views of the same measurement:
///   - readZone()  → LightZone  (DARK / HYSTERESIS_GAP / BRIGHT)
///   - readState() → LightState (DARK / BRIGHT)
///
/// Zone splits the continuous reading into three bands (separated by two
/// configurable thresholds) so external code can implement Schmitt-trigger
/// behaviour if desired. State collapses the three-band reading into a
/// binary DARK/BRIGHT decision suitable for simple on/off control.
class ILightSensor {
public:
    virtual ~ILightSensor() = default;

    /// Return the latest raw ADC reading (0–4095 on ESP32-S3).
    virtual uint16_t readRaw() const = 0;

    /// Classify the current brightness as DARK, HYSTERESIS_GAP, or BRIGHT.
    /// HYSTERESIS_GAP means the reading lies between the two thresholds.
    virtual LightZone readZone() const = 0;

    /// Return the latched stable state: DARK after crossing below
    /// brightToDark, BRIGHT after crossing above darkToBright, holds the
    /// previous value while the reading is inside the hysteresis band.
    virtual LightState readState() const = 0;

    /// Set the pair of switching thresholds.
    /// @param brightToDark  Reading below this → considered dark (falling edge)
    /// @param darkToBright  Reading above this → considered bright (rising edge)
    /// The gap between brightToDark and darkToBright defines the hysteresis
    /// band that prevents flickering near the switching point.
    /// Returns false if the thresholds are invalid (e.g. brightToDark > darkToBright).
    virtual bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) = 0;

    /// Return the current bright-to-dark threshold.
    virtual uint16_t getBrightToDarkThreshold() const = 0;

    /// Return the current dark-to-bright threshold.
    virtual uint16_t getDarkToBrightThreshold() const = 0;
};
