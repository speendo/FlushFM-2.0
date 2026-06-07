// test_light_sensor – native unit tests for dual-EMA edge-detection light sensor
#include <stddef.h>
#include <stdint.h>
#include <unity.h>
#include <math.h>

#include "../../lib/LightSensor/ILightSensor.h"
#include "../../lib/LightSensor/LightSensor.h"

namespace {

// ---------------------------------------------------------------------------
// MockLightSensor: implements ILightSensor with the real dual-EMA algorithm
// so tests verify the actual edge-detection math without Arduino hardware.
// ---------------------------------------------------------------------------
class MockLightSensor final : public ILightSensor {
public:
    MockLightSensor() {}

    // Inject a raw value that poll() will use on its next call.
    void setRaw(int32_t v) { raw_ = v; }

    // Run one iteration of the dual-EMA algorithm with a given raw ADC value.
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

    // -- ILightSensor --------------------------------------------------------
    bool begin() override {
        // Seed both EMAs to the current raw_ value, matching real hardware
        // where begin() seeds from analogRead(pin_).
        fastEma_ = raw_;
        trendEma_ = raw_;
        // Always start OFF. The component layer (US-0046) records the first
        // reading as a baseline and does not trigger a transition on it.
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

    // Public state for test injection
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
// Ported tests (unchanged contract)
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
// Lifecycle tests
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

void test_poll_applies_ema_math() {
    MockLightSensor s;
    s.begin();
    s.setRaw(1500);
    // One poll with raw=1500, default weights (400, 20), EMAs start at 0:
    //   fastEma = (1500*400 + 0*600) / 1000 = 600
    //   trendEma = (1500*20 + 0*980) / 1000 = 30
    //   flank = 600 - 30 = 570
    s.poll();
    TEST_ASSERT_EQUAL_INT32(600, s.getFastEma());
    TEST_ASSERT_EQUAL_INT32(30, s.getTrendEma());
    TEST_ASSERT_EQUAL_INT32(570, s.getFlank());
}

// ---------------------------------------------------------------------------
// EMA convergence tests
// ---------------------------------------------------------------------------

void test_emas_converge_to_constant_input() {
    MockLightSensor s;
    s.setRaw(2000);  // Seed EMAs so begin() starts both at 2000
    s.begin();
    s.setEmaWeights(400, 20);
    for (int i = 0; i < 100; i++) {
        s.injectRaw(2000);
    }
    // Both EMAs should stay at 2000 (steady-state stability)
    TEST_ASSERT_EQUAL_INT32(2000, s.getFastEma());
    TEST_ASSERT_EQUAL_INT32(2000, s.getTrendEma());
}

void test_flank_near_zero_at_steady_state() {
    MockLightSensor s;
    s.setRaw(1500);  // Seed EMAs so both start at 1500
    s.begin();
    s.setEmaWeights(400, 20);
    for (int i = 0; i < 100; i++) {
        s.injectRaw(1500);
    }
    // With both EMAs seeded to 1500 and constant input, flank stays 0
    TEST_ASSERT_EQUAL_INT32(0, s.getFlank());
}

// ---------------------------------------------------------------------------
// Edge detection tests
// ---------------------------------------------------------------------------

void test_light_turns_on_when_flank_exceeds_threshold() {
    MockLightSensor s;
    s.setRaw(500);  // Seed EMAs to ambient dark level
    s.begin();
    s.setBaseThreshold(100);
    s.setEmaWeights(400, 20);
    // Confirm stable at dark (both EMAs start at 500, flank=0)
    TEST_ASSERT_FALSE(s.isLightOn());
    // Step to 2000: flank = 570 > 100 -> on
    s.injectRaw(2000);
    TEST_ASSERT_TRUE(s.isLightOn());
}

void test_light_turns_off_when_flank_below_negative_threshold() {
    MockLightSensor s;
    s.setRaw(500);   // Seed EMAs to dark
    s.begin();
    s.setBaseThreshold(100);
    s.setEmaWeights(400, 20);
    // Step up to 2000 to trigger ON state
    s.injectRaw(2000);  // flank = 570 > 100 -> ON
    TEST_ASSERT_TRUE(s.isLightOn());
    // Settle at 2000 so trend EMA approaches steady state
    for (int i = 0; i < 500; i++) s.injectRaw(2000);
    // Step to 500: fast drops to ~1400, trend lags at ~1921
    // flank = -521 < -100 -> off
    s.injectRaw(500);
    TEST_ASSERT_FALSE(s.isLightOn());
}

void test_light_stays_on_with_subthreshold_change() {
    MockLightSensor s;
    s.setRaw(500);   // Seed EMAs to dark
    s.begin();
    s.setBaseThreshold(200);
    s.setEmaWeights(400, 20);
    // Step to 2000 to trigger ON state
    s.injectRaw(2000);
    TEST_ASSERT_TRUE(s.isLightOn());
    // Settle so EMAs approach 2000
    for (int i = 0; i < 500; i++) s.injectRaw(2000);
    // Small step down from 2000 -> 1800: flank ~ -76 which is > -200 -> stays on
    s.injectRaw(1800);
    TEST_ASSERT_TRUE(s.isLightOn());
}

void test_light_stays_off_with_subthreshold_change() {
    MockLightSensor s;
    s.setRaw(500);  // Seed EMAs to dark level
    s.begin();
    s.setBaseThreshold(200);
    s.setEmaWeights(400, 20);
    for (int i = 0; i < 10; i++) s.injectRaw(500);
    TEST_ASSERT_FALSE(s.isLightOn());
    // Small step up: flank ~ 38 which is < 200 -> stays off
    s.injectRaw(600);
    TEST_ASSERT_FALSE(s.isLightOn());
}

void test_slow_ramp_does_not_trigger() {
    // Gradual changes (clouds, sunset) should NOT trigger state change
    // because both EMAs track together and flank stays small.
    // Ramp from 1000 to 2000 over 2000 iterations at avg +0.5/iteration.
    // At trend alpha=2% (tau=50), steady-state lag ≈ 0.5*50 = 25.
    // flank ≈ 25 which is < 150 -> no trigger.
    MockLightSensor s;
    s.setRaw(1000);  // Seed EMAs to ambient
    s.begin();
    s.setBaseThreshold(150);
    s.setEmaWeights(400, 20);
    for (int i = 0; i < 10; i++) s.injectRaw(1000);
    TEST_ASSERT_FALSE(s.isLightOn());
    // +1 every 2 iterations = avg +0.5/iteration
    int32_t v = 1000;
    for (int i = 0; i < 2000; i++) {
        if (i % 2 == 0) v++;
        s.injectRaw(v);
    }
    TEST_ASSERT_FALSE(s.isLightOn());
}

// ---------------------------------------------------------------------------
// setBaseThreshold validation
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
// setEmaWeights validation
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
    TEST_ASSERT_FALSE(sensor.setEmaWeights(200, 200));
}

void test_setEmaWeights_accepts_trend_slower_than_fast() {
    TEST_ASSERT_TRUE(sensor.setEmaWeights(400, 20));
    TEST_ASSERT_EQUAL_INT32(400, sensor.getEmaFastWeight());
    TEST_ASSERT_EQUAL_INT32(20, sensor.getEmaTrendWeight());
}

void test_setEmaWeights_accepts_different_divisors() {
    TEST_ASSERT_TRUE(sensor.setEmaWeights(500, 10));
    TEST_ASSERT_EQUAL_INT32(500, sensor.getEmaFastWeight());
    TEST_ASSERT_EQUAL_INT32(10, sensor.getEmaTrendWeight());
}

// ---------------------------------------------------------------------------
// Diagnostic getters
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

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();

    // Ported from old tests
    RUN_TEST(test_readRaw_returns_injected_value);
    RUN_TEST(test_setAttenuation_accepts_valid_values);
    RUN_TEST(test_setAttenuation_rejects_invalid_values);
    RUN_TEST(test_raw_reading_interval_default_and_set);

    // Lifecycle
    RUN_TEST(test_begin_resets_state_and_emas);
    RUN_TEST(test_default_latch_is_off);
    RUN_TEST(test_poll_applies_ema_math);

    // EMA convergence
    RUN_TEST(test_emas_converge_to_constant_input);
    RUN_TEST(test_flank_near_zero_at_steady_state);

    // Edge detection
    RUN_TEST(test_light_turns_on_when_flank_exceeds_threshold);
    RUN_TEST(test_light_turns_off_when_flank_below_negative_threshold);
    RUN_TEST(test_light_stays_on_with_subthreshold_change);
    RUN_TEST(test_light_stays_off_with_subthreshold_change);
    RUN_TEST(test_slow_ramp_does_not_trigger);

    // setBaseThreshold validation
    RUN_TEST(test_setBaseThreshold_rejects_negative);
    RUN_TEST(test_setBaseThreshold_accepts_zero);
    RUN_TEST(test_setBaseThreshold_accepts_positive);

    // setEmaWeights validation
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
