// test_light_sensor – native unit tests for LightSensor hysteresis logic
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

    LightZone readZone() const override {
        if (raw_ > darkToBrightThreshold_) return LightZone::BRIGHT;
        if (raw_ < brightToDarkThreshold_) return LightZone::DARK;
        return LightZone::HYSTERESIS_GAP;
    }

    LightState readState() const override {
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

    uint16_t raw_ = 0;
    uint16_t brightToDarkThreshold_ = 1200;
    uint16_t darkToBrightThreshold_ = 2800;
    mutable LightState latchedState_ = LightState::DARK;
};

MockLightSensor sensor;

void test_readRaw_returns_injected_value() {
    sensor.setRaw(1500);
    TEST_ASSERT_EQUAL_UINT16(1500, sensor.readRaw());
    sensor.setRaw(4095);
    TEST_ASSERT_EQUAL_UINT16(4095, sensor.readRaw());
    sensor.setRaw(0);
    TEST_ASSERT_EQUAL_UINT16(0, sensor.readRaw());
}

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

void test_setThresholds_rejects_btd_greater_than_dtb() {
    TEST_ASSERT_FALSE(sensor.setThresholds(3000, 2000));
}

void test_setThresholds_accepts_btd_equal_to_dtb() {
    TEST_ASSERT_TRUE(sensor.setThresholds(2000, 2000));
}

void test_setThresholds_accepts_btd_less_than_dtb() {
    TEST_ASSERT_TRUE(sensor.setThresholds(1000, 3000));
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

    return UNITY_END();
}
