# Rule: State Management
[Status: Active | Updated: 2026-03-11]
**Context:** ESP32-S3 / FreeRTOS | **Goal:** Centralize system control while ensuring thread-safe; decoupled component communication

---

## 1. Core Rules
### States
- **Orchestration:** Use a single SystemController (Core 0) as the "Single Source of Truth" to manage the following states
    - **`OFF`:** deep sleep Tier 2; WiFi dropped; ULP active (→ `software-architecture.md`)
    - **`STARTING`:** initializing WiFi
    - **`IDLE`:** WiFi ready; Switched Domain (Display/DAC) powered off via Relay; Light Sleep Tier 1 (→ `software-architecture.md`, `hardware.md`)
    - **`STREAMING`:** playing audio, display on
    - **`ERROR`:** recoverable failure
- **Transitions:** Use explicit event-driven triggers via callbacks, ISRs, or FreeRTOS task notifications
- **Local Encapsulation:** Components must own their internal state and provide read-only access via public getters (→ `modularity.md`)
- **Error Handling:** Clear transient error states when entering states `OFF`, `STARTING` or `IDLE` to prepare a clean restart

### Events
- **LDR Master Trigger:** Treat the light sensor as a hardware-level interrupt; implement light sensor logic as the highest priority event
- **Timer expiration:** Use timer expiration events where time based transitions are needed (e.g. sleep timeout)
- **User interactions:** User interaction events (e.g. volume control, switching channel) must be passed to the `SystemController`
- **System events:** Use specific event types for logic-driven changes (e.g. `STREAM_LOST`, `WIFI_DISCONNECTED`, `LOW_MEMORY`)

### Data Handling
- **Persistence:** Save critical runtime data (WiFi credentials; last station; volume) to `NVS` (Non-Volatile Storage)
- **Cross-Core Sync:** Core 1 must never call `setState()` directly; signal state changes via FreeRTOS queue to Core 0 (→ `concurrency.md`)
- **Observer Pattern:** Components register callbacks on `SystemController` and are injected via constructor; never poll state (→ `modularity.md`)

## 2. Constraints & Exceptions
- **Limit:** Use FreeRTOS queues for Core 1 → Core 0 signals; use Mutexes for shared peripheral resources — never use raw shared variables (→ `concurrency.md`)
- **Limit:** Use NVS only for critical user-settings, avoid frequent writes
- **Exception:** The boot sequence may bypass formal state transitions during hardware initialization before the scheduler starts
- **Never:** Use global variables or singletons for state storage; inject the controller instance into components via constructors
- **Never:** Use continuous polling of state variables
- **Never:** Allow a component to directly modify another component's state (→ `modularity.md`)

## 3. Reference Pattern

```cpp
// Minimal structural skeleton – SystemController runs on Core 0 exclusively.
// State is never written from Core 1; commands arrive via queue (see concurrency.md).
class SystemController {
private:
    SystemState currentState_{OFF};  // Core 0 only – no atomic needed
    SemaphoreHandle_t observerMutex_ = xSemaphoreCreateMutex();
    std::vector<std::function<void(SystemState)>> observers_;

public:
    void setState(SystemState s) {
        if (s != currentState_) {
            currentState_ = s;
            xSemaphoreTake(observerMutex_, portMAX_DELAY);
            for (auto& cb : observers_) cb(s); // Notify UI/Hardware on Core 0
            xSemaphoreGive(observerMutex_);
        }
    }

    SystemState getState() const { return currentState_; }
};

// Command passing to Audio component
void playNewStation(const char* url) {
    AudioCommand cmd = { .type = PLAY_URL };
    strncpy(cmd.data, url, sizeof(cmd.data));
    xQueueSend(audioQueue, &cmd, portMAX_DELAY);
}