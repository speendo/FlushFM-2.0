# Fix Serial Output & Audio Lazy-Init Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Fix two bugs: (1) no serial output on the USB/OTG port because `Serial` maps to UART0 instead of HW CDC, and (2) `AudioPlayerESP32` global constructor spawning a FreeRTOS I2S task before `app_main()` finishes init.

**Architecture:** Add `ARDUINO_USB_CDC_ON_BOOT=1` to route `Serial` → `HWCDCSerial` on GPIO 19/20. Convert `AudioPlayerESP32` from a global object to a heap-allocated pointer initialized in `setup()`, updating `AudioRuntimeComponent` and `CliComponent` constructors from `IAudioPlayer&` to `IAudioPlayer*`.

**Tech Stack:** ESP32-S3, Arduino-ESP32 3.x (pioarduino), C++20, PlatformIO

**Root Cause Reference:**
- `ARDUINO_USB_CDC_ON_BOOT` defaults to `0` in the framework; without it, `Serial` = `Serial0` (UART0 on GPIO 43/44) and outputs to the CP2102 COM port instead of the USB/OTG port the user is connected to.
- `AudioPlayerESP32` constructor calls `std::make_unique<Audio>()`, which does `xTaskCreateStaticPinnedToCore()` and I2S hardware config before `setup()` runs — a pre-init race condition.

---

## File Modifications

| File | Change |
|------|--------|
| `platformio.ini` | Add `-DARDUINO_USB_CDC_ON_BOOT=1` to debug+production build_flags |
| `src/components/composition/system_components.h` | Change `AudioRuntimeComponent(IAudioPlayer&)` + `CliComponent(IAudioPlayer&)` constructors and `audio_` members to `IAudioPlayer*` |
| `src/components/composition/system_components.cpp` | Update all `audio_.` → `audio_->`, dereference at `start()` and `init()` calls |
| `src/main.cpp` | Replace global `AudioPlayerESP32` + `IAudioPlayer&` with `IAudioPlayer*`, heap-allocate in `setup()` |

---

### Task 1: Add USB CDC build flag

**Files:**
- Modify: `platformio.ini:37-40` (production), `platformio.ini:46-52` (debug)

- [x] **Step 1: Add `-DARDUINO_USB_CDC_ON_BOOT=1` to production build_flags**

In `platformio.ini`, in `[env:production]`, after `-DPRODUCTION_BUILD` (line 41), add the new flag:

```ini
[env:production]
extends = common
build_flags =
    -std=gnu++20
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM
    -DPRODUCTION_BUILD
    -DARDUINO_USB_CDC_ON_BOOT=1
```

- [x] **Step 2: Add `-DARDUINO_USB_CDC_ON_BOOT=1` to debug build_flags**

In `platformio.ini`, in `[env:debug]`, after `-DDEBUG_ENABLED` (line 52), add the new flag:

```ini
[env:debug]
extends = common
build_flags =
    -std=gnu++20
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    -DDEBUG_ENABLED
    -DARDUINO_USB_CDC_ON_BOOT=1
```

- [x] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "fix: add ARDUINO_USB_CDC_ON_BOOT=1 to route Serial to USB CDC (GPIO 19/20)"
```

---

### Task 2: Change AudioRuntimeComponent and CliComponent constructors from `IAudioPlayer&` to `IAudioPlayer*`

**Files:**
- Modify: `src/components/composition/system_components.h:154,172,180,193`

- [x] **Step 1: Change AudioRuntimeComponent constructor and member**

In `src/components/composition/system_components.h`, change line 154 from:
```cpp
    explicit AudioRuntimeComponent(IAudioPlayer& audio);
```
To:
```cpp
    explicit AudioRuntimeComponent(IAudioPlayer* audio);
```

Change line 172 from:
```cpp
    IAudioPlayer& audio_;
```
To:
```cpp
    IAudioPlayer* audio_;
```

- [x] **Step 2: Change CliComponent constructor and member**

In `src/components/composition/system_components.h`, change line 180 from:
```cpp
    explicit CliComponent(IAudioPlayer& audio);
```
To:
```cpp
    explicit CliComponent(IAudioPlayer* audio);
```

Change line 193 from:
```cpp
    IAudioPlayer& audio_;
```
To:
```cpp
    IAudioPlayer* audio_;
```

- [x] **Step 3: Commit**

```bash
git add src/components/composition/system_components.h
git commit -m "refactor: change AudioRuntimeComponent and CliComponent to accept IAudioPlayer*"
```

---

### Task 3: Update component method bodies to use pointer-member syntax

**Files:**
- Modify: `src/components/composition/system_components.cpp:167-168,174,188,195,211,229,238,249,272,303-304,308`

- [x] **Step 1: Update AudioRuntimeComponent constructor signature**

Change line 167 from:
```cpp
AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer& audio)
```
To:
```cpp
AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer* audio)
```

- [x] **Step 2: Update AudioRuntimeComponent::setup() — dereference pointer in start() call**

Change line 174 from:
```cpp
    const bool started = audio_runtime::start(audio_);
```
To:
```cpp
    const bool started = audio_runtime::start(*audio_);
```

- [x] **Step 3: Replace all `audio_.` with `audio_->` in AudioRuntimeComponent methods**

In the same file, replace the following seven usages:

| Line | Old | New |
|------|-----|-----|
| 188 | `audio_.stop();` | `audio_->stop();` |
| 195 | `audio_.stop();` | `audio_->stop();` |
| 211 | `if (!audio_.connectToHost(station))` | `if (!audio_->connectToHost(station))` |
| 229 | `if (!audio_.connectToHost(station))` | `if (!audio_->connectToHost(station))` |
| 238 | `audio_.stop();` | `audio_->stop();` |
| 249 | `const IAudioPlayer::RuntimeState runtimeState = audio_.runtimeState();` | `const IAudioPlayer::RuntimeState runtimeState = audio_->runtimeState();` |
| 272 | `audio_.stop();` | `audio_->stop();` |

Note: `audio_.` only appears in `AudioRuntimeComponent` methods; `CliComponent` does not contain `audio_.` accessors in this file (it passes `audio_` to `cli::init()` in setup).

- [x] **Step 4: Update CliComponent constructor and setup() call**

Change line 303 from:
```cpp
CliComponent::CliComponent(IAudioPlayer& audio)
```
To:
```cpp
CliComponent::CliComponent(IAudioPlayer* audio)
```

Change line 308 from:
```cpp
    cli::init(audio_, audio_runtime::taskHandlePtr(), &s_supervisorV2);
```
To:
```cpp
    cli::init(*audio_, audio_runtime::taskHandlePtr(), &s_supervisorV2);
```

- [x] **Step 5: Commit**

```bash
git add src/components/composition/system_components.cpp
git commit -m "refactor: update component bodies for IAudioPlayer* member access"
```

---

### Task 4: Lazy-init AudioPlayerESP32 in main.cpp

**Files:**
- Modify: `src/main.cpp:23-24,60-82`

- [x] **Step 1: Replace global AudioPlayerESP32 + IAudioPlayer& with a single pointer**

In `src/main.cpp`, replace lines 23-24:
```cpp
static AudioPlayerESP32 s_playerImpl(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
static IAudioPlayer& s_audio = s_playerImpl;
```
With:
```cpp
static IAudioPlayer* s_audio = nullptr;
```

- [x] **Step 2: Allocate AudioPlayerESP32 in setup() after the Serial init block**

In `src/main.cpp`, in `setup()`, after the `while (!Serial)` block and before `PROD_LOG(kLogSource, "Hello FlushFM");`, insert the heap allocation. The setup() function (lines 58-82) should change from:

```cpp
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    const uint32_t start = millis();
    while (!Serial && (millis() - start) < SERIAL_TIMEOUT_MS) {
        delay(10);
    }

    PROD_LOG(kLogSource, "Hello FlushFM");
    registerAudioLibraryCallbacks();

    for (ISystemComponent* component : s_components) {
        component->setup();
    }

    xTaskCreatePinnedToCore(
        stateMachineTask,
        "StateMachine",
        8192,
        &s_supervisorV2,
        2,
        nullptr,
        0
    );
}
```

To:

```cpp
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    const uint32_t start = millis();
    while (!Serial && (millis() - start) < SERIAL_TIMEOUT_MS) {
        delay(10);
    }

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
        &s_supervisorV2,
        2,
        nullptr,
        0
    );
}
```

The `#include "AudioPlayerESP32.h"` on line 5 and `#include "IAudioPlayer.h"` on line 6 remain unchanged — both are still needed.

- [x] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "fix: lazy-init AudioPlayerESP32 in setup() to avoid pre-app_main construction"
```

---

### Task 5: Build and run native tests

- [x] **Step 1: Run native tests**

```bash
pio test -e native
```

Expected: All tests pass. The native test build excludes `main.cpp` and only compiles `supervisor/` files via the build filter, so the `IAudioPlayer*` header change is the only thing that could affect native tests. The existing test `test_component_interface` includes `system_components.h` to verify it compiles — so this also validates our header changes compile cleanly.

- [x] **Step 2: Build production and debug targets**

```bash
pio run -e debug
```

Expected: Build succeeds with no warnings.

```bash
pio run -e production
```

Expected: Build succeeds with no warnings.

