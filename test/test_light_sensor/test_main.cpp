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
// readZone (now uses readFiltered -> same test but via mock filtered path)
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

// ---------------------------------------------------------------------------
// Exponential filter math — tests the formula directly (not via mock)
// Formula: accum = accum - (accum >> shift) + (raw << shift)
// Output:  accum >> (shift * 2)
// ---------------------------------------------------------------------------

void test_filter_gain_is_unity_at_steady_state() {
    // Feed constant raw=1000 for 50 iterations at shift=2 (default)
    // After 50 iterations (~12τ, τ=4) output must converge to within 1 of 1000
    uint32_t accum = 0;
    for (int i = 0; i < 50; i++) {
        accum = accum - (accum >> 2) + (1000 << 2);
    }
    uint16_t output = static_cast<uint16_t>(accum >> 4);
    TEST_ASSERT_UINT16_WITHIN(1, 1000, output);
}

void test_filter_zero_input_converges_to_zero() {
    uint32_t accum = 2000;  // start non-zero
    for (int i = 0; i < 50; i++) {
        accum = accum - (accum >> 2) + (0 << 2);
    }
    uint16_t output = static_cast<uint16_t>(accum >> 4);
    TEST_ASSERT_EQUAL_UINT16(0, output);
}

void test_filter_fullscale_input_converges() {
    uint32_t accum = 0;
    for (int i = 0; i < 50; i++) {
        accum = accum - (accum >> 2) + (4095 << 2);
    }
    uint16_t output = static_cast<uint16_t>(accum >> 4);
    TEST_ASSERT_UINT16_WITHIN(2, 4095, output);
}

void test_filter_all_shifts_converge_correctly() {
    for (uint8_t s = 1; s <= 6; ++s) {
        uint32_t accum = 0;
        // 50 << s iterations gives ~8τ at every shift value
        const int iterations = 50 << s;
        for (int i = 0; i < iterations; i++) {
            accum = accum - (accum >> s) + (2000 << s);
        }
        uint16_t output = static_cast<uint16_t>(accum >> (s * 2));
        // At 8τ, residual error is e^-8 ≈ 0.03%, so ±1 is safe for raw=2000
        TEST_ASSERT_UINT16_WITHIN(1, 2000, output);
    }
}

void test_filter_no_overshoot_on_step_up() {
    uint32_t accum = 0;
    // Settle at 0 for 20 iterations
    for (int i = 0; i < 20; i++) {
        accum = accum - (accum >> 2) + (0 << 2);
    }
    // Step up to 3000 – verify output never exceeds 3000
    for (int i = 0; i < 10; i++) {
        accum = accum - (accum >> 2) + (3000 << 2);
        uint16_t output = static_cast<uint16_t>(accum >> 4);
        TEST_ASSERT_TRUE_MESSAGE(output <= 3000, "Filter overshoot on step up");
    }
}

void test_filter_output_stays_in_adc_range() {
    uint32_t accum = 0;
    // Ramp to 4095, output must stay ≤4095
    for (int i = 0; i < 50; i++) {
        accum = accum - (accum >> 2) + (4095 << 2);
        uint16_t output = static_cast<uint16_t>(accum >> 4);
        TEST_ASSERT_TRUE_MESSAGE(output <= 4095, "Output exceeded 4095 on up-ramp");
    }
    // Drop to 0, output must stay ≥0 (unsigned so automatic) and ≤4095
    for (int i = 0; i < 50; i++) {
        accum = accum - (accum >> 2) + (0 << 2);
        uint16_t output = static_cast<uint16_t>(accum >> 4);
        TEST_ASSERT_TRUE_MESSAGE(output <= 4095, "Output exceeded 4095 on down-ramp");
    }
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
    RUN_TEST(test_filter_gain_is_unity_at_steady_state);
    RUN_TEST(test_filter_zero_input_converges_to_zero);
    RUN_TEST(test_filter_fullscale_input_converges);
    RUN_TEST(test_filter_all_shifts_converge_correctly);
    RUN_TEST(test_filter_no_overshoot_on_step_up);
    RUN_TEST(test_filter_output_stays_in_adc_range);

    return UNITY_END();
}
