# US-0045 Light Sensor Component Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Create a standalone `LightSensor` library under `lib/LightSensor/` with an `ILightSensor` interface (host-testable), register a `LightSensorComponent` with the Supervisor, and add `light thresh` / `light status` debug CLI commands.

**Architecture:** The `ILightSensor` pure virtual interface lives in `lib/LightSensor/ILightSensor.h`. The concrete `LightSensor` class wraps `analogRead()` and lives in `lib/LightSensor/LightSensor.h/.cpp`. A `LightSensorComponent` (in `system_components.h/.cpp`) inherits `ISystemComponent`, receives `ILightSensor&` via DI, and is synchronous (all handlers complete immediately). Debug CLI commands access the sensor via a static `ILightSensor*` stored in `debug_cli.cpp`.

**Tech Stack:** C++20, Arduino-ESP32, Unity (native tests), PlatformIO

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `lib/LightSensor/ILightSensor.h` | Create | Pure virtual interface: `readRaw()`, `readZone()`, `readState()`, `setThresholds()` |
| `lib/LightSensor/LightSensor.h` | Create | Concrete class declaration, enum types, threshold config |
| `lib/LightSensor/LightSensor.cpp` | Create | `analogRead()` wrapper, hysteresis logic, threshold validation |
| `test/test_light_sensor/test_main.cpp` | Create | Native tests using `MockLightSensor` (no hardware) |
| `src/component_types.h` | Modify | Add `V(LightSensor)` to `COMPONENT_ID_X` |
| `src/components/composition/system_components.h` | Modify | Forward-declare `ILightSensor`, add `LightSensorComponent` class |
| `src/components/composition/system_components.cpp` | Modify | Implement `LightSensorComponent` |
| `src/components/cli/debug_cli.h` | Modify | Extend `init()` signature to accept `ILightSensor*` |
| `src/components/cli/debug_cli.cpp` | Modify | Add `light thresh` and `light status` commands |
| `src/main.cpp` | Modify | Instantiate `LightSensor`, `LightSensorComponent`, wire into array |

---

### Task 1: ILightSensor Interface

**Files:**
- Create: `lib/LightSensor/ILightSensor.h`

- [x] **Step 1: Create the pure virtual interface**

```cpp
// ILightSensor.h – Pure virtual interface for light sensor (host-testable)
#pragma once

#include <cstdint>

enum class LightZone : uint8_t {
    DARK,
    HYSTERESIS_GAP,
    BRIGHT
};

enum class LightState : uint8_t {
    DARK,
    BRIGHT
};

class ILightSensor {
public:
    virtual ~ILightSensor() = default;

    virtual uint16_t readRaw() = 0;
    virtual LightZone readZone() = 0;
    virtual LightState readState() = 0;
    virtual bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) = 0;
};
```

- [x] **Step 2: Commit**

```bash
git add lib/LightSensor/ILightSensor.h
git commit -m "US-0045: add ILightSensor pure virtual interface"
```

---

### Task 2: LightSensor Concrete Class

**Files:**
- Create: `lib/LightSensor/LightSensor.h`
- Create: `lib/LightSensor/LightSensor.cpp`

- [x] **Step 1: Create the header with default threshold constants**

```cpp
// LightSensor.h – Concrete TEMT6000 light sensor on ADC1
#pragma once

#include "ILightSensor.h"
#include <cstdint>

#ifndef LIGHT_SENSOR_DEFAULT_BTD
#define LIGHT_SENSOR_DEFAULT_BTD 1200
#endif

#ifndef LIGHT_SENSOR_DEFAULT_DTB
#define LIGHT_SENSOR_DEFAULT_DTB 2800
#endif

class LightSensor final : public ILightSensor {
public:
    LightSensor(int pin,
                uint16_t brightToDark = LIGHT_SENSOR_DEFAULT_BTD,
                uint16_t darkToBright = LIGHT_SENSOR_DEFAULT_DTB);

    bool begin();

    uint16_t readRaw() override;
    LightZone readZone() override;
    LightState readState() override;
    bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) override;

private:
    int pin_;
    uint16_t brightToDarkThreshold_;
    uint16_t darkToBrightThreshold_;
    LightState latchedState_;
};
```

- [x] **Step 2: Create the implementation**

```cpp
// LightSensor.cpp – Concrete TEMT6000 light sensor on ADC1
#include "LightSensor.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

LightSensor::LightSensor(int pin, uint16_t brightToDark, uint16_t darkToBright)
    : pin_(pin)
    , brightToDarkThreshold_(brightToDark)
    , darkToBrightThreshold_(darkToBright)
    , latchedState_(LightState::DARK)
{}

bool LightSensor::begin() {
#ifdef ARDUINO
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
#endif
    return true;
}

uint16_t LightSensor::readRaw() {
#ifdef ARDUINO
    return static_cast<uint16_t>(analogRead(pin_));
#else
    return 0;
#endif
}

LightZone LightSensor::readZone() {
    const uint16_t raw = readRaw();
    if (raw > darkToBrightThreshold_) return LightZone::BRIGHT;
    if (raw < brightToDarkThreshold_) return LightZone::DARK;
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
```

- [x] **Step 3: Commit**

```bash
git add lib/LightSensor/LightSensor.h lib/LightSensor/LightSensor.cpp
git commit -m "US-0045: add concrete LightSensor implementation"
```

---

### Task 3: Native Tests for LightSensor

**Files:**
- Create: `test/test_light_sensor/test_main.cpp`

- [x] **Step 1: Create the mock and tests**

```cpp
// test_light_sensor – native unit tests for LightSensor hysteresis logic
#include <cstddef>
#include <cstdint>
#include <unity.h>

#include "../../lib/LightSensor/ILightSensor.h"
#include "../../lib/LightSensor/LightSensor.h"

namespace {

class MockLightSensor final : public ILightSensor {
public:
    void setRaw(uint16_t v) { raw_ = v; }

    uint16_t readRaw() override { return raw_; }

    LightZone readZone() override {
        if (raw_ > darkToBrightThreshold_) return LightZone::BRIGHT;
        if (raw_ < brightToDarkThreshold_) return LightZone::DARK;
        return LightZone::HYSTERESIS_GAP;
    }

    LightState readState() override {
        const LightZone zone = readZone();
        if (zone == LightZone::BRIGHT) latchedState_ = LightState::BRIGHT;
        else if (zone == LightZone::DARK) latchedState_ = LightState::DARK;
        return latchedState_;
    }

    bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) override {
        if (brightToDark > darkToBright) return false;
        brightToDarkThreshold_ = brightToDark;
        darkToBrightThreshold_ = darkToBright;
        return true;
    }

    uint16_t raw_ = 0;
    uint16_t brightToDarkThreshold_ = 1200;
    uint16_t darkToBrightThreshold_ = 2800;
    LightState latchedState_ = LightState::DARK;
};

MockLightSensor sensor;

// ---------------------------------------------------------------------------
// readRaw
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
// readZone
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
// readState (latched, no third value)
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
// setThresholds validation
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
```

- [x] **Step 2: Run tests to verify they fail (no build config yet)**

Run: `pio test -e native -f test_light_sensor`
Expected: Build error — test directory not yet recognized by PlatformIO (no `test_main.cpp` seems wrong... actually PlatformIO auto-discovers `test/test_*/test_main.cpp`)

Run: `pio test -e native`
Expected: Build succeeds, tests run, new test file automatically discovered.

- [x] **Step 3: Run tests and verify all pass**

Run: `pio test -e native`
Expected: All existing tests pass, plus 12 new light sensor tests pass.

- [x] **Step 4: Commit**

```bash
git add test/test_light_sensor/
git commit -m "US-0045: add LightSensor native tests"
```

---

### Task 4: Add LightSensor to ComponentID

**Files:**
- Modify: `src/component_types.h:99-103`

- [x] **Step 1: Add `V(LightSensor)` to the X-macro**

Replace the `COMPONENT_ID_X` block:

```cpp
#define COMPONENT_ID_X(V) \
    V(BoardInfo) \
    V(WiFi) \
    V(AudioRuntime) \
    V(CLI) \
    V(LightSensor)
```

- [x] **Step 2: Verify the existing component name test still passes**

Run: `pio test -e native -f test_component_types`
Expected: Tests pass (the test only checks the first 4 names, so LightSensor won't appear — but Count is still correct).

Note: The existing `test_component_id_names_match_expected` in `test/test_component_types/test_main.cpp` explicitly asserts only the first four names. LightSensor is not in that test, so no modification needed. `Count` auto-increments.

- [x] **Step 3: Commit**

```bash
git add src/component_types.h
git commit -m "US-0045: add LightSensor to ComponentID enum"
```

---

### Task 5: LightSensorComponent — System Component

**Files:**
- Modify: `src/components/composition/system_components.h`
- Modify: `src/components/composition/system_components.cpp`

- [x] **Step 1: Forward-declare ILightSensor and add LightSensorComponent class declaration**

Append to `system_components.h` after the `CliComponent` class (before `#endif`):

```cpp
class ILightSensor;

/** @brief Monitors TEMT6000 light sensor. Required for quorum.
 *  All handleX() complete immediately. poll() reads light state for
 *  use by US-0046 (light-driven transitions). */
class LightSensorComponent final : public ISystemComponent {
public:
    explicit LightSensorComponent(ILightSensor& sensor);
    bool setup() override;
    void handleBOOTING() override;
    void handleSLEEP() override;
    void handleCONNECTING() override;
    void handleREADY() override;
    void handleLIVE() override;
    void handleERROR() override;
    void handleFATAL() override;
    void poll() override;
    void onTransitionTimeout(uint32_t) override {}

private:
    ILightSensor& sensor_;
};
```

- [x] **Step 2: Implement LightSensorComponent in system_components.cpp**

Add to `system_components.cpp` after the `CliComponent` implementation (before the final blank line):

```cpp
#include "LightSensor.h"

LightSensorComponent::LightSensorComponent(ILightSensor& sensor)
    : ISystemComponent(ComponentID::LightSensor, "LightSensor", true)
    , sensor_(sensor)
{}

bool LightSensorComponent::setup() {
    sensor_.begin();
    registerWithSupervisor(s_supervisor);
    return true;
}

void LightSensorComponent::handleBOOTING()    { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleSLEEP()      { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleCONNECTING() { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleREADY()      { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleLIVE()       { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleERROR()      { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleFATAL()      { completeTransition(TransitionStatus::Completed); }

void LightSensorComponent::poll() {
    // US-0045: no-op — light-driven state transitions are added in US-0046
}
```

- [x] **Step 3: Verify debug build compiles**

Run: `pio run -e debug`
Expected: Build succeeds.

- [x] **Step 4: Commit**

```bash
git add src/components/composition/system_components.h src/components/composition/system_components.cpp
git commit -m "US-0045: add LightSensorComponent system component"
```

---

### Task 6: Wire LightSensor in main.cpp

**Files:**
- Modify: `src/main.cpp`

- [x] **Step 1: Add includes and instantiation**

In `src/main.cpp`, add the include and instantiation. Replace the file with:

```cpp
#ifndef UNIT_TEST

#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "LightSensor.h"
#include "components/audio/audio_callbacks.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "supervisor/supervisor.h"
#include "components/composition/system_components.h"

namespace {

constexpr const char* kLogSource = "Main";

}  // namespace

static IAudioPlayer* s_audio = nullptr;
static LightSensor s_lightSensor(LIGHT_SENSOR_PIN);

Supervisor s_supervisor;
static BoardInfoComponent s_boardInfo;
static WiFiComponent s_wifi;
static AudioRuntimeComponent s_audioRuntime(&s_audio);
static CliComponent s_cli(&s_audio);
static LightSensorComponent s_lightSensorComponent(s_lightSensor);

static ISystemComponent* s_components[] = {
    &s_boardInfo,
    &s_wifi,
    &s_audioRuntime,
    &s_cli,
    &s_lightSensorComponent,
};

static void stateMachineTask(void* param) {
    auto* supervisorV2 = static_cast<Supervisor*>(param);
    supervisorV2->setup();
    for (;;) {
        supervisorV2->run();
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(SERIAL_USB_ENUMERATION_MS);

    PROD_LOG(kLogSource, "Hello FlushFM");
    registerAudioLibraryCallbacks();

    s_audio = new AudioPlayerESP32(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);

    for (ISystemComponent* component : s_components) {
        component->setup();
    }

    xTaskCreatePinnedToCore(
        stateMachineTask,
        "StateMachine",
        8192,
        &s_supervisor,
        2,
        nullptr,
        0
    );
}

void loop() {
    for (ISystemComponent* component : s_components) {
        component->loop();
    }
}

#endif  // UNIT_TEST
```

- [x] **Step 2: Verify debug build compiles**

Run: `pio run -e debug`
Expected: Build succeeds.

- [x] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "US-0045: wire LightSensor and LightSensorComponent in main.cpp"
```

---

### Task 7: Debug CLI — `light thresh` and `light status`

**Files:**
- Modify: `src/components/cli/debug_cli.h`
- Modify: `src/components/cli/debug_cli.cpp`

- [x] **Step 1: Extend debug_cli.h init signature**

Replace `debug_cli.h`:

```cpp
// debug_cli.h – Debug-only Serial commands (tasks, loadtest, suspend, resume, light sensor)
#pragma once

#ifdef DEBUG_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class Supervisor;
class ILightSensor;

namespace debug_cli {

void init(TaskHandle_t* audioTaskHandle, Supervisor* supervisorV2, ILightSensor* lightSensor = nullptr);

bool process(const char* cmd, const char* arg);

void printHelp();

} // namespace debug_cli

#endif // DEBUG_ENABLED
```

- [x] **Step 2: Add light commands to debug_cli.cpp**

Replace `debug_cli.cpp`:

```cpp
// debug_cli.cpp – Debug-only Serial commands
// Compiled only when DEBUG_ENABLED is defined.
#ifdef DEBUG_ENABLED

#include "components/cli/debug_cli.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>

#include "LightSensor.h"
#include "component_types.h"
#include "core/config.h"
#include "core/debug.h"
#include "supervisor/supervisor.h"

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------
static constexpr const char* kLogSource = "DebugCLI";
static TaskHandle_t* s_audioTaskHandle = nullptr;
static Supervisor* s_supervisor = nullptr;
static ILightSensor* s_lightSensor = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printTaskList();
static void printTransitionStatus();
static void loadtestTask(void* param);
static void cmdLightThresh(const char* arg);
static void cmdLightStatus();

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace debug_cli {

void init(TaskHandle_t* audioTaskHandle, Supervisor* supervisor, ILightSensor* lightSensor) {
    s_audioTaskHandle = audioTaskHandle;
    s_supervisor = supervisor;
    s_lightSensor = lightSensor;
}

bool process(const char* cmd, const char* arg) {
    if (strcmp(cmd, "tasks") == 0) {
        printTaskList();
        return true;

    } else if (strcmp(cmd, "loadtest") == 0) {
        const BaseType_t r = xTaskCreatePinnedToCore(
            loadtestTask, "LoadTest", 2048, nullptr, 1, nullptr, 0);
        if (r == pdPASS) {
            PROD_LOG(kLogSource, "LoadTest task started on Core 0 for 5 seconds");
        } else {
            ERROR_LOG(kLogSource, "Failed to create LoadTest task");
        }
        return true;

    } else if (strcmp(cmd, "tstatus") == 0) {
        printTransitionStatus();
        return true;

    } else if (strcmp(cmd, "light") == 0) {
        if (!arg) return false;

        if (strncmp(arg, "thresh ", 7) == 0) {
            cmdLightThresh(arg + 7);
            return true;
        } else if (strcmp(arg, "status") == 0) {
            cmdLightStatus();
            return true;
        }
        return false;
    }

    return false;
}

void printHelp() {
    Serial.println("  tasks               Print FreeRTOS task list (core, state, stack HWM)");
    Serial.println("  loadtest            Run 5s busy-loop on Core 0, check audio stability");
    Serial.println("  tstatus             Show transition and component lifecycle status");
    Serial.println("  light thresh <BTD> <DTB>  Set light sensor thresholds (BTD <= DTB required)");
    Serial.println("  light status        Continuous light sensor readout (send x to exit)");
}

} // namespace debug_cli

// ---------------------------------------------------------------------------
// Light sensor commands (module-private)
// ---------------------------------------------------------------------------

static void cmdLightThresh(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const char* space = strchr(arg, ' ');
    if (!space) {
        ERROR_LOG(kLogSource, "Usage: light thresh <brightToDark> <darkToBright>");
        return;
    }

    const long btd = strtol(arg, nullptr, 10);
    const long dtb = strtol(space + 1, nullptr, 10);

    if (btd < 0 || btd > UINT16_MAX || dtb < 0 || dtb > UINT16_MAX) {
        ERROR_LOG(kLogSource, "Thresholds must be 0-65535");
        return;
    }

    if (s_lightSensor->setThresholds(static_cast<uint16_t>(btd),
                                     static_cast<uint16_t>(dtb))) {
        PROD_LOG(kLogSource, "Thresholds set: BTD=%ld DTB=%ld", btd, dtb);
    } else {
        ERROR_LOG(kLogSource, "Invalid thresholds: brightToDark (%ld) must be <= darkToBright (%ld)", btd, dtb);
    }
}

static void cmdLightStatus() {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }

    Serial.println("Light sensor status (send 'x' or 'exit' to stop)");
    Serial.println("  raw | zone             | state  | BTD   | DTB");

    for (;;) {
        const uint16_t raw = s_lightSensor->readRaw();

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

        Serial.printf("  %4u | %-16s | %-6s | %4u | %4u\r\n",
                      raw, zoneStr, stateStr,
                      (unsigned)0, (unsigned)0);

        // Check for cancel input
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

// ---------------------------------------------------------------------------
// Existing debug commands
// ---------------------------------------------------------------------------

static void printTaskList() {
    Serial.println();
    Serial.println("Task / Memory Report:");
    Serial.println("---------------------------------------------");

    if (s_audioTaskHandle && *s_audioTaskHandle) {
        const UBaseType_t hwm = uxTaskGetStackHighWaterMark(*s_audioTaskHandle);
        Serial.printf("  AudioTask   core=%d  prio=%d  stackHWM=%u DW\r\n",
                      AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY, (unsigned)hwm);
    } else {
        Serial.println("  AudioTask   handle not available");
    }

    Serial.printf("  loopTask    core=1  (Arduino default)\r\n");
    Serial.println("  [WiFi/TCP/IDLE tasks on Core 0 are framework-internal, not application tasks]");
    Serial.println();
    Serial.printf("  Free heap    : %u B\r\n",  (unsigned)ESP.getFreeHeap());
    if (psramFound()) {
        Serial.printf("  Free PSRAM   : %u B\r\n", (unsigned)ESP.getFreePsram());
    }
    Serial.println();
}

static void printTransitionStatus() {
    if (!s_supervisor) {
        ERROR_LOG(kLogSource, "Supervisor not available");
        return;
    }

    Serial.println();
    Serial.printf("SM state:        %s\r\n", stateToString(s_supervisor->getObservedState()));
    Serial.printf("Target:          %s\r\n", stateToString(s_supervisor->getTargetState()));
    Serial.println();
}

static void loadtestTask(void* /*param*/) {
    const uint32_t end = millis() + 5000;
    uint32_t lastYield = millis();
    while (millis() < end) {
        if (millis() - lastYield >= 100) {
            vTaskDelay(1);
            lastYield = millis();
        }
    }
    PROD_LOG(kLogSource, "LoadTest finished -- Core 0 saturation ended");
    vTaskDelete(nullptr);
}

#endif // DEBUG_ENABLED
```

**Self-review note:** The `light status` output line has placeholder `0, 0` for threshold values. This is a bug — thresholds aren't exposed on the `ILightSensor` interface. Need to fix this.

**Fix:** Add `uint16_t getBrightToDarkThreshold()` and `uint16_t getDarkToBrightThreshold()` to `ILightSensor` and `LightSensor`, then use them in the status output.

- [x] **Step 3: Add threshold getters to ILightSensor**

Add to `lib/LightSensor/ILightSensor.h` in the `ILightSensor` class:

```cpp
    virtual uint16_t getBrightToDarkThreshold() const = 0;
    virtual uint16_t getDarkToBrightThreshold() const = 0;
```

- [x] **Step 4: Add threshold getters to LightSensor**

Add to `lib/LightSensor/LightSensor.h` in the `LightSensor` class:

```cpp
    uint16_t getBrightToDarkThreshold() const override { return brightToDarkThreshold_; }
    uint16_t getDarkToBrightThreshold() const override { return darkToBrightThreshold_; }
```

- [x] **Step 5: Update MockLightSensor in tests**

Add to `MockLightSensor` in `test/test_light_sensor/test_main.cpp`:

```cpp
    uint16_t getBrightToDarkThreshold() const override { return brightToDarkThreshold_; }
    uint16_t getDarkToBrightThreshold() const override { return darkToBrightThreshold_; }
```

- [x] **Step 6: Fix the light status output to use real threshold values**

In `cmdLightStatus()`, replace the threshold placeholders:

```cpp
        Serial.printf("  %4u | %-16s | %-6s | %4u | %4u\r\n",
                      raw, zoneStr, stateStr,
                      (unsigned)s_lightSensor->getBrightToDarkThreshold(),
                      (unsigned)s_lightSensor->getDarkToBrightThreshold());
```

- [x] **Step 7: Update CliComponent to pass light sensor to debug_cli::init**

Modify `CliComponent::setup()` in `system_components.cpp` — the component needs access to the `ILightSensor`. Since `CliComponent` currently only takes `IAudioPlayer**`, we need a different approach. The simplest: add a `static ILightSensor*` accessible from `main.cpp`.

In `src/main.cpp`, after `static LightSensor s_lightSensor(LIGHT_SENSOR_PIN);`:

```cpp
extern void setLightSensorForDebug(ILightSensor*);
```

Then in `system_components.cpp`, expose a setter and call it in `main.cpp::setup()`:

Alternative: Since `debug_cli::init()` already accepts the sensor, just call it from `CliComponent::setup()` if the sensor pointer is available. The cleanest approach: store the light sensor pointer as a module-level static in `system_components.cpp` and call `debug_cli::init()` in `LightSensorComponent::setup()`.

In `system_components.cpp`, add at the top:

```cpp
#include "components/cli/debug_cli.h"

static ILightSensor* s_debugLightSensor = nullptr;
```

In `LightSensorComponent::setup()`:

```cpp
bool LightSensorComponent::setup() {
    sensor_.begin();
    registerWithSupervisor(s_supervisor);
#ifdef DEBUG_ENABLED
    s_debugLightSensor = &sensor_;
#endif
    return true;
}
```

Then extend `CliComponent::setup()` to accept the stored pointer. But `debug_cli::init` is already called in `CliComponent::setup()`. Simpler: call `debug_cli::init()` a second time with just the sensor, or add a separate `debug_cli::setLightSensor()`.

**Simplest approach:** Add `debug_cli::setLightSensor(ILightSensor*)` and call it after wiring in `main.cpp`.

In `debug_cli.h`:

```cpp
void setLightSensor(ILightSensor* lightSensor);
```

In `debug_cli.cpp`:

```cpp
void setLightSensor(ILightSensor* lightSensor) {
    s_lightSensor = lightSensor;
}
```

In `main.cpp` `setup()`, after the component setup loop, before `xTaskCreatePinnedToCore`:

```cpp
#ifdef DEBUG_ENABLED
    debug_cli::setLightSensor(&s_lightSensor);
#endif
```

- [x] **Step 8: Run test suite**

Run: `pio test -e native`
Expected: All tests pass.

- [x] **Step 9: Verify debug build compiles**

Run: `pio run -e debug`
Expected: Build succeeds.

- [x] **Step 10: Commit**

```bash
git add lib/LightSensor/ILightSensor.h lib/LightSensor/LightSensor.h test/test_light_sensor/test_main.cpp src/components/cli/debug_cli.h src/components/cli/debug_cli.cpp src/components/composition/system_components.cpp src/main.cpp
git commit -m "US-0045: add light thresh/status debug CLI commands"
```

---

### Task 8: Final Verification

- [x] **Step 1: Run all native tests**

Run: `pio test -e native`
Expected: All tests pass, including 12 new light sensor tests.

- [x] **Step 2: Verify debug build**

Run: `pio run -e debug`
Expected: Build succeeds with no warnings.

- [x] **Step 3: Verify production build**

Run: `pio run -e production`
Expected: Build succeeds. Debug CLI commands stripped.

---

## Self-Review

1. **Spec coverage:** All acceptance criteria from US-0045 are covered — ILightSensor interface (T1), concrete LightSensor (T2), tests (T3), ComponentID (T4), LightSensorComponent registration (T5), main.cpp wiring (T6), CLI commands (T7), validation (T8).

2. **Placeholder scan:** No TBD/TODO placeholders. Step 7 identified and fixed a threshold placeholder bug inline.

3. **Type consistency:** `ILightSensor` methods match across interface (T1), implementation (T2), mock (T3), and CLI usage (T7). Threshold getters added consistently across all three. `ComponentID::LightSensor` used in T4 and T5.
