# Guideline: State Management

> **Status:** Active  
> **Last updated:** 2026-02-28

---

## Purpose

Defines how FlushFM 2.0 manages system-level state and coordinates between components while maintaining loose coupling.

---

## Rules

1. **Centralized system state:** A single SystemController component owns and manages the main system state machine with states: OFF (sleeping), STARTING (initializing WiFi), CONNECTED (WiFi ready, no audio), STREAMING (playing audio), ERROR (recoverable failures). Transitions are bidirectional between adjacent states as appropriate.

2. **Error recovery strategy:** Graceful degradation with limited retries. Errors are displayed on screen when possible. Light sensor OFF always takes priority and can interrupt error recovery at any time, clearing error state for fresh restart on next activation.

2. **Component-local state:** Individual components own their internal state. State may be readable via public getter methods for debugging and status purposes, but other components must not directly modify another component's state.

3. **State transitions are explicit:** State changes are triggered by specific events or conditions. Avoid continuous polling – use event-driven design with callbacks, interrupts, or timer expiration events where time-based transitions are needed (e.g. sleep timeout).

4. **No global state variables:** State is owned by specific components – avoid global variables or singletons for state storage.

5. **State persistence:** Critical configuration (WiFi credentials, last station) is stored in ESP32 NVS (Non-Volatile Storage) and restored on startup.

6. **State observation via callbacks:** Components that need to react to state changes register callback functions rather than polling for changes.

7. **Cross-task state changes must use FreeRTOS queues.** Never modify SystemController state directly from a Core 1 task – send a command via queue to Core 0 instead.

8. **Cross-core data sharing requires explicit synchronization.** Use FreeRTOS queues for producer-consumer message-passing (commands, notifications, events) where you need message history or sequence. Use FreeRTOS mutexes for shared state variables that represent current values (volume, connection status, current track title) – even if only one core writes to them. Choose based on whether you need message flow or shared state access.

---

## Rationale

Centralized system state provides a clear ownership model and single source of truth for high-level system behavior, while allowing components to maintain their internal complexity privately.

Callback-based state observation enables loose coupling – components can react to state changes without the SystemController needing to know who is interested.

NVS persistence ensures the device remembers user preferences (WiFi, last station) across both sleep tiers and power cycles.

Explicit thread safety rules (queues, mutexes) prevent race conditions when the audio Core 1 task needs to signal state changes (e.g. stream connected, metadata received) back to the system Core 0.

---

## Exceptions

- **Startup initialization** may directly set initial state values before the callback system is established.
- **Emergency error conditions** may force immediate state transitions to recover system stability.

---

## Examples

```cpp
// Good: Centralized state management with callbacks
enum SystemState {
    OFF,
    STARTING,
    CONNECTED,
    STREAMING,
    ERROR
};

class SystemController {
private:
    SystemState currentState_ = OFF;
    std::vector<std::function<void(SystemState)>> stateCallbacks_;
    
public:
    void registerCallback(std::function<void(SystemState)> callback) {
        stateCallbacks_.push_back(callback);
    }
    
    void setState(SystemState newState) {
        if (newState != currentState_) {
            DEBUG_LOG("System state: %d -> %d", currentState_, newState);
            currentState_ = newState;
            
            // Notify all registered observers
            for (auto& callback : stateCallbacks_) {
                callback(newState);
            }
        }
    }
    
    SystemState getState() const { return currentState_; }
};

// Component reacts to state changes via callback
class DisplayManager {
public:
    void onSystemStateChanged(SystemState state) {
        switch (state) {
            case STARTING:
                showStartupScreen();
                break;
            case STREAMING:
                showNowPlaying();
                break;
            case ERROR:
                showErrorMessage();
                break;
        }
    }
};
```