# US-0046: Light-Driven State Transitions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Wire `LightSensorComponent::poll()` to detect `isLightOn()` changes and post `postStateRequest(LIVE/READY)` to the Supervisor. Add ULP wake stub.

**Architecture:** Change-detection pattern — component stores `lastLightState_` and `baselineEstablished_`, only posts on state transitions. First poll establishes baseline (no request). Members are `protected` and `requestState()` is a virtual helper so test subclasses can intercept.

**Tech Stack:** C++17, PlatformIO native tests, ESP32-S3 Arduino framework

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `lib/LightSensor/ILightSensor.h` | Modify | Add `configureUlpWake()` default virtual |
| `lib/LightSensor/LightSensor.h` | Modify | Add `LIGHT_SENSOR_WAKE_ENABLED` + override |
| `lib/LightSensor/LightSensor.cpp` | Modify | Stub implementation |
| `src/components/composition/system_components.h` | Modify | Protected members + requestState helper |
| `src/components/composition/system_components.cpp` | Modify | `poll()` + `handleSLEEP()` + `requestState()` |
| `test/test_light_driven_transitions/test_main.cpp` | Create | Component-level tests |

---

### Task 1: Add configureUlpWake() to ILightSensor.h

**Files:**
- Modify: `lib/LightSensor/ILightSensor.h`

- [x] **Step 1: Add the virtual method before the closing `};`**

```cpp
    // -- ULP wake (future deep sleep integration) --------------------------

    /// Prepare the sensor for ULP wake monitoring during deep sleep.
    /// Default implementation is a no-op.
    virtual void configureUlpWake() {}
};
```

- [x] **Step 2: Commit**

```bash
git add lib/LightSensor/ILightSensor.h
git commit -m "US-0046: add configureUlpWake() default virtual to ILightSensor"
```

---

### Task 2: Add ULP Define and Override to LightSensor.h

**Files:**
- Modify: `lib/LightSensor/LightSensor.h`

- [x] **Step 1: Add the compile-time gate after the existing defines, before `class LightSensor`**

```cpp
/// Gate for ULP wake code path. Set to 1 once hardware-validated.
#ifndef LIGHT_SENSOR_WAKE_ENABLED
#define LIGHT_SENSOR_WAKE_ENABLED 0
#endif
```

- [x] **Step 2: Add the override declaration in the class body, after `getFlank()`**

```cpp
    void configureUlpWake() override;
```

- [x] **Step 3: Commit**

```bash
git add lib/LightSensor/LightSensor.h
git commit -m "US-0046: add LIGHT_SENSOR_WAKE_ENABLED and configureUlpWake override"
```

---

### Task 3: Implement configureUlpWake() Stub

**Files:**
- Modify: `lib/LightSensor/LightSensor.cpp`

- [x] **Step 1: Add stub at end of file**

```cpp
void LightSensor::configureUlpWake() {
#if LIGHT_SENSOR_WAKE_ENABLED
    // TODO: configure ULP RISC-V coprocessor to monitor ADC1 on pin_
    // and wake the ESP32 on threshold crossing. Deferred to follow-up story.
#endif
}
```

- [x] **Step 2: Commit**

```bash
git add lib/LightSensor/LightSensor.cpp
git commit -m "US-0046: add configureUlpWake() stub"
```

---

### Task 4: Update LightSensorComponent Header

**Files:**
- Modify: `src/components/composition/system_components.h`

- [x] **Step 1: Change `private:` to `protected:` and add new members**

Current:
```cpp
class LightSensorComponent final : public ISystemComponent {
public:
    explicit LightSensorComponent(ILightSensor& sensor);
    ...
private:
    ILightSensor& sensor_;
};
```

Replace with:
```cpp
class LightSensorComponent : public ISystemComponent {
public:
    explicit LightSensorComponent(ILightSensor& sensor);
    ...
protected:
    ILightSensor& sensor_;
    bool lastLightState_      = false;
    bool baselineEstablished_ = false;

    /// Post a state request to the Supervisor. Factored out so test
    /// subclasses can override it for verification.
    virtual void requestState(SystemState target);
};
```

Note: `final` is removed so tests can subclass.

- [x] **Step 2: Commit**

```bash
git add src/components/composition/system_components.h
git commit -m "US-0046: add protected members and requestState helper to LightSensorComponent"
```

---

### Task 5: Implement Component Logic

**Files:**
- Modify: `src/components/composition/system_components.cpp`

- [x] **Step 1: Replace the empty `poll()` with the baseline + change-detection logic**

Current:
```cpp
void LightSensorComponent::poll() {
    // US-0045: no-op — light-driven state transitions are added in US-0046
}
```

Replace with:
```cpp
void LightSensorComponent::poll() {
    sensor_.poll();

    const bool current = sensor_.isLightOn();

    if (!baselineEstablished_) {
        lastLightState_ = current;
        baselineEstablished_ = true;
        return;
    }

    if (current == lastLightState_) return;

    lastLightState_ = current;

    if (current) {
        requestState(SystemState::LIVE);
    } else {
        requestState(SystemState::READY);
    }
}
```

- [x] **Step 2: Add `requestState()` helper**

Add at the end of the file:
```cpp
void LightSensorComponent::requestState(SystemState target) {
    s_supervisor.postStateRequest(target);
}
```

- [x] **Step 3: Update `handleSLEEP()` to call the ULP wake stub**

Current:
```cpp
void LightSensorComponent::handleSLEEP()      { completeTransition(TransitionStatus::Completed); }
```

Replace with:
```cpp
void LightSensorComponent::handleSLEEP() {
#if LIGHT_SENSOR_WAKE_ENABLED
    sensor_.configureUlpWake();
#endif
    completeTransition(TransitionStatus::Completed);
}
```

- [x] **Step 4: Commit**

```bash
git add src/components/composition/system_components.cpp
git commit -m "US-0046: implement light-driven poll() with baseline, requestState, and ULP SLEEP handler"
```

---

### Task 6: Write Component Tests

**Files:**
- Create: `test/test_light_driven_transitions/test_main.cpp`

- [x] **Step 1: Create test directory**

```bash
mkdir -p test/test_light_driven_transitions
```

- [x] **Step 2: Write the test file**

```cpp
// test_light_driven_transitions – component-level tests for
// LightSensorComponent light-driven state request posting.
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

#include "../../lib/LightSensor/ILightSensor.h"
#include "../../src/components/composition/system_components.h"
#include "../../src/core/config.h"
#include "../../src/supervisor/supervisor.h"
#include "../../src/component_types.h"

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
// Test component — overrides requestState() to track calls instead of
// posting to the real Supervisor.
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
    // Sensor is OFF by default
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
}

void test_transition_to_on_posts_live() {
    MockSensor sensor;
    TestComponent component(sensor);
    component.setup();
    // First poll — baseline, sensor OFF
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    // Turn light ON
    sensor.setLightOn(true);
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::LIVE);
}

void test_transition_to_off_posts_ready() {
    MockSensor sensor;
    sensor.setLightOn(true);  // start with light ON
    TestComponent component(sensor);
    component.setup();
    // First poll — baseline, sensor ON
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    // Turn light OFF
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
    // First poll — baseline
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    // Second poll — same state, still ON
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    // Third poll — same state
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
}

void test_multiple_transitions() {
    MockSensor sensor;
    sensor.setLightOn(false);
    TestComponent component(sensor);
    component.setup();
    // Baseline: OFF
    component.poll();
    TEST_ASSERT_EQUAL(0, component.requestCount);
    // OFF -> ON
    sensor.setLightOn(true);
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::LIVE);
    // ON (same, no post)
    component.poll();
    TEST_ASSERT_EQUAL(1, component.requestCount);
    // ON -> OFF
    sensor.setLightOn(false);
    component.poll();
    TEST_ASSERT_EQUAL(2, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::READY);
    // OFF -> ON again
    sensor.setLightOn(true);
    component.poll();
    TEST_ASSERT_EQUAL(3, component.requestCount);
    TEST_ASSERT_TRUE(component.lastRequest == SystemState::LIVE);
}

void test_handle_sleep_calls_configure_ulp_wake() {
    MockSensor sensor;
    LightSensorComponent component(sensor);
    component.setup();
    // handleSLEEP with LIGHT_SENSOR_WAKE_ENABLED=0 should NOT call
    // configureUlpWake (the #if gate prevents it).
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
    RUN_TEST(test_handle_sleep_calls_configure_ulp_wake);

    return UNITY_END();
}
```

- [x] **Step 3: Run the new tests**

```bash
pio test -e native -f test_light_driven_transitions
```

Expected: 6 tests PASS.

- [x] **Step 4: Commit**

```bash
git add test/test_light_driven_transitions/test_main.cpp
git commit -m "US-0046: add LightSensorComponent tests for light-driven transitions"
```

---

### Task 7: Build and Full Test Suite

- [x] **Step 1: Run the full native test suite**

```bash
pio test -e native
```

Expected: All existing tests pass (130) + 6 new tests = 136 total, all PASS.

- [x] **Step 2: Build production target**

```bash
pio run -e production
```

Expected: BUILD SUCCESS.

- [x] **Step 3: Commit any final changes**

```bash
git add -A
git diff --cached --stat
```

Confirm only the six intended files are changed.
