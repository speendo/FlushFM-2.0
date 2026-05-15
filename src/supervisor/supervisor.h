#pragma once

#ifndef PRODUCTION_BUILD

#include <functional>
#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#endif

#include "component_types.h"

#define COMPONENT_TYPES_LIFECYCLE_STATUS_X(V) \
    V(Unknown) \
    V(Ready) \
    V(Failed) \
    V(Disabled)

#define COMPONENT_TYPES_LIFECYCLE_STATUS_ENUM(name) name,
enum class ComponentLifecycleStatus : uint8_t {
    COMPONENT_TYPES_LIFECYCLE_STATUS_X(COMPONENT_TYPES_LIFECYCLE_STATUS_ENUM)
};

inline const char* toString(ComponentLifecycleStatus status) {
    switch (status) {
#define COMPONENT_TYPES_LIFECYCLE_STATUS_STRING(name) case ComponentLifecycleStatus::name: return #name;
        COMPONENT_TYPES_LIFECYCLE_STATUS_X(COMPONENT_TYPES_LIFECYCLE_STATUS_STRING)
#undef COMPONENT_TYPES_LIFECYCLE_STATUS_STRING
    }
    return "UNKNOWN";
}

#undef COMPONENT_TYPES_LIFECYCLE_STATUS_ENUM
#undef COMPONENT_TYPES_LIFECYCLE_STATUS_X

struct ComponentRegistryEntry {
    ComponentLifecycleStatus lifeCycleStatus = ComponentLifecycleStatus::Unknown;
    bool isRequired = false;
    bool isDisabled = false;
    bool isRegistered = false;
    const char* lastFailureReason = nullptr;
};

struct ComponentStateMatrix {
    uint32_t minState;
    uint32_t maxState;
    uint32_t forwardTimeoutMs;
    uint32_t backwardTimeoutMs;
};

constexpr uint32_t TARGET_MODE = 0xFF;

/** Extract the numeric rank of a SystemState.
 *  @param state The state to query.
 *  @return The uint8_t rank value (0, 10, 20, ... 60). */
constexpr uint8_t stateRank(SystemState state) {
    return static_cast<uint8_t>(state);
}

/** Human-readable name for a SystemState.
 *  @param state The state to convert.
 *  @return Pointer to a static string literal, or "UNKNOWN" for invalid values. */
inline const char* toString(SystemState state) {
    switch (state) {
#define SYSTEM_STATE_STRING(name, value) case SystemState::name: return #name;
        SYSTEM_STATE_X(SYSTEM_STATE_STRING)
#undef SYSTEM_STATE_STRING
    }
    return "UNKNOWN";
}

/** @} */

#undef SYSTEM_STATE_ENUM
#undef SYSTEM_STATE_X

/** @defgroup supervisor_event System Events
 *  Event types that trigger state transitions in the Supervisor.
 *  @{ */

/** X-macro generating the SystemEvent enum.
 *  Events are the triggers fed into handleEvent(). Most semantic
 *  payload is carried via STATE_REQUESTED with a SystemState target. */
#define SYSTEM_EVENT_X(V) \
    V(COMPONENT_SETUP_FAILED) \
    V(STATE_REQUESTED)

#define SYSTEM_EVENT_ENUM(name) name,

/** Events that can be posted to the Supervisor state machine.
 *  - COMPONENT_SETUP_FAILED: A component reported failure outside a
 *    transition context. Triggers immediate ERROR (or FATAL if already ERROR).
 *  - STATE_REQUESTED: Request to transition to a target state supplied
 *    as a 3-param payload in postEvent(). */
enum class SystemEvent {
    SYSTEM_EVENT_X(SYSTEM_EVENT_ENUM)
};

/** Human-readable name for a SystemEvent.
 *  @param event The event to convert.
 *  @return Pointer to a static string literal, or "UNKNOWN" for invalid values. */
inline const char* toString(SystemEvent event) {
    switch (event) {
#define SYSTEM_EVENT_STRING(name) case SystemEvent::name: return #name;
        SYSTEM_EVENT_X(SYSTEM_EVENT_STRING)
#undef SYSTEM_EVENT_STRING
    }
    return "UNKNOWN";
}

/** @} */

#undef SYSTEM_EVENT_ENUM
#undef SYSTEM_EVENT_X

/** @defgroup supervisor_reason System Reasons
 *  Metadata tags annotating why an event was posted.
 *  Reasons are NOT used in transition logic — they exist solely
 *  for telemetry, logging, and diagnostics.
 *  @{ */

/** X-macro generating the SystemReason enum. */
#define SYSTEM_REASON_X(V) \
    V(NONE) \
    V(COMPONENT_SETUP) \
    V(WIFI_INITIALIZED) \
    V(AUDIO_TASK_STARTED) \
    V(AUDIO_TASK_INIT_FAILED) \
    V(USER_REQUEST) \
    V(RECOVERY) \
    V(POWER_POLICY)

#define SYSTEM_REASON_ENUM(name) name,

/** Origin/context metadata for transition telemetry.
 *  These values annotate the cause of a transition but do not
 *  influence the state machine's decision logic — they are purely
 *  diagnostic. */
enum class SystemReason {
    SYSTEM_REASON_X(SYSTEM_REASON_ENUM)
};

/** Human-readable name for a SystemReason.
 *  @param reason The reason to convert.
 *  @return Pointer to a static string literal, or "UNKNOWN" for invalid values. */
inline const char* toString(SystemReason reason) {
    switch (reason) {
#define SYSTEM_REASON_STRING(name) case SystemReason::name: return #name;
        SYSTEM_REASON_X(SYSTEM_REASON_STRING)
#undef SYSTEM_REASON_STRING
    }
    return "UNKNOWN";
}

/** @} */

#undef SYSTEM_REASON_ENUM
#undef SYSTEM_REASON_X

/** Central state machine orchestrator for FlushFM 2.0.
 *
 *  The Supervisor manages system-wide state transitions by coordinating
 *  registered components. It maintains two state views:
 *    - observedState_: the CONFIRMED current state (returned by state())
 *    - targetMode_:    the INTENDED goal state set by external requests
 *
 *  Transitions are either:
 *    1. Orchestrated: beginOrchestration() dispatches work to all
 *       registered components and waits for reportCompletion() from each.
 *    2. Immediate: setObservedStateImmediate() bypasses component coordination
 *       and directly updates the observed state (used for ERROR/FATAL).
 *
 *  Thread safety:
 *    - postEvent() is designed to be callable from any core/task (Arduino
 *      path defers via mailbox; native path dispatches synchronously).
 *    - processMailbox() and setup() are Core 0 only.
 *    - No explicit synchronization primitives are used — mailbox writes
 *      are assumed non-concurrent in practice.
 */
class Supervisor {
public:
    /** Callback invoked on every confirmed state change.
     *  Receives the new SystemState. Must not block or allocate. */
    using StateObserver = std::function<void(SystemState)>;

    /** Function called per component during beginOrchestration().
     *  @param target The SystemState the component should transition to.
     *  @param transitionId Unique ID for this transition.
     *  @return Timeout in milliseconds, or UINT32_MAX for no timeout.
     *          Note: returning 0 means "immediate timeout" — use UINT32_MAX. */
    using TransitionInvoker = std::function<uint32_t(SystemState, uint32_t)>;

    /** Callback invoked when a component's transition exceeds its timeout.
     *  @param transitionId The timed-out transition ID. */
    using TransitionTimeoutHook = std::function<void(uint32_t)>;

    /** Construct a Supervisor with default state (BOOTING, target SLEEP). */
    Supervisor();

    /** Return the currently confirmed system state.
     *  @return The observed state (observedState_), not the goal (targetMode_). */
    SystemState state() const;

    /** Subscribe to state change notifications.
     *  @param observer Callable invoked synchronously on every
     *         setObservedStateImmediate() call. Cannot be unsubscribed. */
    void subscribe(StateObserver observer);

    /** Post an event for asynchronous processing.
     *  On Arduino: writes to the mailbox; processed on next processMailbox().
     *  On native:  dispatches synchronously via handleEvent().
     *  @param event  The event type.
     *  @param reason Origin/context metadata (telemetry only).
     *  @return true. Note: on Arduino, last-writer-wins — a second event
     *          before processMailbox() silently overwrites the first.
     *  @note The 2-param overload does NOT set mailbox_.targetState.
     *        For STATE_REQUESTED, prefer the 3-param overload. */
    bool postEvent(SystemEvent event, SystemReason reason);

    /** Post a STATE_REQUESTED event with an explicit target state.
     *  Rejects targets that are transient internal stepping states
     *  (BOOTING, CONNECTING).
     *  @param event  Must be STATE_REQUESTED for meaningful behavior.
     *         Other events ignore the target parameter.
     *  @param reason Origin/context metadata.
     *  @param target The desired SystemState to transition toward.
     *  @return true on accept, false if target is a transient state
     *          (BOOTING or CONNECTING). */
    bool postEvent(SystemEvent event, SystemReason reason, SystemState target);

    /** Record an asynchronous error event.
     *  First-writer-wins: subsequent calls are silently ignored until
     *  the pending error is drained by processMailbox().
     *  @param reason Human-readable failure description (dangling pointer risk).
     *  @param source The ComponentID that generated the error. */
    void setErrorEvent(DebugReason reason, ComponentID source);

#if !defined(ARDUINO)
    /** Write event to mailbox without dispatching (native/test only).
     *  Used by unit tests to verify mailbox last-write-wins semantics.
     *  Does NOT set mailbox_.targetState — mirrors Arduino 2-param postEvent(). */
    void postEventBuffered(SystemEvent event, SystemReason reason);

    /** Write event+target to mailbox without dispatching (native/test only).
     *  Used by unit tests to verify mailbox last-write-wins semantics. */
    void postEventBuffered(SystemEvent event, SystemReason reason, SystemState target);

    /** Immediately enter FATAL state (native/test only).
     *  Bypasses all event routing and orchestration. Tests the halt gate. */
    void triggerFatal();

    /** Read the configured timeout for a component's pending transition.
     *  @param id The component to query.
     *  @return timeoutMs, or 0 if ComponentID::Count or no pending transition. */
    uint32_t getPendingTimeout(ComponentID id) const;
#endif

    /** Idempotent boot entry (Core 0 only).
     *  Called once from main::setup(). Guards on observedState_ == BOOTING.
     *  Sets target to LIVE, transitions observed state to CONNECTING,
     *  then begins orchestration toward READY.
     *  Safe to call multiple times — subsequent calls are no-ops. */
    void setup();

    /** Drain the mailbox and check error/timeout state (Core 0 only).
     *  Sequence: (1) FATAL gate → return; (2) drain mailbox → handleEvent();
     *  (3) drain errorEvent → ERROR state; (4) checkTransitionTimeouts(). */
    void processMailbox();

    /** Register a component with the Supervisor.
     *  @param id         The component's unique identifier.
     *  @param isRequired If true, component failure during orchestration
     *                    causes the orchestration to fail (ERROR state).
     *  @return false if ComponentID::Count, true otherwise.
     *  @note Double-registration silently overwrites the previous entry.
     *        Lifecycle status is reset to Unknown. */
    bool registerComponent(ComponentID id, bool isRequired);

    /** Register transition hooks for a component.
     *  @param id        The component's identifier.
     *  @param transitionInvoker Called by beginOrchestration() to start
     *         the component's work toward a target state.
     *  @param timeoutHook       Called when a component's transition exceeds
     *         its timeout. Must not block.
     *  @param stateMatrix       Optional matrix mapping state ranks to
     *         forward/backward timeout values. Indexed by stateRank/10.
     *         Must outlive the Supervisor (no ownership transfer).
     *  @param stateMatrixSize   Number of entries in stateMatrix.
     *  @return false if ComponentID::Count, true otherwise. */
    bool setComponentTransitionHooks(ComponentID id,
                                     TransitionInvoker transitionInvoker,
                                     TransitionTimeoutHook timeoutHook,
                                     const ComponentStateMatrix* stateMatrix = nullptr,
                                     size_t stateMatrixSize = 0);

    /** Query a component's lifecycle status.
     *  @param id The component to query.
     *  @return ComponentLifecycleStatus, or Unknown for ComponentID::Count. */
    ComponentLifecycleStatus getComponentStatus(ComponentID id) const;

    /** Check whether a component is marked as required.
     *  @param id The component to query.
     *  @return true if isRequired was set during registration. */
    bool isComponentRequired(ComponentID id) const;

    /** Report completion (or failure) of a component's transition.
     *
     *  This is the primary callback invoked by components after their
     *  TransitionInvoker completes.
     *
     *  @param id           The reporting component.
     *  @param transitionId Must match the in-flight transition. Stale
     *         completions (wrong ID, or no in-flight transition) are rejected.
     *  @param status       Completed or Failed.
     *  @param reason       Failure description (stored verbatim; dangling
     *         pointer risk if dynamically allocated).
     *  @return false if ComponentID::Count, no in-flight transition, or
     *          transition ID mismatch. true otherwise.
     *
     *  Side effects:
     *    - Success: lifecycle → Ready, clears disabled/reason.
     *    - Failure: lifecycle → Failed, marks disabled, stores reason.
     *      If the component is required and belongs to the active
     *      orchestration, marks orchestration_.requiredFailure.
     *    - When ALL pending components have reported (or none remain),
     *      finalizes the orchestration: setObservedStateImmediate()
     *      to the orchestration target (or ERROR on required failure),
     *      then steps toward the saved targetMode_. */
    bool reportCompletion(ComponentID id,
                          uint32_t transitionId,
                          TransitionStatus status,
                          DebugReason reason);

    /** Mark a component as having an in-flight transition (non-orchestration path).
     *  Fails if the component already has a non-zero transitionId.
     *  @note Does NOT set startedAtMs or timeoutMs — timeouts are
     *        only managed within beginOrchestration(). This method is
     *        for components self-managing their own transition lifecycle.
     *  @param id           The component to arm.
     *  @param transitionId Unique transition identifier.
     *  @return false if ComponentID::Count or transition already in-flight. */
    bool beginComponentTransition(ComponentID id, uint32_t transitionId);

    /** Gate for claiming or reversing a state transition.
     *
     *  Rules:
     *    1. from == target → false (no-op).
     *    2. No active transition → claim and store {from, target, id}.
     *    3. Active transition AND target == activeStateTransition_.from
     *       → reversal: overwrite with new {from, target, id}.
     *    4. Otherwise → false (reject).
     *
     *  @param from          The source state (should match observedState_).
     *  @param target        The destination state.
     *  @param transitionId  Unique transition identifier.
     *  @return true if the transition was accepted or reversed. */
    bool requestTransition(SystemState from, SystemState target, uint32_t transitionId);

    /** Mark an active state transition as finished.
     *  @param transitionId Must match the active transition ID.
     *  @return false if no active transition or ID mismatch. */
    bool finishTransition(uint32_t transitionId);

    /** Begin a coordinated multi-component orchestration.
     *
     *  Sequence:
     *    1. Calls requestTransition() — returns false if rejected.
     *    2. Clears all pending transitions and marks all registered,
     *       non-disabled components as pending.
     *    3. If zero components registered: immediately transitions to
     *       target, finishes the transition, then steps toward targetMode_.
     *    4. Otherwise: invokes each component's TransitionInvoker,
     *       resolves timeout (matrix > invoker), sets startedAtMs.
     *
     *  @param target        The state to orchestrate toward.
     *  @param trigger       The event that triggered this orchestration.
     *  @param reason        Origin metadata for telemetry.
     *  @param transitionId  Unique transition identifier.
     *  @return false if requestTransition() rejected the transition. */
    bool beginOrchestration(SystemState target,
                            SystemEvent trigger,
                            SystemReason reason,
                            uint32_t transitionId);

    /** Check whether an orchestration is currently in flight.
     *  @return true if orchestration_.active is set. */
    bool isOrchestrationActive() const;

    /** Count components still waiting to report completion.
     *  @return Number of pending transitions with non-zero transitionId. */
    size_t componentsWaitingForCompletion() const;

    /** Check whether a state transition is currently active.
     *  @return true if hasActiveStateTransition_ is set. */
    bool hasActiveTransition() const;

    /** Get the active state transition's ID.
     *  @return The transitionId from activeStateTransition_. */
    uint32_t activeTransitionId() const;

    /** Get the source state of the active transition.
     *  @return The `from` field of activeStateTransition_. */
    SystemState activeTransitionFrom() const;

    /** Get the target state of the active transition.
     *  @return The `target` field of activeStateTransition_. */
    SystemState activeTransitionTarget() const;

private:
    /** Single-slot last-writer-wins message buffer for cross-core event delivery.
     *  Written by postEvent() from any core; read and cleared by processMailbox()
     *  on Core 0. targetState is meaningful only when event is STATE_REQUESTED
     *  and was posted via the 3-param overload. */
    struct Mailbox {
        SystemEvent event = static_cast<SystemEvent>(0);
        SystemReason reason = SystemReason::NONE;
        SystemState targetState = SystemState::BOOTING;
        bool pending = false;
    };

    /** Single-slot first-writer-wins error accumulator.
     *  Once set, subsequent errors are silently dropped until
     *  the pending error is drained by processMailbox(). */
    struct ErrorEvent {
        bool pending = false;
        DebugReason reason = nullptr;
        ComponentID source = ComponentID::Count;
    };

    /** Tracks a single component's participation in an orchestration.
     *  transitionId == 0 means "not pending." Zero-initialized timeoutMs
     *  and startedAtMs mean the timeout fires immediately — this is safe
     *  because the struct is only activated by beginOrchestration() which
     *  sets these fields explicitly. */
    struct PendingComponentTransition {
        uint32_t transitionId = 0;
        uint32_t timeoutMs = 0;
        uint32_t startedAtMs = 0;
        bool timeoutHandled = false;
    };

    /** Hooks and state matrix for one component, registered via
     *  setComponentTransitionHooks(). transitionInvoker and timeoutHook
     *  are moved into place. stateMatrix is a non-owning pointer —
     *  must outlive the Supervisor. */
    struct ComponentTransitionHooks {
        TransitionInvoker transitionInvoker;
        TransitionTimeoutHook timeoutHook;
        const ComponentStateMatrix* stateMatrix = nullptr;
        size_t stateMatrixSize = 0;
    };

    /** Tracks an active state transition's identity and endpoints.
     *  Used by requestTransition() / finishTransition() and exposed
     *  via the activeTransition*() accessors. */
    struct StateTransitionInfo {
        uint32_t transitionId = 0;
        SystemState from = SystemState::BOOTING;
        SystemState target = SystemState::BOOTING;
    };

    /** Context for a coordinated multi-component orchestration.
     *  Set by beginOrchestration(), consumed by reportCompletion().
     *  requiredFailure is set when a required component fails, causing
     *  the final state to be ERROR instead of the orchestration target. */
    struct OrchestrationContext {
        bool active = false;
        uint32_t transitionId = 0;
        SystemState target = SystemState::BOOTING;
        SystemEvent trigger = static_cast<SystemEvent>(0);
        SystemReason reason = SystemReason::NONE;
        bool requiredFailure = false;
    };

    /** Core event dispatcher. Gated on FATAL.
     *  - STATE_REQUESTED: reads mailbox_.targetState.
     *    ERROR/FATAL targets → immediate (non-orchestrated) update.
     *    Orchestration in flight → stores as targetMode_ (queued intent).
     *    Otherwise → sets targetMode_ and calls stepTowardTarget().
     *  - COMPONENT_SETUP_FAILED: immediate ERROR (or FATAL if already ERROR).
     *  @param event  The event to handle.
     *  @param reason Origin metadata for telemetry. */
    void handleEvent(SystemEvent event, SystemReason reason);

    /** Immediately set the observed state, notify observers, and log.
     *  This is the ONLY function that writes observedState_.
     *  Side effects:
     *    - ERROR: sets transientError_ = true, targetMode_ = SLEEP.
     *    - SLEEP/CONNECTING/READY: clears transientError_.
     *    - If next == targetMode_: resets targetMode_ to SLEEP.
     *  @param next         The new state.
     *  @param trigger      The event that caused the change.
     *  @param reason       Origin metadata for telemetry.
     *  @param transitionId Associated transition ID (0 if none). */
    void setObservedStateImmediate(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId = 0);

    /** Compute and execute the next intermediate step toward targetMode_.
     *  Uses rank comparison (> / < / ==) to determine direction.
     *  Called when no orchestration is in flight.
     *
     *  Stepping rules:
     *    - ERROR/FATAL: no-op (recovery/exit out of scope).
     *    - At target && LIVE: orchestrates READY (LIVE replay for
     *      re-initialization).
     *    - Moving up (obsRank < 40): immediate CONNECTING, orchestrate READY.
     *    - Moving up (obsRank == 50): orchestrate LIVE.
     *    - Moving down (obsRank == 60): orchestrate READY (transient).
     *    - Moving down (obsRank == 50): orchestrate target directly.
     *  @param event  The trigger event for the step.
     *  @param reason Origin metadata. */
    void stepTowardTarget(SystemEvent event, SystemReason reason);

    /** Check all component transitions for timeout expiry.
     *  Two-phase: (1) collect timed-out indices, (2) handle each.
     *  Handled timeouts call the component's timeoutHook (or fall back
     *  to reportCompletion(Failed) if the hook is null).
     *  Only acts when orchestration_.active is true. */
    void checkTransitionTimeouts();

    /** The confirmed/reported system state. Updated ONLY by setObservedStateImmediate(). */
    SystemState observedState_ = SystemState::BOOTING;
    /** The intended goal state, set by external STATE_REQUESTED events.
     *  Reset to SLEEP when observedState_ reaches targetMode_ or on ERROR. */
    SystemState targetMode_ = SystemState::SLEEP;
    /** Flag set on ERROR entry, cleared on SLEEP/CONNECTING/READY.
     *  Reserved for future error-recovery logic; currently unused by transitions. */
    bool transientError_ = false;
    /** Single-slot cross-core message buffer (last-writer-wins). */
    Mailbox mailbox_{};
    /** Single-slot async error accumulator (first-writer-wins). */
    ErrorEvent errorEvent_{};
    /** Registered state-change observers (heap-allocated, unbounded). */
    std::vector<StateObserver> observers_;
    /** Monotonically increasing transition ID counter. Skips 0 (invalid sentinel).
     *  Wraps from UINT32_MAX to 1 (never 0). */
    uint32_t nextTransitionId_ = 1;
    /** Per-component registration and lifecycle state. Indexed by ComponentID. */
    std::array<ComponentRegistryEntry, static_cast<size_t>(ComponentID::Count)> componentRegistry_{};
    /** Per-component pending transition state. Indexed by ComponentID. */
    std::array<PendingComponentTransition, static_cast<size_t>(ComponentID::Count)> pendingTransitions_{};
    /** Per-component transition hooks. Indexed by ComponentID. */
    std::array<ComponentTransitionHooks, static_cast<size_t>(ComponentID::Count)> componentHooks_{};
    /** Whether a state transition has been claimed via requestTransition(). */
    bool hasActiveStateTransition_ = false;
    /** The currently active state transition identity. */
    StateTransitionInfo activeStateTransition_{};
    /** Context for the currently active orchestration, if any. */
    OrchestrationContext orchestration_{};
};

#endif // PRODUCTION_BUILD
