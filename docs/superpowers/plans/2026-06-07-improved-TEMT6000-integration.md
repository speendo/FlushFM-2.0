# Improved TEMT6000 Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Replace the hysteresis-based light sensor with a dual-EMA edge-detection algorithm that distinguishes artificial light toggles from natural ambient light shifts.

**Architecture:** Two exponential moving averages (fast 40%, trend 2%) run in parallel on each ADC sample; their difference (flank) crosses a single threshold to detect on/off edges. No arrays, no floating point, zero heap.

**Tech Stack:** C++17, PlatformIO native tests, ESP32-S3 Arduino framework

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `lib/LightSensor/ILightSensor.h` | Rewrite | Pure virtual interface |
| `lib/LightSensor/LightSensor.h` | Rewrite | Concrete class + compile-time defaults |
| `lib/LightSensor/LightSensor.cpp` | Rewrite | Dual-EMA algorithm + ADC hardware |
| `test/test_light_sensor/test_main.cpp` | Rewrite | Mock-based unit tests |
| `src/components/cli/debug_cli.cpp` | Modify | CLI command adaptation |

**Untouched:** `main.cpp`, `system_components.cpp`, `system_components.h`, `component_types.h`, `config.h`, `pinout.md`

---

### Task 1: Rewrite ILightSensor.h (Interface)

**Files:**
- Rewrite: `lib/LightSensor/ILightSensor.h`

- [x] **Step 1: Replace the entire file**

```cpp
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
};
```

---

### Task 2: Rewrite LightSensor.h (Header + Compile-time Defaults)

**Files:**
- Rewrite: `lib/LightSensor/LightSensor.h`

- [x] **Step 1: Replace the entire file**

```cpp
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
```

---

### Task 3: Write Tests (test_main.cpp)

**Files:**
- Rewrite: `test/test_light_sensor/test_main.cpp`

- [x] **Step 1: Replace the entire file with MockLightSensor + all tests**

```cpp
// test_light_sensor – native unit tests for dual-EMA edge-detection light sensor
#include <stddef.h>
#include <stdint.h>
#include <unity.h>
#include <math.h>

#include "../../lib/LightSensor/ILightSensor.h"
#include "../../lib/LightSensor/LightSensor.h"

namespace {

class MockLightSensor final : public ILightSensor {
public:
    MockLightSensor() {}

    void setRaw(int32_t v) { raw_ = v; }

    void injectRaw(int32_t v) {
        fastEma_ = ((v * fastWeight_) + (fastEma_ * (LIGHT_SENSOR_FAST_EMA_DIVISOR - fastWeight_)))
                 / LIGHT_SENSOR_FAST_EMA_DIVISOR;
        trendEma_ = ((v * trendWeight_) + (trendEma_ * (LIGHT_SENSOR_TREND_EMA_DIVISOR - trendWeight_)))
                  / LIGHT_SENSOR_TREND_EMA_DIVISOR;
        int32_t flank = fastEma_ - trendEma_;
        if (!isLightOn_ && flank > baseThreshold_)
            isLightOn_ = true;
        else if (isLightOn_ && flank < -baseThreshold_)
            isLightOn_ = false;
    }

    bool begin() override {
        raw_ = 0;
        fastEma_ = 0;
        trendEma_ = 0;
        isLightOn_ = false;
        baseThreshold_ = LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD;
        fastWeight_ = LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT;
        trendWeight_ = LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT;
        attenuationVolts_ = LIGHT_SENSOR_DEFAULT_ATTENUATION;
        rawReadingIntervalMs_ = LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS;
        return true;
    }

    void poll() override { injectRaw(raw_); }

    uint16_t readRaw() const override { return static_cast<uint16_t>(raw_); }
    bool isLightOn() const override { return isLightOn_; }

    bool setBaseThreshold(int32_t t) override {
        if (t < 0) return false;
        baseThreshold_ = t;
        return true;
    }
    int32_t getBaseThreshold() const override { return baseThreshold_; }

    bool setEmaWeights(int32_t fast, int32_t trend) override {
        if (fast <= 0 || fast >= LIGHT_SENSOR_FAST_EMA_DIVISOR) return false;
        if (trend <= 0 || trend >= LIGHT_SENSOR_TREND_EMA_DIVISOR) return false;
        if (trend * LIGHT_SENSOR_FAST_EMA_DIVISOR >= fast * LIGHT_SENSOR_TREND_EMA_DIVISOR) return false;
        fastWeight_ = fast;
        trendWeight_ = trend;
        return true;
    }
    int32_t getEmaFastWeight() const override { return fastWeight_; }
    int32_t getEmaTrendWeight() const override { return trendWeight_; }

    bool setAttenuation(float volts) override {
        const float valid[] = {1.1f, 1.5f, 2.2f, 3.3f};
        for (float v : valid) {
            if (fabsf(volts - v) < 0.05f) {
                attenuationVolts_ = v;
                return true;
            }
        }
        return false;
    }
    float getAttenuation() const override { return attenuationVolts_; }

    void setRawReadingIntervalMs(uint16_t ms) override { rawReadingIntervalMs_ = ms; }
    uint16_t getRawReadingIntervalMs() const override { return rawReadingIntervalMs_; }

    int32_t getFastEma() const override { return fastEma_; }
    int32_t getTrendEma() const override { return trendEma_; }
    int32_t getFlank() const override { return fastEma_ - trendEma_; }

    int32_t raw_ = 0;
    int32_t fastEma_ = 0;
    int32_t trendEma_ = 0;
    int32_t baseThreshold_ = 0;
    int32_t fastWeight_ = 0;
    int32_t trendWeight_ = 0;
    bool isLightOn_ = false;
    float attenuationVolts_ = 3.3f;
    uint16_t rawReadingIntervalMs_ = 20;
};

MockLightSensor sensor;

// ---------------------------------------------------------------------------
// Ported tests
// ---------------------------------------------------------------------------

void test_readRaw_returns_injected_value() {
    sensor.setRaw(1500);
    TEST_ASSERT_EQUAL_UINT16(1500, sensor.readRaw());
    sensor.setRaw(4095);
    TEST_ASSERT_EQUAL_UINT16(4095, sensor.readRaw());
    sensor.setRaw(0);
    TEST_ASSERT_EQUAL_UINT16(0, sensor.readRaw());
}

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

void test_raw_reading_interval_default_and_set() {
    TEST_ASSERT_EQUAL_UINT16(20, sensor.getRawReadingIntervalMs());
    sensor.setRawReadingIntervalMs(250);
    TEST_ASSERT_EQUAL_UINT16(250, sensor.getRawReadingIntervalMs());
}

// ---------------------------------------------------------------------------
// New tests: lifecycle
// ---------------------------------------------------------------------------

void test_begin_resets_state_and_emas() {
    MockLightSensor s;
    s.begin();
    TEST_ASSERT_EQUAL_INT32(0, s.getFastEma());
    TEST_ASSERT_EQUAL_INT32(0, s.getTrendEma());
    TEST_ASSERT_FALSE(s.isLightOn());
    TEST_ASSERT_EQUAL_INT32(LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD, s.getBaseThreshold());
    TEST_ASSERT_EQUAL_INT32(LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT, s.getEmaFastWeight());
    TEST_ASSERT_EQUAL_INT32(LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT, s.getEmaTrendWeight());
}

void test_default_latch_is_off() {
    MockLightSensor s;
    s.begin();
    TEST_ASSERT_FALSE(s.isLightOn());
}

void test_poll_calls_injectRaw() {
    MockLightSensor s;
    s.begin();
    s.setRaw(1500);
    s.poll();
    // After one poll with raw=1500 and default weights:
    // fastEma = (1500*400 + 0)/1000 = 600
    // trendEma = (1500*20 + 0)/1000 = 30
    TEST_ASSERT_EQUAL_INT32(600, s.getFastEma());
    TEST_ASSERT_EQUAL_INT32(30, s.getTrendEma());
    TEST_ASSERT_EQUAL_INT32(570, s.getFlank());
}

// ---------------------------------------------------------------------------
// New tests: EMA convergence
// ---------------------------------------------------------------------------

void test_emas_converge_to_constant_input() {
    MockLightSensor s;
    s.begin();
    s.setEmaWeights(400, 20);
    // Inject constant 2000 for many iterations — both EMAs must converge
    for (int i = 0; i < 200; i++) {
        s.injectRaw(2000);
    }
    TEST_ASSERT_INT32_WITHIN(10, 2000, s.getFastEma());
    TEST_ASSERT_INT32_WITHIN(10, 2000, s.getTrendEma());
}

void test_flank_near_zero_at_steady_state() {
    MockLightSensor s;
    s.begin();
    s.setEmaWeights(400, 20);
    for (int i = 0; i < 200; i++) {
        s.injectRaw(1500);
    }
    int32_t f = s.getFlank();
    TEST_ASSERT_INT32_WITHIN(10, 0, f);
}

// ---------------------------------------------------------------------------
// New tests: edge detection
// ---------------------------------------------------------------------------

void test_light_turns_on_when_flank_exceeds_threshold() {
    MockLightSensor s;
    s.begin();
    s.setBaseThreshold(100);
    s.setEmaWeights(400, 20);
    // Settle at 500 (dark)
    for (int i = 0; i < 200; i++) s.injectRaw(500);
    TEST_ASSERT_FALSE(s.isLightOn());
    // Step to 2000 — fast EMA jumps, trend EMA lags → flank > threshold
    s.injectRaw(2000);
    TEST_ASSERT_TRUE(s.isLightOn());
}

void test_light_turns_off_when_flank_below_negative_threshold() {
    MockLightSensor s;
    s.begin();
    s.setBaseThreshold(100);
    s.setEmaWeights(400, 20);
    // Settle at 2000 (bright)
    for (int i = 0; i < 200; i++) s.injectRaw(2000);
    TEST_ASSERT_TRUE(s.isLightOn());
    // Step down to 500 — fast EMA drops, trend EMA lags → flank < -threshold
    s.injectRaw(500);
    TEST_ASSERT_FALSE(s.isLightOn());
}

void test_light_stays_on_with_subthreshold_change() {
    MockLightSensor s;
    s.begin();
    s.setBaseThreshold(200);
    s.setEmaWeights(400, 20);
    // Settle at bright
    for (int i = 0; i < 200; i++) s.injectRaw(2000);
    TEST_ASSERT_TRUE(s.isLightOn());
    // Small step down — flank stays below threshold
    s.injectRaw(1800);
    TEST_ASSERT_TRUE(s.isLightOn());
}

void test_light_stays_off_with_subthreshold_change() {
    MockLightSensor s;
    s.begin();
    s.setBaseThreshold(200);
    s.setEmaWeights(400, 20);
    // Settle at dark
    for (int i = 0; i < 200; i++) s.injectRaw(500);
    TEST_ASSERT_FALSE(s.isLightOn());
    // Small step up — flank stays below threshold
    s.injectRaw(600);
    TEST_ASSERT_FALSE(s.isLightOn());
}

void test_slow_ramp_does_not_trigger() {
    // Natural light changes are slow — both EMAs track together, flank stays small
    MockLightSensor s;
    s.begin();
    s.setBaseThreshold(150);
    s.setEmaWeights(400, 20);
    // Settle at 1000
    for (int i = 0; i < 200; i++) s.injectRaw(1000);
    TEST_ASSERT_FALSE(s.isLightOn());
    // Ramp slowly from 1000 to 2000 over 100 steps (10 per step)
    int32_t v = 1000;
    for (int i = 0; i < 100; i++) {
        v += 10;
        s.injectRaw(v);
    }
    TEST_ASSERT_FALSE(s.isLightOn());
}

// ---------------------------------------------------------------------------
// New tests: setBaseThreshold
// ---------------------------------------------------------------------------

void test_setBaseThreshold_rejects_negative() {
    TEST_ASSERT_FALSE(sensor.setBaseThreshold(-1));
}

void test_setBaseThreshold_accepts_zero() {
    TEST_ASSERT_TRUE(sensor.setBaseThreshold(0));
    TEST_ASSERT_EQUAL_INT32(0, sensor.getBaseThreshold());
}

void test_setBaseThreshold_accepts_positive() {
    TEST_ASSERT_TRUE(sensor.setBaseThreshold(300));
    TEST_ASSERT_EQUAL_INT32(300, sensor.getBaseThreshold());
}

// ---------------------------------------------------------------------------
// New tests: setEmaWeights
// ---------------------------------------------------------------------------

void test_setEmaWeights_rejects_zero_or_negative() {
    TEST_ASSERT_FALSE(sensor.setEmaWeights(0, 20));
    TEST_ASSERT_FALSE(sensor.setEmaWeights(400, 0));
    TEST_ASSERT_FALSE(sensor.setEmaWeights(-1, 20));
    TEST_ASSERT_FALSE(sensor.setEmaWeights(400, -1));
}

void test_setEmaWeights_rejects_at_or_above_divisor() {
    TEST_ASSERT_FALSE(sensor.setEmaWeights(LIGHT_SENSOR_FAST_EMA_DIVISOR, 20));
    TEST_ASSERT_FALSE(sensor.setEmaWeights(400, LIGHT_SENSOR_TREND_EMA_DIVISOR));
}

void test_setEmaWeights_rejects_trend_faster_than_fast() {
    TEST_ASSERT_FALSE(sensor.setEmaWeights(20, 400));
}

void test_setEmaWeights_rejects_equal_ratios() {
    // 200/1000 == 200/1000 — trend not slower
    TEST_ASSERT_FALSE(sensor.setEmaWeights(200, 200));
}

void test_setEmaWeights_accepts_trend_slower_than_fast() {
    TEST_ASSERT_TRUE(sensor.setEmaWeights(400, 20));
    TEST_ASSERT_EQUAL_INT32(400, sensor.getEmaFastWeight());
    TEST_ASSERT_EQUAL_INT32(20, sensor.getEmaTrendWeight());
}

void test_setEmaWeights_accepts_different_divisors() {
    // fast=500/1000=50%, trend=10/1000=1% — trend is slower ✓
    TEST_ASSERT_TRUE(sensor.setEmaWeights(500, 10));
    TEST_ASSERT_EQUAL_INT32(500, sensor.getEmaFastWeight());
    TEST_ASSERT_EQUAL_INT32(10, sensor.getEmaTrendWeight());
}

// ---------------------------------------------------------------------------
// New tests: diagnostic getters
// ---------------------------------------------------------------------------

void test_diagnostic_getters_return_internal_state() {
    MockLightSensor s;
    s.begin();
    s.setEmaWeights(400, 20);
    s.injectRaw(1000);
    TEST_ASSERT_EQUAL_INT32(400, s.getFastEma());
    TEST_ASSERT_EQUAL_INT32(20, s.getTrendEma());
    TEST_ASSERT_EQUAL_INT32(380, s.getFlank());
}

}  // namespace

int main() {
    UNITY_BEGIN();

    // Ported
    RUN_TEST(test_readRaw_returns_injected_value);
    RUN_TEST(test_setAttenuation_accepts_valid_values);
    RUN_TEST(test_setAttenuation_rejects_invalid_values);
    RUN_TEST(test_raw_reading_interval_default_and_set);

    // Lifecycle
    RUN_TEST(test_begin_resets_state_and_emas);
    RUN_TEST(test_default_latch_is_off);
    RUN_TEST(test_poll_calls_injectRaw);

    // EMA convergence
    RUN_TEST(test_emas_converge_to_constant_input);
    RUN_TEST(test_flank_near_zero_at_steady_state);

    // Edge detection
    RUN_TEST(test_light_turns_on_when_flank_exceeds_threshold);
    RUN_TEST(test_light_turns_off_when_flank_below_negative_threshold);
    RUN_TEST(test_light_stays_on_with_subthreshold_change);
    RUN_TEST(test_light_stays_off_with_subthreshold_change);
    RUN_TEST(test_slow_ramp_does_not_trigger);

    // setBaseThreshold
    RUN_TEST(test_setBaseThreshold_rejects_negative);
    RUN_TEST(test_setBaseThreshold_accepts_zero);
    RUN_TEST(test_setBaseThreshold_accepts_positive);

    // setEmaWeights
    RUN_TEST(test_setEmaWeights_rejects_zero_or_negative);
    RUN_TEST(test_setEmaWeights_rejects_at_or_above_divisor);
    RUN_TEST(test_setEmaWeights_rejects_trend_faster_than_fast);
    RUN_TEST(test_setEmaWeights_rejects_equal_ratios);
    RUN_TEST(test_setEmaWeights_accepts_trend_slower_than_fast);
    RUN_TEST(test_setEmaWeights_accepts_different_divisors);

    // Diagnostic
    RUN_TEST(test_diagnostic_getters_return_internal_state);

    return UNITY_END();
}
```

- [x] **Step 2: Run tests to verify they pass with MockLightSensor**

```bash
pio test -e native -f test_light_sensor
```

Expected: 24 tests PASS.

- [x] **Step 3: Commit**

```bash
git add lib/LightSensor/ILightSensor.h lib/LightSensor/LightSensor.h test/test_light_sensor/test_main.cpp
git commit -m "US-0045: replace light sensor interface with dual-EMA edge detection, add 24 tests"
```

---

### Task 4: Implement LightSensor.cpp (Real Hardware)

**Files:**
- Rewrite: `lib/LightSensor/LightSensor.cpp`

- [x] **Step 1: Replace the entire file**

```cpp
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
    fastEma_ = initial;
    trendEma_ = initial;
    lastSampleMs_ = millis();
#endif
    return true;
}

void LightSensor::poll() {
#if defined(ARDUINO)
    const uint32_t now = millis();
    if (now - lastSampleMs_ < rawReadingIntervalMs_) return;

    int32_t raw = static_cast<int32_t>(analogRead(pin_));

    fastEma_ = ((raw * fastWeight_)
             + (fastEma_ * (LIGHT_SENSOR_FAST_EMA_DIVISOR - fastWeight_)))
             / LIGHT_SENSOR_FAST_EMA_DIVISOR;

    trendEma_ = ((raw * trendWeight_)
              + (trendEma_ * (LIGHT_SENSOR_TREND_EMA_DIVISOR - trendWeight_)))
              / LIGHT_SENSOR_TREND_EMA_DIVISOR;

    int32_t flank = fastEma_ - trendEma_;

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
    if (fastWeight <= 0 || fastWeight >= LIGHT_SENSOR_FAST_EMA_DIVISOR) return false;
    if (trendWeight <= 0 || trendWeight >= LIGHT_SENSOR_TREND_EMA_DIVISOR) return false;
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
```

- [x] **Step 2: Run native tests to ensure no regressions**

```bash
pio test -e native
```

Expected: All tests pass (24 light sensor tests + any other test suites).

- [x] **Step 3: Commit**

```bash
git add lib/LightSensor/LightSensor.cpp
git commit -m "US-0045: implement dual-EMA edge-detection algorithm in LightSensor"
```

---

### Task 5: Update CLI Commands

**Files:**
- Modify: `src/components/cli/debug_cli.cpp`

- [x] **Step 1: Update forward declarations (lines 33-37)**

Replace:
```cpp
static void cmdLightThresh(const char* arg);
static void cmdLightAtten(const char* arg);
static void cmdLightInterval(const char* arg);
static void cmdLightShift(const char* arg);
static void cmdLightStatus();
```

With:
```cpp
static void cmdLightThresh(const char* arg);
static void cmdLightAtten(const char* arg);
static void cmdLightInterval(const char* arg);
static void cmdLightEma(const char* arg);
static void cmdLightStatus();
```

- [x] **Step 2: Update command routing in `process()` (lines 85-87)**

Replace:
```cpp
        } else if (strncmp(arg, "shift ", 6) == 0) {
            cmdLightShift(arg + 6);
            return true;
```

With:
```cpp
        } else if (strncmp(arg, "ema ", 4) == 0) {
            cmdLightEma(arg + 4);
            return true;
```

- [x] **Step 3: Update help text in `printHelp()` (lines 102-105)**

Replace:
```cpp
    Serial.println("  light thresh <BTD> <DTB>  Set light sensor thresholds (BTD <= DTB required)");
    Serial.println("  light atten <V>      Set ADC attenuation (1.1, 1.5, 2.2, 3.3)");
    Serial.println("  light interval <ms>  Set raw reading interval in milliseconds");
    Serial.println("  light shift <1-6>    Set filter shift (lower = faster response)");
    Serial.println("  light status        Continuous light sensor readout (send x to exit)");
```

With:
```cpp
    Serial.println("  light thresh <n>     Set base threshold for flank detection (>= 0)");
    Serial.println("  light atten <V>      Set ADC attenuation (1.1, 1.5, 2.2, 3.3)");
    Serial.println("  light interval <ms>  Set raw reading interval in milliseconds");
    Serial.println("  light ema <fast> <trend>  Set EMA weights (0 < trend/slowDiv < fast/fastDiv)");
    Serial.println("  light status        Continuous light sensor readout (send x to exit)");
```

- [x] **Step 4: Replace `cmdLightThresh` (lines 115-140)**

Replace the entire function:
```cpp
static void cmdLightThresh(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const long thresh = strtol(arg, nullptr, 10);
    if (thresh < 0) {
        ERROR_LOG(kLogSource, "Base threshold must be >= 0");
        return;
    }
    if (s_lightSensor->setBaseThreshold(static_cast<int32_t>(thresh))) {
        PROD_LOG(kLogSource, "Base threshold set: %ld", thresh);
    } else {
        ERROR_LOG(kLogSource, "Invalid base threshold: %ld", thresh);
    }
}
```

- [x] **Step 5: Replace `cmdLightShift` with `cmdLightEma` (lines 169-181)**

Delete the entire `cmdLightShift` function and add:
```cpp
static void cmdLightEma(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const char* space = strchr(arg, ' ');
    if (!space) {
        ERROR_LOG(kLogSource, "Usage: light ema <fastWeight> <trendWeight>");
        return;
    }
    const long fast = strtol(arg, nullptr, 10);
    const long trend = strtol(space + 1, nullptr, 10);
    if (fast < 1 || trend < 1) {
        ERROR_LOG(kLogSource, "EMA weights must be positive");
        return;
    }
    if (s_lightSensor->setEmaWeights(static_cast<int32_t>(fast), static_cast<int32_t>(trend))) {
        PROD_LOG(kLogSource, "EMA weights set: fast=%ld trend=%ld", fast, trend);
    } else {
        ERROR_LOG(kLogSource,
                  "Invalid EMA weights: need 0 < trend/trendDiv < fast/fastDiv");
    }
}
```

- [x] **Step 6: Replace `cmdLightStatus` (lines 183-230)**

Replace the entire function:
```cpp
static void cmdLightStatus() {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }

    Serial.println("Light sensor status (send 'x' or 'exit' to stop)");
    Serial.println("  raw | fastEMA | trendEMA | flank | state | thresh | atten | intv | fastW | trendW");

    for (;;) {
        s_lightSensor->poll();

        const uint16_t raw = s_lightSensor->readRaw();
        const int32_t fastEma = s_lightSensor->getFastEma();
        const int32_t trendEma = s_lightSensor->getTrendEma();
        const int32_t flank = s_lightSensor->getFlank();
        const bool lightOn = s_lightSensor->isLightOn();

        Serial.printf("  %4u | %7ld | %8ld | %5ld | %-5s | %6ld | %.1f  | %4u | %5ld | %6ld\r\n",
                      raw,
                      (long)fastEma,
                      (long)trendEma,
                      (long)flank,
                      lightOn ? "ON" : "OFF",
                      (long)s_lightSensor->getBaseThreshold(),
                      (double)s_lightSensor->getAttenuation(),
                      (unsigned)s_lightSensor->getRawReadingIntervalMs(),
                      (long)s_lightSensor->getEmaFastWeight(),
                      (long)s_lightSensor->getEmaTrendWeight());

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

- [x] **Step 7: Build the production target to verify compilation**

```bash
pio run -e production
```

Expected: BUILD SUCCESS.

- [x] **Step 8: Run native tests again to confirm no breakage**

```bash
pio test -e native
```

Expected: All tests pass.

- [x] **Step 9: Commit**

```bash
git add src/components/cli/debug_cli.cpp
git commit -m "US-0045: update CLI commands for dual-EMA light sensor (thresh, ema, status)"
```

---

### Task 6: Final Verification

- [x] **Step 1: Run the full native test suite**

```bash
pio test -e native
```

Expected: All tests pass, zero failures.

- [x] **Step 2: Clean production build**

```bash
pio run -e production
```

Expected: BUILD SUCCESS, no warnings related to LightSensor.

- [x] **Step 3: Verify no references to removed symbols**

```bash
grep -rn "LightZone\|LightState\|readFiltered\|readZone\|readState\|setThresholds\|getBrightToDarkThreshold\|getDarkToBrightThreshold\|setFilterShift\|getFilterShift\|LIGHT_SENSOR_DEFAULT_BTD\|LIGHT_SENSOR_DEFAULT_DTB\|LIGHT_SENSOR_FILTER_SHIFT" lib/ src/
```

Expected: No matches (all old symbols fully removed).

- [x] **Step 4: Commit verification**

```bash
git add -A
git diff --cached --stat
```

Confirm only the five intended files are changed. Commit if anything remains.

---

### Summary of Changes

| Count | Item |
|-------|------|
| 2 | Enums deleted (LightZone, LightState) |
| 8 | Interface methods deleted (readFiltered, readZone, readState, setThresholds, getBrightToDarkThreshold, getDarkToBrightThreshold, setFilterShift, getFilterShift) |
| 3 | Compile-time defines deleted (LIGHT_SENSOR_DEFAULT_BTD, LIGHT_SENSOR_DEFAULT_DTB, LIGHT_SENSOR_FILTER_SHIFT) |
| 6 | Member variables deleted/replaced (brightToDarkThreshold_, darkToBrightThreshold_, latchedState_, filterShift_, filteredAccum_, seeded_) |
| 3 | Constructor parameters deleted (brightToDark, darkToBright, filterShift) |
| 11 | New interface methods added (poll, isLightOn, setBaseThreshold, getBaseThreshold, setEmaWeights, getEmaFastWeight, getEmaTrendWeight, getFastEma, getTrendEma, getFlank, plus diagnostic getters) |
| 5 | New compile-time defines added (DEFAULT_BASE_THRESHOLD, DEFAULT_FAST_EMA_WEIGHT, DEFAULT_TREND_EMA_WEIGHT, FAST_EMA_DIVISOR, TREND_EMA_DIVISOR) |
| 5 | New member variables added (baseThreshold_, fastWeight_, trendWeight_, fastEma_, trendEma_) |
| 24 | Tests (4 ported, 20 new) |
