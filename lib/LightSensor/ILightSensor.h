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

/// Abstract interface for a two-threshold light sensor with a configurable
/// exponential filter, pin-specific ADC attenuation, and a non-blocking sample
/// interval.
///
/// Two views of the same measurement:
///   - readZone()  -> LightZone  (DARK / HYSTERESIS_GAP / BRIGHT)
///   - readState() -> LightState (DARK / BRIGHT)
///
/// Both use readFiltered() internally so thresholds are compared against the
/// smoothed reading. readRaw() is still available for diagnostics.
class ILightSensor {
public:
    virtual ~ILightSensor() = default;

    // -- Thresholds ----------------------------------------------------------

    virtual bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) = 0;
    virtual uint16_t getBrightToDarkThreshold() const = 0;
    virtual uint16_t getDarkToBrightThreshold() const = 0;

    // -- Raw reading ---------------------------------------------------------

    /// Instantaneous, unfiltered ADC reading (0-4095 on ESP32-S3).
    virtual uint16_t readRaw() const = 0;

    // -- Filtered reading ----------------------------------------------------

    /// Exponentially smoothed reading. Auto-samples a single ADC value when the
    /// sample interval has elapsed; returns the cached filtered value otherwise.
    /// Seeds on first call with the current raw reading so no ramp-up from zero.
    /// Modifies internal filter state (non-const).
    virtual uint16_t readFiltered() = 0;

    // -- Zone and state (use readFiltered() internally) -----------------------

    /// Instantaneous light zone based on the FILTERED reading vs thresholds.
    /// non-const because it calls readFiltered().
    virtual LightZone readZone() = 0;

    /// Latched stable state based on the FILTERED reading vs thresholds.
    /// non-const because it calls readFiltered().
    virtual LightState readState() = 0;

    // -- Attenuation ---------------------------------------------------------

    /// Set the ADC input range for the light sensor pin only.
    /// Accepted values: 1.1, 1.5, 2.2, 3.3 (float volts).
    /// Applied to the pin immediately if begin() has already been called.
    /// Returns false if the value is not one of the valid voltages.
    virtual bool setAttenuation(float volts) = 0;

    /// Return the current attenuation in volts.
    virtual float getAttenuation() const = 0;

    // -- Filter parameters ---------------------------------------------------

    /// Set the exponential filter shift factor (1-6).
    /// Lower = faster response, less smoothing (1: alpha=1/2, tau~140ms @100ms).
    /// Higher = slower but smoother (4: alpha=1/16, tau~1.6s @100ms).
    /// Default is 2 (alpha=1/4, tau~350ms -- fast enough for room-light changes).
    /// Returns false if value is outside 1-6.
    virtual bool setFilterShift(uint8_t shift) = 0;

    /// Return the current filter shift factor.
    virtual uint8_t getFilterShift() const = 0;

    // -- Sample interval -----------------------------------------------------

    /// Set how often a new ADC sample is taken (milliseconds).
    virtual void setRawReadingIntervalMs(uint16_t ms) = 0;

    /// Return the current sample interval in milliseconds.
    virtual uint16_t getRawReadingIntervalMs() const = 0;

    // -- Lifecycle -----------------------------------------------------------

    /// Initialise the sensor hardware (ADC pin, attenuation, filter seed).
    virtual bool begin() = 0;
};
