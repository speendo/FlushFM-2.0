#pragma once

#include <map>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#else
using QueueHandle_t = void*;
using TickType_t = uint32_t;
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr TickType_t pdMS_TO_TICKS(uint32_t milliseconds) { return milliseconds; }
#endif

#include "component_types.h"

enum class SystemState {
    OFF,
    STARTING,
    IDLE,
    STREAMING,
    ERROR,
};

enum class SystemEvent {
    BOOT,
    COMPONENT_SETUP_FAILED,
    WIFI_READY,
    AUDIO_INIT_OK,
    AUDIO_INIT_FAILED,
    PLAY_REQUESTED,
    STOP_REQUESTED,
    WIFI_DISCONNECTED,
    STREAM_LOST,
    RECOVER,
    ENTER_OFF,
};

// Event posting policy: controls how postEvent handles queue backpressure.
enum class EventPolicy {
    FIRE_AND_FORGET,      // Non-blocking; silent loss acceptable for optional events.
    BOUNDED_BLOCKING,     // Short timeout for critical state-machine events; ERROR_LOG on failure.
};

enum class SystemReason {
    // Event origin/context used for transition telemetry and diagnostics.
    // This is not a causal rule engine input.
    NONE,
    BOOT_SEQUENCE,
    COMPONENT_SETUP,
    WIFI_INITIALIZED,
    AUDIO_TASK_STARTED,
    AUDIO_TASK_INIT_FAILED,
    USER_REQUEST,
    RECOVERY,
    POWER_POLICY,
};

class SystemController {
public:
    using StateObserver = std::function<void(SystemState)>;

    SystemController();

    SystemState state() const;
    void subscribe(StateObserver observer);

    // Thread-safe event enqueue for any core/task.
    // reason carries origin/context metadata for logging and debugging.
    // policy controls queue backpressure strategy: FIRE_AND_FORGET may lose events; BOUNDED_BLOCKING uses
    // a short timeout to signal critical events and fall back to sticky pending flags.
    bool postEvent(SystemEvent event, SystemReason reason, EventPolicy policy = EventPolicy::FIRE_AND_FORGET);

    // Core 0 only: process pending events and run transition logic.
    void dispatchPending();

    bool registerComponent(const char* name, bool isRequired);
    ComponentLifecycleStatus getComponentStatus(const char* name) const;
    bool markComponentFailed(const char* name, const char* reason);
    bool isComponentRequired(const char* name) const;

private:
    struct QueuedEvent {
        SystemEvent event;
        SystemReason reason;
    };

    static std::string normalizeComponentName(const char* name);
    static void copyFailureReason(char* destination, size_t destinationSize, const char* reason);

    void handleEvent(SystemEvent event, SystemReason reason);
    void transitionTo(SystemState next, SystemEvent trigger, SystemReason reason);

    SystemState state_ = SystemState::OFF;
    bool transientError_ = false;
    QueueHandle_t queue_ = nullptr;
    std::vector<StateObserver> observers_;
    bool pendingCriticalEvent_ = false;  // Sticky flag for critical event loss recovery.
    SystemEvent pendingEvent_ = SystemEvent::BOOT;
    SystemReason pendingReason_ = SystemReason::NONE;
    std::map<std::string, ComponentRegistryEntry> componentRegistry_;
};

const char* toString(SystemState state);
const char* toString(SystemEvent event);
const char* toString(SystemReason reason);
