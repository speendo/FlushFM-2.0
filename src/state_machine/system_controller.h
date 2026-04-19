#pragma once

#include <map>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

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

enum class SystemState : uint8_t {
    ERROR = 0,
    BOOTING = 10,
    SLEEP = 20,
    CONNECTING = 30,
    READY = 40,
    LIVE = 50,
};

constexpr uint8_t stateRank(SystemState state) {
    return static_cast<uint8_t>(state);
}

constexpr bool isBelowState(SystemState lhs, SystemState rhs) {
    return stateRank(lhs) < stateRank(rhs);
}

constexpr bool isAtLeastState(SystemState lhs, SystemState rhs) {
    return stateRank(lhs) >= stateRank(rhs);
}

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

enum class TransitionRequestDecision : uint8_t {
    Ignored,
    Started,
    Superseded,
    Queued,
};

class SystemController {
public:
    using StateObserver = std::function<void(SystemState)>;
    using TransitionInvoker = std::function<uint32_t(SystemState, uint32_t)>;
    using TransitionTimeoutHook = std::function<void(uint32_t)>;

    SystemController();

    SystemState state() const;
    void subscribe(StateObserver observer);

    // Thread-safe event enqueue for any core/task.
    // reason carries origin/context metadata for logging and debugging.
    // policy controls queue backpressure strategy: FIRE_AND_FORGET may lose events; BOUNDED_BLOCKING uses
    // a short timeout to signal critical events and fall back to sticky pending flags.
    bool postEvent(SystemEvent event, SystemReason reason, EventPolicy policy = EventPolicy::FIRE_AND_FORGET);

    // Core 0 only: drain the event queue and run transition logic.
    void processEventQueue();

    bool registerComponent(const char* name, bool isRequired);
    bool setComponentTransitionHooks(const char* name,
                                     TransitionInvoker transitionInvoker,
                                     TransitionTimeoutHook timeoutHook);
    ComponentLifecycleStatus getComponentStatus(const char* name) const;
    bool markComponentFailed(const char* name, const char* reason);
    bool isComponentRequired(const char* name) const;
    bool beginComponentTransition(const char* name, uint32_t transitionId);
    bool reportCompletion(const char* componentName,
                          uint32_t transitionId,
                          TransitionStatus status,
                          DebugReason reason);
    TransitionRequestDecision requestTransition(SystemState from, SystemState target, uint32_t transitionId);
    bool finishTransition(uint32_t transitionId);
    bool beginOrchestration(SystemState target,
                            SystemEvent trigger,
                            SystemReason reason,
                            uint32_t transitionId);
    bool isOrchestrationActive() const;
    size_t componentsWaitingForCompletion() const;
    bool hasActiveTransition() const;
    bool hasQueuedTransition() const;
    uint32_t activeTransitionId() const;
    SystemState activeTransitionFrom() const;
    SystemState activeTransitionTarget() const;
    uint32_t queuedTransitionId() const;
    SystemState queuedTransitionFrom() const;
    SystemState queuedTransitionTarget() const;

private:
    struct QueuedEvent {
        SystemEvent event;
        SystemReason reason;
    };

    static std::string normalizeComponentName(const char* name);
    static void copyFailureReason(char* destination, size_t destinationSize, const char* reason);

    struct PendingComponentTransition {
        uint32_t transitionId = 0;
        uint32_t timeoutMs = 0;
        uint32_t startedAtMs = 0;
        bool timeoutHandled = false;
    };

    struct ComponentTransitionHooks {
        TransitionInvoker transitionInvoker;
        TransitionTimeoutHook timeoutHook;
    };

    struct StateTransitionInfo {
        uint32_t transitionId = 0;
        SystemState from = SystemState::BOOTING;
        SystemState target = SystemState::BOOTING;
    };

    struct OrchestrationContext {
        bool active = false;
        uint32_t transitionId = 0;
        SystemState target = SystemState::BOOTING;
        SystemEvent trigger = SystemEvent::BOOT;
        SystemReason reason = SystemReason::NONE;
        bool requiredFailure = false;
    };

    void handleEvent(SystemEvent event, SystemReason reason);
    void transitionTo(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId = 0);
    void checkTransitionTimeouts();

    SystemState state_ = SystemState::BOOTING;
    bool transientError_ = false;
    QueueHandle_t queue_ = nullptr;
    std::vector<QueuedEvent> deferredIntentEvents_{};
    std::vector<StateObserver> observers_;
    bool pendingCriticalEvent_ = false;  // Sticky flag for critical event loss recovery.
    SystemEvent pendingEvent_ = SystemEvent::BOOT;
    SystemReason pendingReason_ = SystemReason::NONE;
    uint32_t nextTransitionId_ = 1;
    std::map<std::string, ComponentRegistryEntry> componentRegistry_;
    std::map<std::string, PendingComponentTransition> pendingTransitions_;
    std::map<std::string, ComponentTransitionHooks> componentHooks_;
    bool hasActiveStateTransition_ = false;
    StateTransitionInfo activeStateTransition_{};
    bool hasQueuedStateTransition_ = false;
    StateTransitionInfo queuedStateTransition_{};
    OrchestrationContext orchestration_{};
    bool pendingReplayRequested_ = false;
    bool deferredReplayEvent_ = false;
    bool pendingPlayAfterReady_ = false;
    bool deferredPlayAfterReadyEvent_ = false;
    bool startupWiFiReady_ = false;
    bool startupAudioReady_ = false;
};

const char* toString(SystemState state);
const char* toString(SystemEvent event);
const char* toString(SystemReason reason);
const char* toString(TransitionRequestDecision decision);
