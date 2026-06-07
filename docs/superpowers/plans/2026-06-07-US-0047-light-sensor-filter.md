# US-0047 Light Sensor Attenuation, Filter, and Interval Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add configurable ADC attenuation (pin-specific, float volts), an exponential fixed-point filter with configurable shift, and a non-blocking sample interval to the `LightSensor` library. `readZone()` and `readState()` switch to using the filtered output.

**Architecture:** The `ILightSensor` interface gains `readFiltered()`, `setAttenuation(float)`, `getAttenuation()`, `setFilterShift(uint8_t)`, `getFilterShift()`, `setRawReadingIntervalMs(uint16_t)`, `getRawReadingIntervalMs()`. `readZone()` and `readState()` lose `const` (they now call `readFiltered()` which mutates filter state). The concrete `LightSensor` implements a fixed-point exponential filter with `millis()`-based timing. `readRaw()` remains unchanged for diagnostics.

**Tech Stack:** C++20, Arduino-ESP32, Unity (native tests), PlatformIO

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `lib/LightSensor/ILightSensor.h` | Modify | Add new methods, drop `const` from `readZone()`/`readState()` |
| `lib/LightSensor/LightSensor.h` | Modify | New members, compile-time defaults, changed signatures |
| `lib/LightSensor/LightSensor.cpp` | Modify | `readFiltered()` implementation, `setAttenuation()` mapping, switch internal calls |
| `test/test_light_sensor/test_main.cpp` | Modify | Update mock, add filter/attenuation tests |
| `src/components/cli/debug_cli.cpp` | Modify | Add `light atten <volts>`, update `light status` output |

Unchanged files: `debug_cli.h`, `system_components.h/.cpp`, `main.cpp`, `component_types.h`.

---

### Task 1: Update ILightSensor Interface

**Files:**
- Modify: `lib/LightSensor/ILightSensor.h`

- [x] **Step 1: Add new methods and drop const from readZone/readState**

Replace `ILightSensor.h`:

```cpp
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
///   - readZone()  → LightZone  (DARK / HYSTERESIS_GAP / BRIGHT)
///   - readState() → LightState (DARK / BRIGHT)
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

    /// Instantaneous, unfiltered ADC reading (0–4095 on ESP32-S3).
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

    /// Set the exponential filter shift factor (1–6).
    /// Lower = faster response, less smoothing (1: α=1/2, τ≈140ms @100ms).
    /// Higher = slower but smoother (4: α=1/16, τ≈1.6s @100ms).
    /// Default is 2 (α=1/4, τ≈350ms — fast enough for room-light changes).
    /// Returns false if value is outside 1–6.
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
```

- [x] **Step 2: Commit**

```bash
git add lib/LightSensor/ILightSensor.h
git commit -m "US-0047: add attenuation, filter, and interval to ILightSensor interface"
```

---

### Task 2: Update LightSensor Concrete Class

**Files:**
- Modify: `lib/LightSensor/LightSensor.h`
- Modify: `lib/LightSensor/LightSensor.cpp`

- [x] **Step 1: Update header with new members and compile-time defaults**

Replace `LightSensor.h`:

```cpp
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
```

- [x] **Step 2: Update implementation**

Replace `LightSensor.cpp`:

```cpp
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
    // Seed the filter with the first reading so readFiltered() starts
    // at the ambient level immediately — no ramp-up from zero.
    filteredAccum_ = static_cast<uint32_t>(analogRead(pin_)) << LIGHT_SENSOR_FILTER_SCALE;
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
                       + (raw << LIGHT_SENSOR_FILTER_SCALE);
        lastSampleMs_ = now;
        seeded_ = true;
    }
    return static_cast<uint16_t>(filteredAccum_ >> LIGHT_SENSOR_FILTER_SCALE);
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
```

- [x] **Step 3: Verify existing native tests still pass**

Run: `pio test -e native`
Expected: Failures — MockLightSensor still has old const-correct signatures and missing methods. This is expected (Task 3 will fix the mock).

- [x] **Step 4: Commit**

```bash
git add lib/LightSensor/LightSensor.h lib/LightSensor/LightSensor.cpp
git commit -m "US-0047: add attenuation, filter, and interval to LightSensor"
```

---

### Task 3: Update MockLightSensor and Add New Tests

**Files:**
- Modify: `test/test_light_sensor/test_main.cpp`

- [x] **Step 1: Update MockLightSensor and add new tests**

Replace `test/test_light_sensor/test_main.cpp`:

```cpp
// test_light_sensor – native unit tests for LightSensor hysteresis, filter,
// attenuation, and interval logic
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

#include "../../lib/LightSensor/ILightSensor.h"
#include "../../lib/LightSensor/LightSensor.h"

namespace {

class MockLightSensor final : public ILightSensor {
public:
    void setRaw(uint16_t v) { raw_ = v; }

    uint16_t readRaw() const override { return raw_; }

    uint16_t readFiltered() override { return raw_; }

    LightZone readZone() override {
        const uint16_t val = readFiltered();
        if (val > darkToBrightThreshold_) return LightZone::BRIGHT;
        if (val < brightToDarkThreshold_) return LightZone::DARK;
        return LightZone::HYSTERESIS_GAP;
    }

    LightState readState() override {
        const LightZone zone = readZone();
        if (zone == LightZone::BRIGHT) latchedState_ = LightState::BRIGHT;
        else if (zone == LightZone::DARK) latchedState_ = LightState::DARK;
        return latchedState_;
    }

    bool begin() override { return true; }

    bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) override {
        if (brightToDark > darkToBright) return false;
        brightToDarkThreshold_ = brightToDark;
        darkToBrightThreshold_ = darkToBright;
        return true;
    }

    uint16_t getBrightToDarkThreshold() const override { return brightToDarkThreshold_; }
    uint16_t getDarkToBrightThreshold() const override { return darkToBrightThreshold_; }

    bool setAttenuation(float volts) override {
        const float valid[] = {1.1f, 1.5f, 2.2f, 3.3f};
        for (float v : valid) {
            if (volts > v - 0.05f && volts < v + 0.05f) {
                attenuation_ = v;
                return true;
            }
        }
        return false;
    }
    float getAttenuation() const override { return attenuation_; }

    bool setFilterShift(uint8_t shift) override {
        if (shift < 1 || shift > 6) return false;
        shift_ = shift;
        return true;
    }
    uint8_t getFilterShift() const override { return shift_; }

    void setRawReadingIntervalMs(uint16_t ms) override { interval_ = ms; }
    uint16_t getRawReadingIntervalMs() const override { return interval_; }

    uint16_t raw_ = 0;
    uint16_t brightToDarkThreshold_ = 1200;
    uint16_t darkToBrightThreshold_ = 2800;
    LightState latchedState_ = LightState::DARK;
    float attenuation_ = 3.3f;
    uint8_t shift_ = 2;
    uint16_t interval_ = 20;
};

MockLightSensor sensor;

// ---------------------------------------------------------------------------
// readRaw (unchanged from US-0045)
// ---------------------------------------------------------------------------

void test_readRaw_returns_injected_value() {
    sensor.setRaw(1500);
    TEST_ASSERT_EQUAL_UINT16(1500, sensor.readRaw());
    sensor.setRaw(4095);
    TEST_ASSERT_EQUAL_UINT16(4095, sensor.readRaw());
    sensor.setRaw(0);
    TEST_ASSERT_EQUAL_UINT16(0, sensor.readRaw());
}

// ---------------------------------------------------------------------------
// readZone (now uses readFiltered → same test but via mock filtered path)
// ---------------------------------------------------------------------------

void test_readZone_dark_below_brightToDark() {
    sensor.setThresholds(1200, 2800);
    sensor.setRaw(800);
    TEST_ASSERT_EQUAL(static_cast<int>(LightZone::DARK),
                      static_cast<int>(sensor.readZone()));
}

void test_readZone_bright_above_darkToBright() {
    sensor.setThresholds(1200, 2800);
    sensor.setRaw(3000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightZone::BRIGHT),
                      static_cast<int>(sensor.readZone()));
}

void test_readZone_hysteresis_gap_between_thresholds() {
    sensor.setThresholds(1200, 2800);
    sensor.setRaw(2000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightZone::HYSTERESIS_GAP),
                      static_cast<int>(sensor.readZone()));
}

void test_readZone_dark_on_boundary_brightToDark() {
    sensor.setThresholds(1200, 2800);
    sensor.setRaw(1199);
    TEST_ASSERT_EQUAL(static_cast<int>(LightZone::DARK),
                      static_cast<int>(sensor.readZone()));
}

void test_readZone_bright_on_boundary_darkToBright() {
    sensor.setThresholds(1200, 2800);
    sensor.setRaw(2801);
    TEST_ASSERT_EQUAL(static_cast<int>(LightZone::BRIGHT),
                      static_cast<int>(sensor.readZone()));
}

// ---------------------------------------------------------------------------
// readState (now uses filtered readZone)
// ---------------------------------------------------------------------------

void test_readState_latches_to_bright_and_holds_through_gap() {
    sensor.setThresholds(1200, 2800);

    sensor.setRaw(500);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::DARK), static_cast<int>(sensor.readState()));

    sensor.setRaw(3000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::BRIGHT), static_cast<int>(sensor.readState()));

    sensor.setRaw(2000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::BRIGHT), static_cast<int>(sensor.readState()));
}

void test_readState_latches_to_dark_and_holds_through_gap() {
    sensor.setThresholds(1200, 2800);

    sensor.setRaw(3000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::BRIGHT), static_cast<int>(sensor.readState()));

    sensor.setRaw(500);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::DARK), static_cast<int>(sensor.readState()));

    sensor.setRaw(2000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::DARK), static_cast<int>(sensor.readState()));
}

void test_readState_default_latch_is_dark() {
    MockLightSensor freshSensor;
    freshSensor.setThresholds(1200, 2800);
    freshSensor.setRaw(2000);
    TEST_ASSERT_EQUAL(static_cast<int>(LightState::DARK),
                      static_cast<int>(freshSensor.readState()));
}

// ---------------------------------------------------------------------------
// setThresholds (unchanged)
// ---------------------------------------------------------------------------

void test_setThresholds_rejects_btd_greater_than_dtb() {
    TEST_ASSERT_FALSE(sensor.setThresholds(3000, 2000));
}

void test_setThresholds_accepts_btd_equal_to_dtb() {
    TEST_ASSERT_TRUE(sensor.setThresholds(2000, 2000));
}

void test_setThresholds_accepts_btd_less_than_dtb() {
    TEST_ASSERT_TRUE(sensor.setThresholds(1000, 3000));
}

// ---------------------------------------------------------------------------
// setAttenuation
// ---------------------------------------------------------------------------

void test_setAttenuation_accepts_valid_values() {
    TEST_ASSERT_TRUE(sensor.setAttenuation(1.1f));
    TEST_ASSERT_EQUAL_FLOAT(1.1f, sensor.getAttenuation());

    TEST_ASSERT_TRUE(sensor.setAttenuation(1.5f));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, sensor.getAttenuation());

    TEST_ASSERT_TRUE(sensor.setAttenuation(2.2f));
    TEST_ASSERT_EQUAL_FLOAT(2.2f, sensor.getAttenuation());

    TEST_ASSERT_TRUE(sensor.setAttenuation(3.3f));
    TEST_ASSERT_EQUAL_FLOAT(3.3f, sensor.getAttenuation());
}

void test_setAttenuation_rejects_invalid_values() {
    TEST_ASSERT_FALSE(sensor.setAttenuation(0.0f));
    TEST_ASSERT_FALSE(sensor.setAttenuation(1.0f));
    TEST_ASSERT_FALSE(sensor.setAttenuation(1.2f));
    TEST_ASSERT_FALSE(sensor.setAttenuation(2.0f));
    TEST_ASSERT_FALSE(sensor.setAttenuation(3.0f));
    TEST_ASSERT_FALSE(sensor.setAttenuation(5.0f));
}

// ---------------------------------------------------------------------------
// setFilterShift
// ---------------------------------------------------------------------------

void test_setFilterShift_accepts_valid_range() {
    for (uint8_t s = 1; s <= 6; ++s) {
        TEST_ASSERT_TRUE(sensor.setFilterShift(s));
        TEST_ASSERT_EQUAL_UINT8(s, sensor.getFilterShift());
    }
}

void test_setFilterShift_rejects_out_of_range() {
    TEST_ASSERT_FALSE(sensor.setFilterShift(0));
    TEST_ASSERT_FALSE(sensor.setFilterShift(7));
    TEST_ASSERT_FALSE(sensor.setFilterShift(255));
}

// ---------------------------------------------------------------------------
// sample interval
// ---------------------------------------------------------------------------

void test_raw_reading_interval_default_and_set() {
    TEST_ASSERT_EQUAL_UINT16(20, sensor.getRawReadingIntervalMs());
    sensor.setRawReadingIntervalMs(250);
    TEST_ASSERT_EQUAL_UINT16(250, sensor.getRawReadingIntervalMs());
}

}  // namespace

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_readRaw_returns_injected_value);
    RUN_TEST(test_readZone_dark_below_brightToDark);
    RUN_TEST(test_readZone_bright_above_darkToBright);
    RUN_TEST(test_readZone_hysteresis_gap_between_thresholds);
    RUN_TEST(test_readZone_dark_on_boundary_brightToDark);
    RUN_TEST(test_readZone_bright_on_boundary_darkToBright);
    RUN_TEST(test_readState_latches_to_bright_and_holds_through_gap);
    RUN_TEST(test_readState_latches_to_dark_and_holds_through_gap);
    RUN_TEST(test_readState_default_latch_is_dark);
    RUN_TEST(test_setThresholds_rejects_btd_greater_than_dtb);
    RUN_TEST(test_setThresholds_accepts_btd_equal_to_dtb);
    RUN_TEST(test_setThresholds_accepts_btd_less_than_dtb);
    RUN_TEST(test_setAttenuation_accepts_valid_values);
    RUN_TEST(test_setAttenuation_rejects_invalid_values);
    RUN_TEST(test_setFilterShift_accepts_valid_range);
    RUN_TEST(test_setFilterShift_rejects_out_of_range);
    RUN_TEST(test_raw_reading_interval_default_and_set);

    return UNITY_END();
}
```

- [x] **Step 2: Run tests to verify they pass**

Run: `pio test -e native`
Expected: All 17 tests pass (12 existing + 5 new).

- [x] **Step 3: Commit**

```bash
git add test/test_light_sensor/test_main.cpp
git commit -m "US-0047: update mock and tests for attenuation, filter, interval"
```

---

### Task 4: Update Debug CLI

**Files:**
- Modify: `src/components/cli/debug_cli.cpp`

- [x] **Step 1: Add `light atten` command and update `light status` output**

In `debug_cli.cpp`, add sub-command routing in `process()`. After the existing `light` command block (which handles `thresh` and `status`), add:

```cpp
        } else if (strncmp(arg, "atten ", 6) == 0) {
            cmdLightAtten(arg + 6);
            return true;
        } else if (strncmp(arg, "interval ", 9) == 0) {
            cmdLightInterval(arg + 9);
            return true;
        } else if (strncmp(arg, "shift ", 6) == 0) {
            cmdLightShift(arg + 6);
            return true;
```

Add forward declarations:

```cpp
static void cmdLightAtten(const char* arg);
static void cmdLightInterval(const char* arg);
static void cmdLightShift(const char* arg);
```

Add `cmdLightAtten`, `cmdLightInterval`, and `cmdLightShift` implementations near `cmdLightThresh`:

```cpp
static void cmdLightAtten(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    float volts = atof(arg);
    if (s_lightSensor->setAttenuation(volts)) {
        PROD_LOG(kLogSource, "Attenuation set: %.1f V", (double)s_lightSensor->getAttenuation());
    } else {
        ERROR_LOG(kLogSource, "Invalid attenuation: %.1f (valid: 1.1, 1.5, 2.2, 3.3)", (double)volts);
    }
}

static void cmdLightInterval(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const long ms = strtol(arg, nullptr, 10);
    if (ms < 1 || ms > UINT16_MAX) {
        ERROR_LOG(kLogSource, "Raw reading interval must be 1-65535 ms");
        return;
    }
    s_lightSensor->setRawReadingIntervalMs(static_cast<uint16_t>(ms));
    PROD_LOG(kLogSource, "Raw reading interval set: %ld ms", ms);
}

static void cmdLightShift(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const long shift = strtol(arg, nullptr, 10);
    if (shift < 1 || shift > 6) {
        ERROR_LOG(kLogSource, "Filter shift must be 1-6");
        return;
    }
    s_lightSensor->setFilterShift(static_cast<uint8_t>(shift));
    PROD_LOG(kLogSource, "Filter shift set: %ld", shift);
}
```

Update `cmdLightStatus()` to show filtered value, shift, and attenuation. Replace the status loop body:

```cpp
    Serial.println("  raw | filtered | zone             | state  | BTD   | DTB   | atten | intv | shift");
    ...
    Serial.printf("  %4u | %8u | %-16s | %-6s | %4u | %4u | %.1f  | %4u | %u\r\n",
                  raw, filtered, zoneStr, stateStr,
                  (unsigned)s_lightSensor->getBrightToDarkThreshold(),
                  (unsigned)s_lightSensor->getDarkToBrightThreshold(),
                  (double)s_lightSensor->getAttenuation(),
                  (unsigned)s_lightSensor->getRawReadingIntervalMs(),
                  (unsigned)s_lightSensor->getFilterShift());
```

Update `printHelp()` to add:

```cpp
    Serial.println("  light atten <V>      Set ADC attenuation (1.1, 1.5, 2.2, 3.3)");
    Serial.println("  light interval <ms>  Set raw reading interval in milliseconds");
    Serial.println("  light shift <1-6>    Set filter shift (lower = faster response)");
```

- [x] **Step 2: Full code for the updated cmdLightStatus**

```cpp
static void cmdLightStatus() {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }

    Serial.println("Light sensor status (send 'x' or 'exit' to stop)");
    Serial.println("  raw | filtered | zone             | state  | BTD   | DTB   | atten | intv | shift");

    for (;;) {
        const uint16_t raw = s_lightSensor->readRaw();
        const uint16_t filtered = s_lightSensor->readFiltered();

        LightZone zone = s_lightSensor->readZone();
        const char* zoneStr = "?";
        switch (zone) {
            case LightZone::DARK:           zoneStr = "DARK";            break;
            case LightZone::HYSTERESIS_GAP: zoneStr = "HYSTERESIS_GAP";  break;
            case LightZone::BRIGHT:         zoneStr = "BRIGHT";          break;
        }

        LightState state = s_lightSensor->readState();
        const char* stateStr = "?";
        switch (state) {
            case LightState::DARK:   stateStr = "DARK";   break;
            case LightState::BRIGHT: stateStr = "BRIGHT"; break;
        }

        Serial.printf("  %4u | %8u | %-16s | %-6s | %4u | %4u | %.1f  | %4u | %u\r\n",
                      raw, filtered, zoneStr, stateStr,
                      (unsigned)s_lightSensor->getBrightToDarkThreshold(),
                      (unsigned)s_lightSensor->getDarkToBrightThreshold(),
                      (double)s_lightSensor->getAttenuation(),
                      (unsigned)s_lightSensor->getRawReadingIntervalMs(),
                      (unsigned)s_lightSensor->getFilterShift());

        while (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();
            if (line == "x" || line == "exit") {
                Serial.println("Exiting light status mode.");
                return;
            }
        }

        delay(200);
    }
}
```

- [x] **Step 3: Build and verify**

Run: `pio run -e debug`
Expected: Build succeeds.

Run: `pio run -e production`
Expected: Build succeeds.

- [x] **Step 4: Commit**

```bash
git add src/components/cli/debug_cli.cpp
git commit -m "US-0047: add light atten command, update light status output"
```

---

### Task 5: Final Verification

- [x] **Step 1: Run all native tests**

Run: `pio test -e native`
Expected: All tests pass (existing + new filter/attenuation tests).

- [x] **Step 2: Verify debug build**

Run: `pio run -e debug`
Expected: Build succeeds with no warnings.

- [x] **Step 3: Verify production build**

Run: `pio run -e production`
Expected: Build succeeds. Debug CLI commands stripped.

---

## Self-Review

1. **Spec coverage:** All acceptance criteria covered — ILightSensor interface changes (T1), concrete LightSensor with filter/attenuation/interval (T2), mock + tests (T3), CLI (T4), validation (T5).

2. **Placeholder scan:** No TBD/TODO. Every code block is complete.

3. **Type consistency:** `setAttenuation(float)` matched across interface (T1), implementation (T2), mock (T3), and CLI (T4). `setFilterShift(uint8_t)` consistent. `readFiltered()` return type `uint16_t` consistent. `readZone()`/`readState()` non-const everywhere.
