#pragma once

#include <stdint.h>

/// Abstract interface for a dual-EMA edge-detection light sensor.
///
/// Two EMAs run in parallel on every poll():
///   - Fast EMA tracks the raw signal closely (default 40% weight).
///   - Trend EMA follows slowly (default 2% weight), representing ambient baseline.
///
/// The difference (flank = FastEMA - TrendEMA) crosses a single base threshold
/// to trigger on/off state changes. poll() is non-blocking — it samples ADC only
/// when the configurable interval has elapsed.
class ILightSensor {
public:
    virtual ~ILightSensor() = default;

    // -- Lifecycle -----------------------------------------------------------

    /// Seed both EMAs with the first ADC reading and configure hardware.
    virtual bool begin() = 0;

    // -- Polling (non-blocking) ----------------------------------------------

    /// Sample ADC on interval, update both EMAs, compute flank, update state.
    /// Returns immediately if interval has not yet elapsed.
    virtual void poll() = 0;

    // -- Raw reading ---------------------------------------------------------

    /// Instantaneous, unfiltered ADC reading (0-4095 on ESP32-S3).
    virtual uint16_t readRaw() const = 0;

    // -- State ---------------------------------------------------------------

    /// Current latched on/off state (idempotent — does not sample).
    virtual bool isLightOn() const = 0;

    // -- Base threshold ------------------------------------------------------

    /// Set the flank amplitude required to trigger a state change.
    /// Rejects negative values. Default: 150.
    virtual bool setBaseThreshold(int32_t threshold) = 0;
    virtual int32_t getBaseThreshold() const = 0;

    // -- EMA weights ---------------------------------------------------------

    /// Set fast and trend EMA weights. Each must be > 0 and < its divisor.
    /// Trend ratio must be slower than fast ratio:
    ///   trend / trendDivisor < fast / fastDivisor
    /// Defaults: fast=400, trend=20 (divisors both 1000).
    virtual bool setEmaWeights(int32_t fastWeight, int32_t trendWeight) = 0;
    virtual int32_t getEmaFastWeight() const = 0;
    virtual int32_t getEmaTrendWeight() const = 0;

    // -- Attenuation ---------------------------------------------------------

    /// Set ADC input range for the sensor pin. Valid: 1.1, 1.5, 2.2, 3.3.
    virtual bool setAttenuation(float volts) = 0;
    virtual float getAttenuation() const = 0;

    // -- Sample interval -----------------------------------------------------

    /// How often poll() samples a new ADC reading (ms). Default: 20.
    virtual void setRawReadingIntervalMs(uint16_t ms) = 0;
    virtual uint16_t getRawReadingIntervalMs() const = 0;

    // -- Diagnostic (for CLI status display) ---------------------------------

    virtual int32_t getFastEma() const = 0;
    virtual int32_t getTrendEma() const = 0;
    virtual int32_t getFlank() const = 0;

    // -- ULP wake (future deep sleep integration) --------------------------

    /// Prepare the sensor for ULP wake monitoring during deep sleep.
    /// Default implementation is a no-op.
    virtual void configureUlpWake() {}
};
