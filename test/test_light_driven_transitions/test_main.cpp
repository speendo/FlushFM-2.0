// test_light_driven_transitions – component-level tests for
// LightSensorComponent light-driven state request posting.
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

#include "../../lib/LightSensor/ILightSensor.h"
#include "../../src/components/composition/system_components.h"
#include "../../src/supervisor/supervisor.h"

namespace {

// ---------------------------------------------------------------------------
// Mock ILightSensor — lets tests control isLightOn() and poll()
// ---------------------------------------------------------------------------
class MockSensor final : public ILightSensor {
public:
    void setLightOn(bool v) { lightOn_ = v; }
    bool configureUlpWakeCalled() const { return ulpWakeCalled_; }

    bool begin() override { return true; }
    void poll() override {}
    uint16_t readRaw() const override { return 0; }
    bool isLightOn() const override { return lightOn_; }

    bool setBaseThreshold(int32_t) override { return true; }
    int32_t getBaseThreshold() const override { return 0; }
    bool setEmaWeights(int32_t, int32_t) override { return true; }
    int32_t getEmaFastWeight() const override { return 0; }
    int32_t getEmaTrendWeight() const override { return 0; }
    bool setAttenuation(float) override { return true; }
    float getAttenuation() const override { return 3.3f; }
    void setRawReadingIntervalMs(uint16_t) override {}
    uint16_t getRawReadingIntervalMs() const override { return 20; }
    int32_t getFastEma() const override { return 0; }
    int32_t getTrendEma() const override { return 0; }
    int32_t getFlank() const override { return 0; }

    void configureUlpWake() override { ulpWakeCalled_ = true; }

    bool lightOn_ = false;
    bool ulpWakeCalled_ = false;
};

// ---------------------------------------------------------------------------
// Test component — overrides requestState() to track calls
// ---------------------------------------------------------------------------
class TestComponent : public LightSensorComponent {
public:
    using LightSensorComponent::LightSensorComponent;

    int requestCount = 0;
    SystemState lastRequest = SystemState::BOOTING;

protected:
    void requestState(SystemState target) override {
        requestCount++;
        lastRequest = target;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_first_poll_establishes_baseline_no_request() {
    MockSensor sensor;
    TestComponent component(sensor);
    component.setup();
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
}

void test_transition_to_on_posts_live() {
    MockSensor sensor;
    TestComponent component(sensor);
    component.setup();
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    sensor.setLightOn(true);
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::LIVE);
}

void test_transition_to_off_posts_ready() {
    MockSensor sensor;
    sensor.setLightOn(true);
    TestComponent component(sensor);
    component.setup();
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    sensor.setLightOn(false);
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::READY);
}

void test_same_state_repeated_posts_nothing() {
    MockSensor sensor;
    sensor.setLightOn(true);
    TestComponent component(sensor);
    component.setup();
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
}

void test_multiple_transitions() {
    MockSensor sensor;
    sensor.setLightOn(false);
    TestComponent component(sensor);
    component.setup();
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    sensor.setLightOn(true);
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::LIVE);
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    sensor.setLightOn(false);
    component.poll();
    TEST_ASSERT_EQUAL(2, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::READY);
    sensor.setLightOn(true);
    component.poll();
    TEST_ASSERT_EQUAL(3, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::LIVE);
}

void test_handle_sleep_ulp_wake_not_called_when_disabled() {
    MockSensor sensor;
    LightSensorComponent component(sensor);
    component.setup();
    component.handleSLEEP();
    TEST_ASSERT_FALSE(sensor.configureUlpWakeCalled());
}

}  // namespace

// ---------------------------------------------------------------------------
// Provide the extern Supervisor symbol required by system_components.cpp
// ---------------------------------------------------------------------------
Supervisor s_supervisor;

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_first_poll_establishes_baseline_no_request);
    RUN_TEST(test_transition_to_on_posts_live);
    RUN_TEST(test_transition_to_off_posts_ready);
    RUN_TEST(test_same_state_repeated_posts_nothing);
    RUN_TEST(test_multiple_transitions);
    RUN_TEST(test_handle_sleep_ulp_wake_not_called_when_disabled);

    return UNITY_END();
}
