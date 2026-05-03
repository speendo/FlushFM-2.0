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

#define SYSTEM_STATE_X(V) \
    V(ERROR, 0) \
    V(BOOTING, 10) \
    V(SLEEP, 20) \
    V(CONNECTING, 30) \
    V(READY, 40) \
    V(LIVE, 50)

#define SYSTEM_STATE_ENUM(name, value) name = value,
enum class SystemState : uint8_t {
    SYSTEM_STATE_X(SYSTEM_STATE_ENUM)
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

inline const char* toString(SystemState state) {
    switch (state) {
#define SYSTEM_STATE_STRING(name, value) case SystemState::name: return #name;
        SYSTEM_STATE_X(SYSTEM_STATE_STRING)
#undef SYSTEM_STATE_STRING
    }
    return "UNKNOWN";
}

#undef SYSTEM_STATE_ENUM
#undef SYSTEM_STATE_X

#define SYSTEM_EVENT_X(V) \
    V(BOOT) \
    V(COMPONENT_SETUP_FAILED) \
    V(WIFI_READY) \
    V(AUDIO_INIT_OK) \
    V(AUDIO_INIT_FAILED) \
    V(PLAY_REQUESTED) \
    V(STOP_REQUESTED) \
    V(WIFI_DISCONNECTED) \
    V(STREAM_LOST) \
    V(RECOVER) \
    V(ENTER_SLEEP)

#define SYSTEM_EVENT_ENUM(name) name,
enum class SystemEvent {
    SYSTEM_EVENT_X(SYSTEM_EVENT_ENUM)
};

inline const char* toString(SystemEvent event) {
    switch (event) {
#define SYSTEM_EVENT_STRING(name) case SystemEvent::name: return #name;
        SYSTEM_EVENT_X(SYSTEM_EVENT_STRING)
#undef SYSTEM_EVENT_STRING
    }
    return "UNKNOWN";
}

#undef SYSTEM_EVENT_ENUM
#undef SYSTEM_EVENT_X

// Event posting policy: controls how postEvent handles queue backpressure.
#define EVENT_POLICY_X(V) \
    V(FIRE_AND_FORGET) \
    V(BOUNDED_BLOCKING)

#define EVENT_POLICY_ENUM(name) name,
enum class EventPolicy {
    EVENT_POLICY_X(EVENT_POLICY_ENUM)
};

inline const char* toString(EventPolicy policy) {
    switch (policy) {
#define EVENT_POLICY_STRING(name) case EventPolicy::name: return #name;
        EVENT_POLICY_X(EVENT_POLICY_STRING)
#undef EVENT_POLICY_STRING
    }
    return "UNKNOWN";
}

#undef EVENT_POLICY_ENUM
#undef EVENT_POLICY_X

#define SYSTEM_REASON_X(V) \
    V(NONE) \
    V(BOOT_SEQUENCE) \
    V(COMPONENT_SETUP) \
    V(WIFI_INITIALIZED) \
    V(AUDIO_TASK_STARTED) \
    V(AUDIO_TASK_INIT_FAILED) \
    V(USER_REQUEST) \
    V(RECOVERY) \
    V(POWER_POLICY)

#define SYSTEM_REASON_ENUM(name) name,
enum class SystemReason {
    // Event origin/context used for transition telemetry and diagnostics.
    // This is not a causal rule engine input.
    SYSTEM_REASON_X(SYSTEM_REASON_ENUM)
};

inline const char* toString(SystemReason reason) {
    switch (reason) {
#define SYSTEM_REASON_STRING(name) case SystemReason::name: return #name;
        SYSTEM_REASON_X(SYSTEM_REASON_STRING)
#undef SYSTEM_REASON_STRING
    }
    return "UNKNOWN";
}

#undef SYSTEM_REASON_ENUM
#undef SYSTEM_REASON_X

#define TRANSITION_REQUEST_DECISION_X(V) \
    V(Ignored) \
    V(Started) \
    V(Superseded) \
    V(Queued)

#define TRANSITION_REQUEST_DECISION_ENUM(name) name,
enum class TransitionRequestDecision : uint8_t {
    TRANSITION_REQUEST_DECISION_X(TRANSITION_REQUEST_DECISION_ENUM)
};

inline const char* toString(TransitionRequestDecision decision) {
    switch (decision) {
#define TRANSITION_REQUEST_DECISION_STRING(name) case TransitionRequestDecision::name: return #name;
        TRANSITION_REQUEST_DECISION_X(TRANSITION_REQUEST_DECISION_STRING)
#undef TRANSITION_REQUEST_DECISION_STRING
    }
    return "UNKNOWN";
}

#undef TRANSITION_REQUEST_DECISION_ENUM
#undef TRANSITION_REQUEST_DECISION_X

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
    // policy controls queue backpressure strategy; the non-blocking mode may lose events while the
    // bounded mode waits briefly and falls back to sticky pending flags.
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
    SystemState targetState_ = SystemState::SLEEP;
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
    bool startupWiFiReady_ = false;
    bool startupAudioReady_ = false;
};
