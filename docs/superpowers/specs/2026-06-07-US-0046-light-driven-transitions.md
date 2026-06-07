# DESIGN: Light-Driven State Transitions (US-0046)

## 1. Context

The dual-EMA edge-detection sensor (US-0045 replacement) exposes `isLightOn()` via `poll()`. US-0046 wires this into the Supervisor state machine so the system transitions to LIVE when light turns on and READY when light turns off.

**Overrides US-0046 story text where it conflicts:** `readState()`/`BRIGHT`/`DARK`/`HYSTERESIS_GAP` → `isLightOn()`/`bool`. `readFiltered()` → `sensor_.poll()`. All old API references in the story are superseded by the new dual-EMA interface.

## 2. Baseline on Boot

`isLightOn()` always starts false (both EMAs seeded to equal ambient reading, flank = 0). The sensor has no concept of ambient-vs-artificial light at boot — it only detects changes.

`LightSensorComponent::poll()` handles this by establishing a baseline on its first call:

- First `poll()`: reads `isLightOn()`, stores it in `lastLightState_`, sets `baselineEstablished_ = true`, posts **no** Supervisor request.
- Subsequent `poll()` calls: posts a state request only when `isLightOn()` differs from `lastLightState_`.

This means the room can be lit or dark at boot — the first actual light toggle (someone entering and turning ON, or leaving and turning OFF) correctly triggers the transition. No missed cycles.

## 3. Component Logic (`LightSensorComponent`)

### 3.1 Private Members

| Member | Type | Initial | Purpose |
|--------|------|---------|---------|
| `lastLightState_` | `bool` | `false` | Last `isLightOn()` value the component acted on |
| `baselineEstablished_` | `bool` | `false` | Guards against posting on first poll |

### 3.2 `poll()` Implementation

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
        s_supervisor.postStateRequest(SystemState::LIVE);
    } else {
        s_supervisor.postStateRequest(SystemState::READY);
    }
}
```

### 3.3 Change-Detection Behavior

- Sensor reads ON and `lastLightState_` was OFF → post `LIVE`, update `lastLightState_` to ON
- Sensor reads OFF and `lastLightState_` was ON → post `READY`, update `lastLightState_` to OFF
- Sensor reads same as `lastLightState_` → no-op (nothing changed)
- This naturally prevents CLI-conflict issues: if a manual CLI command changes system state while the light sensor sees no change, the sensor posts nothing

## 4. ULP Wake Stub (`handleSLEEP`)

Future-proofing for deep-sleep ULP light-sensor monitoring. No functional ULP code — only an integration surface.

### 4.1 Interface Addition (`ILightSensor.h`)

```cpp
// Prepare the sensor for ULP wake monitoring during deep sleep.
// Stub — implementation is a no-op until ULP scaffolding exists.
virtual void configureUlpWake() {}
```

### 4.2 Compile-time Gate (`LightSensor.h`)

```cpp
#ifndef LIGHT_SENSOR_WAKE_ENABLED
#define LIGHT_SENSOR_WAKE_ENABLED 0
#endif
```

### 4.3 LightSensor Override (`LightSensor.cpp`)

```cpp
void LightSensor::configureUlpWake() {
#if LIGHT_SENSOR_WAKE_ENABLED
    // TODO: configure ULP RISC-V coprocessor to monitor ADC1 on pin_
    // and wake the ESP32 on threshold crossing. Implementation deferred
    // to a follow-up story. This method exists to provide a clean
    // integration point for the component layer.
#endif
}
```

### 4.4 Component Handler (`LightSensorComponent::handleSLEEP`)

```cpp
void LightSensorComponent::handleSLEEP() {
#if LIGHT_SENSOR_WAKE_ENABLED
    sensor_.configureUlpWake();
#endif
    completeTransition(TransitionStatus::Completed);
}
```

## 5. Tests

### 5.1 Test Strategy

A mock `ILightSensor` and a mock Supervisor (or test spy) verify that:

- First poll does not post any request (baseline establishment)
- State transitions from OFF to ON post `LIVE` exactly once
- State transitions from ON to OFF post `READY` exactly once
- Consecutive polls with the same state post nothing
- Consecutive polls with intermediate toggles post correctly (ON→OFF→ON posts LIVE once per ON event, READY once per OFF event)
- `handleSLEEP()` when `LIGHT_SENSOR_WAKE_ENABLED = 1` calls `configureUlpWake()`

### 5.2 MockLightSensor for Component Tests

The mock for component tests is simpler than the unit-test mock — it only needs to implement `ILightSensor` with a settable `isLightOn_` and a flag tracking whether `configureUlpWake()` was called.

### 5.3 Test Runner Integration

Tests go in a new file `test/test_light_driven_transitions/test_main.cpp` (separate from `test_light_sensor` which tests the sensor algorithm). This tests the component layer's correct use of the `ILightSensor` interface.

## 6. Files Changed

```
lib/LightSensor/ILightSensor.h                              — add configureUlpWake() default virtual
lib/LightSensor/LightSensor.h                               — add LIGHT_SENSOR_WAKE_ENABLED define + override
lib/LightSensor/LightSensor.cpp                             — stub implementation
src/components/composition/system_components.h              — add lastLightState_, baselineEstablished_ members
src/components/composition/system_components.cpp            — poll() + handleSLEEP() logic
test/test_light_driven_transitions/test_main.cpp            — new: component-level tests
```

**No changes to:** `main.cpp`, `debug_cli.cpp`, `config.h`, `pinout.md`, existing test files.
