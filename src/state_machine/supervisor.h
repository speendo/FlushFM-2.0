#pragma once

#include <functional>
#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#endif

#include "component_types.h"

#define SYSTEM_STATE_X(V) \
    V(FATAL, 0) \
    V(ERROR, 5) \
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

class Supervisor {
public:
    using StateObserver = std::function<void(SystemState)>;
    using TransitionInvoker = std::function<uint32_t(SystemState, uint32_t)>;
    using TransitionTimeoutHook = std::function<void(uint32_t)>;

    Supervisor();

    SystemState state() const;
    void subscribe(StateObserver observer);

    // Thread-safe event enqueue for any core/task.
    // reason carries origin/context metadata for logging and debugging.
    bool postEvent(SystemEvent event, SystemReason reason);

    // TargetMode (goal) and ObservedState (current reality):
    // state() returns the CONFIRMED state (observedState_), not the goal.
    // The goal (targetMode_) is set by user intents in handleEvent().
    // The step target is stored in orchestration_.target.
    // observedState_ advances only in transitionTo(), which is called
    // from direct-event paths or from reportCompletion after confirmation.

#if !defined(ARDUINO)
    // Native-only: writes to the Mailbox slot without dispatching.
    // Used by unit tests to verify Mailbox last-write-wins semantics.
    void postEventBuffered(SystemEvent event, SystemReason reason);

    // Native-only: enters FATAL state for unit testing the halt gate.
    // Production entry to FATAL will go through beginOrchestration(FATAL).
    void triggerFatal();
#endif

    // Core 0 only: drain the Mailbox slot and run transition logic.
    void processMailbox();

    bool registerComponent(ComponentID id, bool isRequired);
    bool setComponentTransitionHooks(ComponentID id,
                                     TransitionInvoker transitionInvoker,
                                     TransitionTimeoutHook timeoutHook);
    ComponentLifecycleStatus getComponentStatus(ComponentID id) const;
    bool isComponentRequired(ComponentID id) const;
    bool reportCompletion(ComponentID id,
                          uint32_t transitionId,
                          TransitionStatus status,
                          DebugReason reason);
    bool beginComponentTransition(ComponentID id, uint32_t transitionId);
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
    struct Mailbox {
        SystemEvent event = SystemEvent::BOOT;
        SystemReason reason = SystemReason::NONE;
        bool pending = false;
    };

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

    SystemState observedState_ = SystemState::BOOTING;
    SystemState targetMode_ = SystemState::SLEEP;
    bool transientError_ = false;
    Mailbox mailbox_{};
    std::vector<StateObserver> observers_;
    uint32_t nextTransitionId_ = 1;
    std::array<ComponentRegistryEntry, static_cast<size_t>(ComponentID::Count)> componentRegistry_{};
    std::array<PendingComponentTransition, static_cast<size_t>(ComponentID::Count)> pendingTransitions_{};
    std::array<ComponentTransitionHooks, static_cast<size_t>(ComponentID::Count)> componentHooks_{};
    bool hasActiveStateTransition_ = false;
    StateTransitionInfo activeStateTransition_{};
    bool hasQueuedStateTransition_ = false;
    StateTransitionInfo queuedStateTransition_{};
    OrchestrationContext orchestration_{};
};
