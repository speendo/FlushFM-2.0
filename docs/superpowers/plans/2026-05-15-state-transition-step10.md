# Step 10: Remove Old Supervisor from Components

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Remove all old `Supervisor&` dependencies from the four system components (BoardInfo, WiFi, AudioRuntime, CLI), replace reportCompletion/setErrorEvent calls with SupervisorV2 equivalents, add ComponentMailbox polling in each component's `loop()`, and clean up main.cpp by removing the old `Supervisor` instance and old-loop wiring.

**Architecture:** The global `SupervisorV2 s_supervisorV2` (made non-static in step 10a) is accessed from component .cpp files via `extern`. Each component gains a `ComponentMailbox` member that is registered with SupervisorV2 during `registerWithController()`. The component's `loop()` polls the mailbox; for async components (WiFi, Audio), `completeTransition()` is called from the existing `completePendingTransition()` path. The CLI library (`cli::init()`, `printComponentStatusSummary()`) is also migrated to use SupervisorV2.

**Tech Stack:** C++17, Arduino framework. No new test file — this is a hardware-only wiring step. Verification via `pio run -e production` build and `pio test -e native`.

**Prerequisite:** Step 9 complete (production build succeeds, 145 passed on native).

---

## File Structure

- **Modify:** `src/main.cpp` — remove old `Supervisor s_system`, old `loop()` wiring
- **Modify:** `src/components/composition/system_components.h` — remove `Supervisor&` from constructors, add `ComponentMailbox` members
- **Modify:** `src/components/composition/system_components.cpp` — remove `#include "supervisor/supervisor.h"`, rewrite registerWithController, replace reportCompletion/setErrorEvent calls, add mailbox polling loops, remove invokeComponentTransition
- **Modify:** `src/components/cli/cli.h` — change `Supervisor*` to `SupervisorV2*` in `init()`
- **Modify:** `src/components/cli/cli.cpp` — store `SupervisorV2*`, use `postStateRequest()` instead of `postEvent()`, simplify `printComponentStatusSummary()`
- **Modify:** `src/components/cli/debug_cli.h` / `debug_cli.cpp` — change `Supervisor*` to `SupervisorV2*`

---

### Task 10a: Make SupervisorV2 globally accessible (already done in step 9 — verify)

- [x] **Step 10a.1: Confirm `s_supervisorV2` is non-static**

In `src/main.cpp`, line 28 should read:

```cpp
SupervisorV2 s_supervisorV2;
```

Not `static SupervisorV2 s_supervisorV2;`. If still `static`, remove the `static` keyword.

(Note: this was done in the step 9 implementation; this step verifies it persisted.)

---

### Task 10b: Migrate CLI library (cli.cpp, debug_cli) to SupervisorV2

This task must happen FIRST because `CliComponent::setup()` calls `cli::init()` which takes a `Supervisor*`.

**Files:**
- Modify: `src/components/cli/cli.h` — change `init()` signature
- Modify: `src/components/cli/cli.cpp` — store `SupervisorV2*`, use `postStateRequest()`
- Modify: `src/components/cli/debug_cli.h` — change `init()` signature
- Modify: `src/components/cli/debug_cli.cpp` — change stored pointer type

- [x] **Step 10b.1: Change `cli::init()` signature in `cli.h`**

In `src/components/cli/cli.h`, change:

```cpp
void init(IAudioPlayer& audio, TaskHandle_t* audioTaskHandle, Supervisor* controller = nullptr);
```

To:

```cpp
void init(IAudioPlayer& audio, TaskHandle_t* audioTaskHandle, SupervisorV2* supervisorV2 = nullptr);
```

- [x] **Step 10b.2: Update `cli.cpp` to use SupervisorV2**

In `src/components/cli/cli.cpp`:

a) Replace `#include "supervisor/supervisor.h"` with `#include "supervisor/supervisor_v2.h"`

b) Change the static variable:
```cpp
static Supervisor* s_controller = nullptr;
```
To:
```cpp
static SupervisorV2* s_supervisorV2 = nullptr;
```

c) In `init()`, replace:
```cpp
    s_controller = controller;
    debug_cli::init(audioTaskHandle, controller);
```
With:
```cpp
    s_supervisorV2 = supervisorV2;
    debug_cli::init(audioTaskHandle, supervisorV2);
```

d) Replace all `s_controller->postEvent(SystemEvent::STATE_REQUESTED, SystemState::XXX)` calls with `s_supervisorV2->postStateRequest(SystemState::XXX)`. The three instances in the play/stop/reset command handling:

```cpp
    if (s_supervisorV2) {
            (void)s_supervisorV2->postStateRequest(SystemState::LIVE);
```
```cpp
            (void)s_supervisorV2->postStateRequest(SystemState::READY);
```
```cpp
            (void)s_supervisorV2->postStateRequest(SystemState::READY);
```

e) Simplify `printComponentStatusSummary()` to use SupervisorV2's public interface instead of old Supervisor methods:

```cpp
void printComponentStatusSummary(const SupervisorV2& supervisorV2) {
    Serial.printf("System:     %s\r\n", stateToString(supervisorV2.getObservedState()));
    Serial.printf("Target:     %s\r\n", stateToString(supervisorV2.getTargetState()));
    // Component-level status is not exposed by SupervisorV2's public API;
    // omit the per-component registry dump.
}
```

f) Update the call site at line 270-271:
```cpp
    if (result.key == cli_output::MessageKey::STATUS && s_supervisorV2) {
        printComponentStatusSummary(*s_supervisorV2);
```

- [x] **Step 10b.3: Update `debug_cli.h` and `debug_cli.cpp`**

In `src/components/cli/debug_cli.h`, change:
```cpp
void init(TaskHandle_t* audioTaskHandle, Supervisor* controller);
```
To:
```cpp
void init(TaskHandle_t* audioTaskHandle, SupervisorV2* supervisorV2);
```

In `src/components/cli/debug_cli.cpp`, replace `#include "supervisor/supervisor.h"` with `#include "supervisor/supervisor_v2.h"` and change the stored `Supervisor*` to `SupervisorV2*`.

- [x] **Step 10b.4: Commit**

```bash
git add src/components/cli/cli.h src/components/cli/cli.cpp src/components/cli/debug_cli.h src/components/cli/debug_cli.cpp
git commit -m "step 10b: migrate CLI library from Supervisor to SupervisorV2"
```

---

### Task 10c: BoardInfoComponent migration (simplest — no old Supervisor dependency)

**Files:**
- Modify: `src/components/composition/system_components.h` — add `ComponentMailbox` member
- Modify: `src/components/composition/system_components.cpp` — rewrite registerWithController, add mailbox polling loop

- [x] **Step 10c.1: Add `ComponentMailbox` to `BoardInfoComponent` class declaration**

In `system_components.h`, add a public member to `BoardInfoComponent`:

```cpp
class BoardInfoComponent final : public ISystemComponent {
public:
    BoardInfoComponent();
    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    const ComponentStateMatrix* getStateMatrix() const override { return kBoardInfoStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kBoardInfoStateMatrix); }

    ComponentMailbox supervisorV2Mailbox;
};
```

- [x] **Step 10c.2: Rewrite `BoardInfoComponent` code in `system_components.cpp`**

Replace the entire BoardInfoComponent section with:

```cpp
BoardInfoComponent::BoardInfoComponent() : ISystemComponent(ComponentID::BoardInfo, kBoardInfoName) {}

extern SupervisorV2 s_supervisorV2;

void BoardInfoComponent::registerWithController(Supervisor& controller) const {
    (void)controller;
}

bool BoardInfoComponent::setup() {
    board_info::print();
    s_supervisorV2.registerComponent(
        id(), &const_cast<BoardInfoComponent*>(this)->supervisorV2Mailbox, false);
    return true;
}

void BoardInfoComponent::loop() {
    SystemState target;
    if (!supervisorV2Mailbox.consumeNextState(target)) return;

    switch (target) {
        case SystemState::SLEEP:     setOFF(0); break;
        case SystemState::READY:     setIDLE(0); break;
        case SystemState::LIVE:      setSTREAMING(0); break;
        case SystemState::ERROR:
        case SystemState::FATAL:     setERROR(0); break;
        default: return;
    }
    s_supervisorV2.completeTransition(id(), TransitionStatus::Completed);
}

uint32_t BoardInfoComponent::setOFF(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutOffMs;
}

uint32_t BoardInfoComponent::setIDLE(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutIdleMs;
}

uint32_t BoardInfoComponent::setSTREAMING(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutStreamingMs;
}

uint32_t BoardInfoComponent::setERROR(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutErrorMs;
}

void BoardInfoComponent::onTransitionTimeout(uint32_t transitionId) {
    (void)transitionId;
}
```

- [x] **Step 10c.3: Commit**

```bash
git add src/components/composition/system_components.h src/components/composition/system_components.cpp
git commit -m "step 10c: migrate BoardInfoComponent to SupervisorV2 mailbox"
```

---

### Task 10d: CliComponent migration

**Files:**
- Modify: `src/components/composition/system_components.h` — remove `Supervisor&` from constructor, add `ComponentMailbox`
- Modify: `src/components/composition/system_components.cpp` — rewrite

- [x] **Step 10d.1: Change `CliComponent` class declaration**

In `system_components.h`:
- Change constructor from `CliComponent(IAudioPlayer& audio, Supervisor& system)` to `CliComponent(IAudioPlayer& audio)`
- Remove `Supervisor& system_` from private members
- Add `ComponentMailbox supervisorV2Mailbox;` to private section
- Add `void loop() override;` (already has it)

```cpp
class CliComponent final : public ISystemComponent {
public:
    CliComponent(IAudioPlayer& audio);

    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    const ComponentStateMatrix* getStateMatrix() const override { return kCliStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kCliStateMatrix); }

private:
    IAudioPlayer& audio_;
    ComponentMailbox supervisorV2Mailbox;
};
```

- [x] **Step 10d.2: Rewrite `CliComponent` code in `system_components.cpp`**

Replace CliComponent section (lines 373-432) with:

```cpp
CliComponent::CliComponent(IAudioPlayer& audio)
    : ISystemComponent(ComponentID::CLI, kCliName), audio_(audio) {}

void CliComponent::registerWithController(Supervisor& controller) const {
    (void)controller;
}

bool CliComponent::setup() {
    s_supervisorV2.registerComponent(
        id(), &const_cast<CliComponent*>(this)->supervisorV2Mailbox, false);
    cli::init(audio_, audio_runtime::taskHandlePtr(), &s_supervisorV2);
    cli::printHelp();
    return true;
}

void CliComponent::loop() {
    SystemState target;
    if (supervisorV2Mailbox.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:     setOFF(0); break;
            case SystemState::READY:     setIDLE(0); break;
            case SystemState::LIVE:      setSTREAMING(0); break;
            case SystemState::ERROR:
            case SystemState::FATAL:     setERROR(0); break;
            default: break;
        }
        s_supervisorV2.completeTransition(id(), TransitionStatus::Completed);
    }

    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        cli::process(cmdBuf);
    }
}

uint32_t CliComponent::setOFF(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutOffMs;
}

uint32_t CliComponent::setIDLE(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutIdleMs;
}

uint32_t CliComponent::setSTREAMING(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutStreamingMs;
}

uint32_t CliComponent::setERROR(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutErrorMs;
}

void CliComponent::onTransitionTimeout(uint32_t transitionId) {
    (void)transitionId;
}
```

- [x] **Step 10d.3: Commit**

```bash
git add src/components/composition/system_components.h src/components/composition/system_components.cpp
git commit -m "step 10d: migrate CliComponent to SupervisorV2 mailbox"
```

---

### Task 10e: WiFiComponent migration

**Files:**
- Modify: `src/components/composition/system_components.h` — remove `Supervisor&` from constructor, add `ComponentMailbox`
- Modify: `src/components/composition/system_components.cpp` — replace reportCompletion, replace setErrorEvent, add mailbox polling

- [x] **Step 10e.1: Change `WiFiComponent` class declaration**

In `system_components.h`:
- Change constructor from `WiFiComponent(Supervisor& system)` to `WiFiComponent()`
- Remove `Supervisor& system_` from private members
- Add `ComponentMailbox supervisorV2Mailbox;` to private section

```cpp
class WiFiComponent final : public ISystemComponent {
public:
    WiFiComponent();

    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    bool bootAutoConnectSucceeded() const;
    const ComponentStateMatrix* getStateMatrix() const override { return kWiFiStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kWiFiStateMatrix); }

private:
    static void onConnected(void* context);
    static void onDisconnected(void* context);
    void startPendingTransition(bool streamingTarget, uint32_t transitionId);
    void completePendingTransition(TransitionStatus status, const char* reason);

    ComponentMailbox supervisorV2Mailbox;
    bool bootAutoConnectSucceeded_ = false;
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
};
```

- [x] **Step 10e.2: Rewrite `WiFiComponent` code in `system_components.cpp`**

Replace WiFiComponent section (lines 112-237) with:

```cpp
WiFiComponent::WiFiComponent()
    : ISystemComponent(ComponentID::WiFi, kWiFiName) {}

void WiFiComponent::registerWithController(Supervisor& controller) const {
    (void)controller;
}

bool WiFiComponent::setup() {
    s_supervisorV2.registerComponent(
        id(), &const_cast<WiFiComponent*>(this)->supervisorV2Mailbox, true);

    wifi_manager::setConnectedCallback(&WiFiComponent::onConnected, this);
    wifi_manager::setDisconnectedCallback(&WiFiComponent::onDisconnected, this);
    wifi_manager::init();

    char ssid[settings::kSsidMaxLen] = {};
    char pass[settings::kPassMaxLen] = {};

    if (settings::loadSsid(ssid, sizeof(ssid))) {
        wifi_manager::setSsid(ssid);
        settings::loadPass(pass, sizeof(pass));
        if (pass[0] != '\0') {
            wifi_manager::setPass(pass);
        }

        PROD_LOG(kWiFiName, "Boot auto-connect requested from persisted settings");
        wifi_manager::connect();
        bootAutoConnectSucceeded_ = (wifi_manager::state() == wifi_manager::WiFiState::CONNECTED);
    }

    return true;
}

uint32_t WiFiComponent::setOFF(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    wifi_manager::disconnect();
    completePendingTransition(TransitionStatus::Completed, nullptr);
    return kWiFiTimeoutOffMs;
}

uint32_t WiFiComponent::setIDLE(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    completePendingTransition(TransitionStatus::Completed, nullptr);
    return kWiFiTimeoutIdleMs;
}

uint32_t WiFiComponent::setSTREAMING(uint32_t transitionId) {
    startPendingTransition(true, transitionId);
    if (!wifi_manager::isConnected()) {
        wifi_manager::connect();
    }
    return kWiFiTimeoutStreamingMs;
}

uint32_t WiFiComponent::setERROR(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    completePendingTransition(TransitionStatus::Completed, nullptr);
    return kWiFiTimeoutErrorMs;
}

void WiFiComponent::onTransitionTimeout(uint32_t transitionId) {
    (void)transitionId;
    if (transitionPending_ && pendingTransitionId_ == transitionId) {
        completePendingTransition(TransitionStatus::Failed, "timeout");
    }
}

void WiFiComponent::loop() {
    SystemState target;
    if (supervisorV2Mailbox.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:       setOFF(0); break;
            case SystemState::READY:       setIDLE(0); break;
            case SystemState::CONNECTING:
            case SystemState::LIVE:        setSTREAMING(0); break;
            case SystemState::ERROR:
            case SystemState::FATAL:       setERROR(0); break;
            default: break;
        }
    }

    if (!transitionPending_ || !pendingStreamingTarget_) {
        return;
    }

    if (wifi_manager::isConnected()) {
        completePendingTransition(TransitionStatus::Completed, nullptr);
    }
}

bool WiFiComponent::bootAutoConnectSucceeded() const {
    return bootAutoConnectSucceeded_;
}

void WiFiComponent::onConnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Completed, nullptr);
    }
}

void WiFiComponent::onDisconnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Failed, "wifi disconnected");
    } else {
        s_supervisorV2.postErrorEvent("wifi disconnected", ComponentID::WiFi);
    }
}

void WiFiComponent::startPendingTransition(bool streamingTarget, uint32_t transitionId) {
    transitionPending_ = true;
    pendingStreamingTarget_ = streamingTarget;
    pendingTransitionId_ = transitionId;
}

void WiFiComponent::completePendingTransition(TransitionStatus status, const char* reason) {
    if (!transitionPending_) return;
    transitionPending_ = false;
    (void)reason;
    s_supervisorV2.completeTransition(id(), status);
}
```

- [x] **Step 10e.3: Commit**

```bash
git add src/components/composition/system_components.h src/components/composition/system_components.cpp
git commit -m "step 10e: migrate WiFiComponent to SupervisorV2 mailbox"
```

---

### Task 10f: AudioRuntimeComponent migration

**Files:**
- Modify: `src/components/composition/system_components.h` — remove `Supervisor&` from constructor, add `ComponentMailbox`
- Modify: `src/components/composition/system_components.cpp` — replace reportCompletion, replace setErrorEvent, add mailbox polling

- [x] **Step 10f.1: Change `AudioRuntimeComponent` class declaration**

In `system_components.h`:
- Change constructor from `AudioRuntimeComponent(IAudioPlayer& audio, Supervisor& system)` to `AudioRuntimeComponent(IAudioPlayer& audio)`
- Remove `Supervisor& system_` from private members
- Add `ComponentMailbox supervisorV2Mailbox;` to private section

```cpp
class AudioRuntimeComponent final : public ISystemComponent {
public:
    AudioRuntimeComponent(IAudioPlayer& audio);

    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    const ComponentStateMatrix* getStateMatrix() const override { return kAudioStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kAudioStateMatrix); }

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);
    void startPendingTransition(bool streamingTarget, uint32_t transitionId);
    void completePendingTransition(TransitionStatus status, const char* reason);

    IAudioPlayer& audio_;
    ComponentMailbox supervisorV2Mailbox;
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
    bool pendingErrorTarget_ = false;
};
```

- [x] **Step 10f.2: Rewrite `AudioRuntimeComponent` code in `system_components.cpp`**

Replace AudioRuntimeComponent section (lines 239-371) with:

```cpp
AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer& audio)
    : ISystemComponent(ComponentID::AudioRuntime, kAudioRuntimeName), audio_(audio) {}

void AudioRuntimeComponent::registerWithController(Supervisor& controller) const {
    (void)controller;
}

bool AudioRuntimeComponent::setup() {
    s_supervisorV2.registerComponent(
        id(), &const_cast<AudioRuntimeComponent*>(this)->supervisorV2Mailbox, true);

    audio_runtime::setSignalHandler(&AudioRuntimeComponent::onAudioSignal, this);
    const bool started = audio_runtime::start(audio_);
    if (!started) {
        s_supervisorV2.postErrorEvent("audio task init failed", ComponentID::AudioRuntime);
    }
    return started;
}

uint32_t AudioRuntimeComponent::setOFF(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    pendingErrorTarget_ = false;
    audio_.stop();
    return kAudioTimeoutOffMs;
}

uint32_t AudioRuntimeComponent::setIDLE(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    pendingErrorTarget_ = false;
    audio_.stop();
    return kAudioTimeoutIdleMs;
}

uint32_t AudioRuntimeComponent::setSTREAMING(uint32_t transitionId) {
    startPendingTransition(true, transitionId);
    pendingErrorTarget_ = false;

    char station[settings::kStationMaxLen] = {};
    if (!settings::loadStation(station, sizeof(station)) || station[0] == '\0') {
        completePendingTransition(TransitionStatus::Failed, "no station configured");
        return kAudioTimeoutStreamingMs;
    }

    if (!audio_.connectToHost(station)) {
        completePendingTransition(TransitionStatus::Failed, "audio connect failed");
    }

    return kAudioTimeoutStreamingMs;
}

uint32_t AudioRuntimeComponent::setERROR(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    pendingErrorTarget_ = true;
    audio_.stop();
    return kAudioTimeoutErrorMs;
}

void AudioRuntimeComponent::onTransitionTimeout(uint32_t transitionId) {
    (void)transitionId;
    if (transitionPending_ && pendingTransitionId_ == transitionId) {
        audio_.stop();
        completePendingTransition(TransitionStatus::Failed, "timeout");
    }
}

void AudioRuntimeComponent::loop() {
    SystemState target;
    if (supervisorV2Mailbox.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:       setOFF(0); break;
            case SystemState::READY:       setIDLE(0); break;
            case SystemState::CONNECTING:
            case SystemState::LIVE:        setSTREAMING(0); break;
            case SystemState::ERROR:
            case SystemState::FATAL:       setERROR(0); break;
            default: break;
        }
    }

    if (!transitionPending_) return;

    const IAudioPlayer::RuntimeState runtimeState = audio_.runtimeState();
    if (pendingStreamingTarget_) {
        if (runtimeState == IAudioPlayer::RuntimeState::LIVE) {
            completePendingTransition(TransitionStatus::Completed, nullptr);
        } else if (runtimeState == IAudioPlayer::RuntimeState::ERROR) {
            completePendingTransition(TransitionStatus::Failed, "audio runtime error");
        }
        return;
    }

    if (runtimeState == IAudioPlayer::RuntimeState::SLEEP) {
        completePendingTransition(TransitionStatus::Completed, nullptr);
    } else if (runtimeState == IAudioPlayer::RuntimeState::ERROR && !pendingErrorTarget_) {
        completePendingTransition(TransitionStatus::Failed, "audio stop failed");
    }
}

void AudioRuntimeComponent::onAudioSignal(audio_runtime::Signal signal, void* context) {
    auto* self = static_cast<AudioRuntimeComponent*>(context);
    if (!self) return;

    if (signal == audio_runtime::Signal::INIT_OK) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Completed, nullptr);
        }
    } else if (signal == audio_runtime::Signal::STREAM_LOST) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed, "stream lost");
        } else {
            s_supervisorV2.postErrorEvent("stream lost", ComponentID::AudioRuntime);
        }
    } else {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed, "audio init failed");
        } else {
            s_supervisorV2.postErrorEvent("audio init failed", ComponentID::AudioRuntime);
        }
    }
}

void AudioRuntimeComponent::startPendingTransition(bool streamingTarget, uint32_t transitionId) {
    transitionPending_ = true;
    pendingStreamingTarget_ = streamingTarget;
    pendingTransitionId_ = transitionId;
}

void AudioRuntimeComponent::completePendingTransition(TransitionStatus status, const char* reason) {
    if (!transitionPending_) return;
    transitionPending_ = false;
    (void)reason;
    s_supervisorV2.completeTransition(id(), status);
}
```

- [x] **Step 10f.3: Commit**

```bash
git add src/components/composition/system_components.h src/components/composition/system_components.cpp
git commit -m "step 10f: migrate AudioRuntimeComponent to SupervisorV2 mailbox"
```

---

### Task 10g: Clean up main.cpp — remove old Supervisor

**Files:**
- Modify: `src/main.cpp` — remove old `Supervisor s_system`, old `loop()` wiring, simplify component construction

- [x] **Step 10g.1: Rewrite `main.cpp`**

Replace the file with:

```cpp
#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "components/audio/audio_callbacks.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "supervisor/supervisor_v2.h"
#include "components/composition/system_components.h"

namespace {

constexpr const char* kLogSource = "Main";

}  // namespace

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static AudioPlayerESP32 s_playerImpl(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
static IAudioPlayer& s_audio = s_playerImpl;

// ---------------------------------------------------------------------------
// Components — no old Supervisor dependency
// ---------------------------------------------------------------------------
SupervisorV2 s_supervisorV2;
static BoardInfoComponent s_boardInfo;
static WiFiComponent s_wifi;
static AudioRuntimeComponent s_audioRuntime(s_audio);
static CliComponent s_cli(s_audio);

static ISystemComponent* s_components[] = {
    &s_boardInfo,
    &s_wifi,
    &s_audioRuntime,
    &s_cli,
};

// ---------------------------------------------------------------------------
// SupervisorV2 state machine task
// ---------------------------------------------------------------------------

static void stateMachineTask(void* param) {
    auto* supervisorV2 = static_cast<SupervisorV2*>(param);
    supervisorV2->setup();
    for (;;) {
        supervisorV2->run();
    }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

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

void loop() {
    for (ISystemComponent* component : s_components) {
        component->loop();
    }
}
```

Key changes:
- Removed `#include "supervisor/supervisor.h"` — only SupervisorV2 remains
- Removed `static Supervisor s_system;` — old Supervisor gone
- Components no longer pass `s_system` to constructors
- `(void)s_system.setup();` removed
- `s_system.processMailbox();` removed — SupervisorV2 has its own task
- `(void)s_system.setup();` → components now register with SupervisorV2

- [x] **Step 10g.2: Commit**

```bash
git add src/main.cpp
git commit -m "step 10g: remove old Supervisor from main.cpp, wire components to SupervisorV2 only"
```

---

### Task 10h: Remove `invokeComponentTransition` and unused includes

**Files:**
- Modify: `src/components/composition/system_components.cpp` — remove dead code

- [x] **Step 10h.1: Remove `invokeComponentTransition` and old includes**

In `system_components.cpp`, remove:
- The `#include "supervisor/supervisor.h"` line (no longer needed)
- The `invokeComponentTransition()` function (lines 41-60 — only used by old hooks)
- The `extern SupervisorV2 s_supervisorV2;` is already in the file (from component sections)

Add at top after `#include "supervisor/supervisor.h"` removal:
```cpp
#include "supervisor/supervisor_v2.h"

extern SupervisorV2 s_supervisorV2;
```

The extern declaration should appear once, before the first component that uses it (BoardInfoComponent).

- [x] **Step 10h.2: Commit**

```bash
git add src/components/composition/system_components.cpp
git commit -m "step 10h: remove invokeComponentTransition and old Supervisor include"
```

---

### Task 10i: Build and final verification

- [x] **Step 10i.1: Build the production target**

```bash
pio run -e production
```

Expected: BUILD SUCCESS.

- [x] **Step 10i.2: Run the full native test suite**

```bash
pio test -e native
```

Expected: 145 succeeded, 4 pre-existing errors unchanged. Main.cpp changes don't affect native tests (not compiled in native environment).

- [x] **Step 10i.3: Commit final verification**

```bash
git commit --allow-empty -m "step 10i: verify production build and native test suite after old Supervisor removal"
```
