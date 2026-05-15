#include "supervisor/supervisor.h"

#include <utility>

#if !defined(ARDUINO)
#include <chrono>
#else
#endif

#include "core/debug.h"

namespace {

/** Log source tag for all Supervisor log messages. */
constexpr const char* kLogSource = "Supervisor";

/** Platform-abstracted millisecond timestamp.
 *  Arduino: wraps to millis() directly (49.7 day rollover).
 *  Native:  steady_clock with epoch offset (also wraps at ~49.7 days
 *           due to uint32_t cast, acceptable for test timeouts).
 *  @return Milliseconds since an arbitrary epoch. */
uint32_t nowMs() {
#if defined(ARDUINO)
    return millis();
#else
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point kStart = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - kStart).count();
    return static_cast<uint32_t>(elapsed);
#endif
}

}  // namespace

/** Default constructor.
 *  All state is zero-initialized via default member initializers:
 *    - observedState_ = BOOTING
 *    - targetMode_ = SLEEP
 *    - nextTransitionId_ = 1 (0 is reserved as "invalid")
 *    - All arrays zero-initialized (componentRegistry_, pendingTransitions_, componentHooks_)
 *    - orchestration_.active = false, hasActiveStateTransition_ = false */
Supervisor::Supervisor() {
}

/** @brief Idempotent boot entry.
 *
 *  Contract:
 *    1. Guard: only proceeds if observedState_ == BOOTING.
 *    2. Sets targetMode_ = LIVE.
 *    3. Immediately transitions to CONNECTING (notifies observers).
 *    4. Begins orchestration toward READY (components initialize).
 *    5. The CONNECTING state has zero dwell — orchestration immediately
 *       targets READY. On completion, stepTowardTarget advances to LIVE.
 *
 *  Never calls postEvent(BOOTING). Safe to call multiple times — subsequent
 *  calls are no-ops (observedState_ is no longer BOOTING). */
void Supervisor::setup() {
    if (observedState_ != SystemState::BOOTING) return;
    targetMode_ = SystemState::LIVE;
    setObservedStateImmediate(SystemState::CONNECTING,
                              SystemEvent::STATE_REQUESTED,
                              SystemReason::COMPONENT_SETUP);
    uint32_t tid = nextTransitionId_;
    ++nextTransitionId_;
    if (nextTransitionId_ == 0) nextTransitionId_ = 1;
    (void)beginOrchestration(SystemState::READY,
                             SystemEvent::STATE_REQUESTED,
                             SystemReason::COMPONENT_SETUP,
                             tid);
}

/** @brief Return the confirmed current state.
 *  @return observedState_ (not targetMode_). Guaranteed current as of
 *          the last setObservedStateImmediate() call. */
SystemState Supervisor::state() const {
    return observedState_;
}

/** @brief Register a state-change observer.
 *  @param observer Callable invoked synchronously on every state change.
 *         No unsubscribe mechanism — observers live for the lifetime
 *         of the system. Must not block or allocate during invocation. */
void Supervisor::subscribe(StateObserver observer) {
    observers_.push_back(observer);
}

/** @brief Enqueue or dispatch an event (no target state).
 *
 *  Platform-dependent dispatch:
 *    - **Native:** Synchronously calls handleEvent(). Resets
 *      mailbox_.targetState to BOOTING to prevent stale payload leakage.
 *    - **Arduino:** Writes event/reason to mailbox, sets pending flag.
 *      mailbox_.targetState is NOT modified — can carry stale data from
 *      a previous 3-param call. Prefer the 3-param overload for
 *      STATE_REQUESTED.
 *  @param event  The event type.
 *  @param reason Origin metadata.
 *  @return true. On Arduino, last-writer-wins — second event before
 *          processMailbox() silently overwrites. */
bool Supervisor::postEvent(SystemEvent event, SystemReason reason) {
#if !defined(ARDUINO)
    mailbox_.targetState = SystemState::BOOTING;
    handleEvent(event, reason);
    return true;
#else
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.pending = true;
    return true;
#endif
}

/** @brief Enqueue or dispatch a STATE_REQUESTED event with target state.
 *
 *  Validates that target is not a transient internal stepping state
 *  (BOOTING or CONNECTING). These states are only entered internally
 *  by stepTowardTarget() and setObservedStateImmediate().
 *
 *  Platform dispatch same as 2-param overload, but always sets
 *  mailbox_.targetState so handleEvent() receives the intended target.
 *
 *  @param event  Should be STATE_REQUESTED. Other events ignore target.
 *  @param reason Origin metadata.
 *  @param target The desired system state.
 *  @return true on accept; false if target is BOOTING or CONNECTING. */
bool Supervisor::postEvent(SystemEvent event, SystemReason reason, SystemState target) {
    // Reject external requests for transient internal stepping states
    if (event == SystemEvent::STATE_REQUESTED &&
        (target == SystemState::BOOTING || target == SystemState::CONNECTING)) {
        PROD_LOG(kLogSource, "Rejected STATE_REQUESTED for transient state %s", toString(target));
        return false;
    }
#if !defined(ARDUINO)
    mailbox_.targetState = target;
    handleEvent(event, reason);
    return true;
#else
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.targetState = target;
    mailbox_.pending = true;
    return true;
#endif
}

/** @brief Record an asynchronous error event for processing on the next tick.
 *
 *  First-writer-wins: if errorEvent_ is already pending, subsequent
 *  calls are silently dropped. The first error is the only one the
 *  system responds to until processMailbox() drains it.
 *
 *  @param reason Human-readable failure string. Stored as a raw pointer
 *         — must remain valid until processMailbox() drains it.
 *         Dangling pointer risk if dynamically allocated.
 *  @param source The component reporting the error. */
void Supervisor::setErrorEvent(DebugReason reason, ComponentID source) {
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
}

#if !defined(ARDUINO)
/** @brief Write event to mailbox without dispatching (native/test only).
 *  Mirrors Arduino 2-param postEvent() for testing mailbox semantics.
 *  Does NOT set mailbox_.targetState. */
void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.pending = true;
}

/** @brief Write event+target to mailbox without dispatching (native/test only).
 *  Mirrors Arduino 3-param postEvent() for testing mailbox semantics. */
void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason, SystemState target) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.targetState = target;
    mailbox_.pending = true;
}

/** @brief Immediately enter FATAL state (native/test only).
 *  Bypasses all event routing, orchestration, and mailbox semantics.
 *  Tests the FATAL halt gate in processMailbox() and handleEvent(). */
void Supervisor::triggerFatal() {
    setObservedStateImmediate(SystemState::FATAL, static_cast<SystemEvent>(0), SystemReason::NONE);
}

/** @brief Read a component's pending transition timeout (native/test only).
 *  @param id The component to query.
 *  @return timeoutMs if the component has a pending transition, or 0
 *          for ComponentID::Count or no pending transition. */
uint32_t Supervisor::getPendingTimeout(ComponentID id) const {
    if (id == ComponentID::Count) return 0;
    return pendingTransitions_[static_cast<size_t>(id)].timeoutMs;
}
#endif

/** @brief Drain the mailbox and run transition logic (Core 0 only).
 *
 *  Called periodically from the main loop. Process sequence:
 *    1. **FATAL halt gate** — if state is FATAL, return immediately.
 *    2. **Mailbox drain** — if pending, read event/reason, clear flag,
 *       call handleEvent(). Last-writer-wins: only the most recent event
 *       is processed.
 *    3. **Error drain** — if errorEvent_ is pending, clear it and
 *       immediately transition to ERROR. Uses setObservedStateImmediate
 *       (not handleEvent), so ERROR→FATAL escalation logic is bypassed
 *       for this path.
 *    4. **Timeout check** — calls checkTransitionTimeouts() to handle
 *       any component transitions that exceeded their deadline. */
void Supervisor::processMailbox() {
    if (observedState_ == SystemState::FATAL) return;
    if (mailbox_.pending) {
        SystemEvent event = mailbox_.event;
        SystemReason reason = mailbox_.reason;
        mailbox_.pending = false;
        handleEvent(event, reason);
    }
    if (errorEvent_.pending) {
        errorEvent_.pending = false;
        setObservedStateImmediate(SystemState::ERROR, SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::RECOVERY);
    }
    checkTransitionTimeouts();
}

/** @brief Register a component with the Supervisor.
 *
 *  Sets the component registry entry for the given ID. Resets lifecycle
 *  status to Unknown and clears the disabled flag.
 *
 *  @param id         The component's unique identifier.
 *  @param isRequired If true, failure during orchestration causes the
 *                    entire orchestration to transition to ERROR.
 *  @return false if id == ComponentID::Count.
 *  @note Double-registration silently overwrites the previous entry.
 *        There is no deregistration mechanism. */
bool Supervisor::registerComponent(ComponentID id, bool isRequired) {
    if (id == ComponentID::Count) return false;
    ComponentRegistryEntry& entry = componentRegistry_[static_cast<size_t>(id)];
    entry.isRequired = isRequired;
    entry.isRegistered = true;
    entry.lifeCycleStatus = ComponentLifecycleStatus::Unknown;
    entry.isDisabled = false;
    entry.lastFailureReason = nullptr;
    PROD_LOG(kLogSource, "Registered component %s (required=%s)",
             componentName(id), isRequired ? "true" : "false");
    return true;
}

/** @brief Register transition hooks for a component.
 *
 *  Takes ownership of transitionInvoker and timeoutHook via std::move.
 *  stateMatrix is stored as a non-owning pointer — the caller must
 *  ensure it outlives the Supervisor.
 *
 *  @param id        The component's identifier.
 *  @param transitionInvoker Called by beginOrchestration() to initiate
 *         transition work. Returns timeout in ms (0 = immediate timeout!
 *         Use UINT32_MAX for no timeout).
 *  @param timeoutHook       Called by checkTransitionTimeouts() when
 *         the transition exceeds its timeout. Must not block.
 *  @param stateMatrix       Optional matrix mapping state rank indices
 *         to forward/backward timeouts. Indexed by stateRank/10.
 *  @param stateMatrixSize   Number of entries in stateMatrix.
 *  @return false if id == ComponentID::Count. */
bool Supervisor::setComponentTransitionHooks(ComponentID id,
                                                   TransitionInvoker transitionInvoker,
                                                   TransitionTimeoutHook timeoutHook,
                                                   const ComponentStateMatrix* stateMatrix,
                                                   size_t stateMatrixSize) {
    if (id == ComponentID::Count) return false;
    componentHooks_[static_cast<size_t>(id)] = ComponentTransitionHooks{
        std::move(transitionInvoker),
        std::move(timeoutHook),
        stateMatrix,
        stateMatrixSize};
    return true;
}

/** @brief Query a component's lifecycle status.
 *  @param id The component to query.
 *  @return Lifecycle status, or Unknown for ComponentID::Count. */
ComponentLifecycleStatus Supervisor::getComponentStatus(ComponentID id) const {
    if (id == ComponentID::Count) return ComponentLifecycleStatus::Unknown;
    return componentRegistry_[static_cast<size_t>(id)].lifeCycleStatus;
}

/** @brief Check whether a component is marked as required.
 *  @param id The component to query.
 *  @return true if the component was registered as required. */
bool Supervisor::isComponentRequired(ComponentID id) const {
    if (id == ComponentID::Count) return false;
    return componentRegistry_[static_cast<size_t>(id)].isRequired;
}

/** @brief Mark a component as having an in-flight transition.
 *
 *  Used for components that self-manage transitions outside of
 *  beginOrchestration(). Fails if the component already has a
 *  non-zero transitionId.
 *
 *  @note Does NOT set startedAtMs or timeoutMs — those are only
 *        managed within beginOrchestration(). If this component
 *        participates in a timeout check, the zero timeoutMs will
 *        cause an immediate timeout.
 *  @param id           The component to arm.
 *  @param transitionId Unique transition identifier.
 *  @return false if ComponentID::Count or transition already in-flight. */
bool Supervisor::beginComponentTransition(ComponentID id, uint32_t transitionId) {
    if (id == ComponentID::Count) return false;
    PendingComponentTransition& pending = pendingTransitions_[static_cast<size_t>(id)];
    if (pending.transitionId != 0) {
        DEBUG_LOG(kLogSource, "Rejected transition begin for %s: already has in-flight transition",
                  componentName(id));
        return false;
    }
    pending = PendingComponentTransition{transitionId};
    DEBUG_LOG(kLogSource, "Transition armed for %s with id=%lu",
              componentName(id),
              static_cast<unsigned long>(transitionId));
    return true;
}

/** @brief Report completion or failure of a component transition.
 *
 *  This is the primary callback invoked by components to signal that
 *  their transition work (started by TransitionInvoker) is done.
 *
 *  **Validation:**
 *    - Rejects ComponentID::Count.
 *    - Rejects completions with no in-flight transition (transitionId == 0).
 *    - Rejects stale completions (transitionId mismatch).
 *
 *  **Success path** (TransitionStatus::Completed):
 *    - Sets lifecycle to Ready.
 *    - Clears isDisabled and lastFailureReason.
 *
 *  **Failure path** (TransitionStatus::Failed):
 *    - Sets lifecycle to Failed, marks isDisabled, stores reason.
 *    - If the component is required and belongs to the active orchestration,
 *      marks orchestration_.requiredFailure = true (causes final state to
 *      be ERROR instead of the orchestration target).
 *
 *  **Orchestration finalization:**
 *    When ALL pending components have reported (none remain), and this
 *    completion belongs to the active orchestration:
 *      1. Captures targetMode_ before setObservedStateImmediate may
 *         reset it to SLEEP (savedTarget).
 *      2. Transitions to ERROR (if requiredFailure) or orchestration target.
 *      3. Calls finishTransition(), clears orchestration_.active.
 *      4. If observedState_ != savedTarget: calls stepTowardTarget()
 *         to continue toward the saved user intent.
 *
 *  @param id           The reporting component.
 *  @param transitionId Must match the in-flight transition.
 *  @param status       Completed or Failed.
 *  @param reason       Failure description (stored as raw pointer).
 *  @return false on rejection; true on accepted completion. */
bool Supervisor::reportCompletion(ComponentID id,
                                        uint32_t transitionId,
                                        TransitionStatus status,
                                        DebugReason reason) {
    if (id == ComponentID::Count) return false;

    PendingComponentTransition& pending = pendingTransitions_[static_cast<size_t>(id)];
    if (pending.transitionId == 0) {
        DEBUG_LOG(kLogSource, "Ignoring completion for %s: no in-flight transition",
                  componentName(id));
        return false;
    }

    if (pending.transitionId != transitionId) {
        DEBUG_LOG(kLogSource, "Ignoring stale completion for %s: expected id=%lu got id=%lu",
                  componentName(id),
                  static_cast<unsigned long>(pending.transitionId),
                  static_cast<unsigned long>(transitionId));
        return false;
    }

    ComponentRegistryEntry& entry = componentRegistry_[static_cast<size_t>(id)];
    if (status == TransitionStatus::Completed) {
        entry.lifeCycleStatus = ComponentLifecycleStatus::Ready;
        entry.isDisabled = false;
        entry.lastFailureReason = nullptr;
        PROD_LOG(kLogSource, "Component %s reported completion for transition id=%lu",
                 componentName(id),
                 static_cast<unsigned long>(transitionId));
    } else {
        entry.lifeCycleStatus = ComponentLifecycleStatus::Failed;
        entry.isDisabled = true;
        entry.lastFailureReason = reason;
        if (orchestration_.active && orchestration_.transitionId == transitionId && entry.isRequired) {
            orchestration_.requiredFailure = true;
        }
        ERROR_LOG(kLogSource, "Component %s reported failure for transition id=%lu: %s",
                  componentName(id),
                  static_cast<unsigned long>(transitionId),
                  reason ? reason : "<none>");
    }

    pending = {};

    auto hasPendingTransitions = [this]() -> bool {
        for (const auto& p : pendingTransitions_) {
            if (p.transitionId != 0) return true;
        }
        return false;
    };

    if (orchestration_.active && orchestration_.transitionId == transitionId && !hasPendingTransitions()) {
        // Save user intent before setObservedStateImmediate may reset targetMode_ to SLEEP
        const SystemState savedTarget = targetMode_;

        if (orchestration_.requiredFailure) {
            setObservedStateImmediate(SystemState::ERROR,
                         SystemEvent::COMPONENT_SETUP_FAILED,
                         SystemReason::RECOVERY,
                         transitionId);
        } else {
            setObservedStateImmediate(orchestration_.target,
                         orchestration_.trigger,
                         orchestration_.reason,
                         transitionId);
        }

        (void)finishTransition(transitionId);
        orchestration_.active = false;

        // Continue toward saved intent only if we have not yet arrived
        if (observedState_ != savedTarget) {
            stepTowardTarget(orchestration_.trigger, orchestration_.reason);
        }
    }

    return true;
}

/** @brief Gate for claiming or reversing a state transition.
 *
 *  Rules:
 *    1. Same-state request (from == target) → false. No work to do.
 *    2. No active transition → claim succeeds. Stores {from, target, id}.
 *    3. Active transition exists and new target == active `from` →
 *       **reversal**: overwrites with new {from, target, id}. This allows
 *       a rollback: if the active transition is A→B, a new request C→A
 *       (where A is the original source) supersedes it. Note that `from`
 *       in the reversal is the caller's `from` (should match observedState_),
 *       which may differ from the active transition's original `from`.
 *    4. Otherwise → false. Transition is rejected.
 *
 *  @param from          The source state (should match observedState_).
 *  @param target        The destination state.
 *  @param transitionId  Unique identifier.
 *  @return true if accepted or reversed; false for same-state or rejected. */
bool Supervisor::requestTransition(SystemState from,
                                        SystemState target,
                                        uint32_t transitionId) {
    if (from == target) {
        return false;
    }

    if (!hasActiveStateTransition_) {
        hasActiveStateTransition_ = true;
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        return true;
    }

    if (target == activeStateTransition_.from) {
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        return true;
    }

    return false;
}

/** @brief Mark the active state transition as finished.
 *
 *  Only succeeds if there is an active transition AND the transitionId
 *  matches. Idempotent once cleared — subsequent calls return false.
 *
 *  @param transitionId Must match activeStateTransition_.transitionId.
 *  @return true if the transition was cleared; false if none active or
 *          ID mismatch. */
bool Supervisor::finishTransition(uint32_t transitionId) {
    if (!hasActiveStateTransition_) return false;
    if (activeStateTransition_.transitionId != transitionId) return false;
    hasActiveStateTransition_ = false;
    return true;
}

/** @brief Begin a coordinated multi-component orchestration toward a target state.
 *
 *  This is the core orchestration launcher, called by stepTowardTarget() and
 *  setup(). The sequence is:
 *
 *  1. **Transition gate:** Calls requestTransition(observedState_, target).
 *     If rejected (transition in flight, reversal not possible), returns false.
 *  2. **Set context:** Stores target, trigger, reason, clears requiredFailure.
 *  3. **Mark pending:** Clears ALL pendingTransitions_ arrays, then marks every
 *     registered, non-disabled component as pending for this transitionId.
 *  4. **Zero-registered fast path:** If zero components are registered,
 *     immediately transitions to target (setObservedStateImmediate), finishes
 *     the transition, clears orchestration, then steps toward savedTarget.
 *  5. **Invoke components:** For each pending component with a TransitionInvoker:
 *     - Sets startedAtMs and clears timeoutHandled.
 *     - Calls invoker (for side effects), captures invokerTimeout.
 *     - **Timeout resolution:**
 *       * If state matrix exists AND has an entry for stateRank/10:
 *         use forwardTimeoutMs / backwardTimeoutMs (based on direction).
 *       * Otherwise: use invokerTimeout.
 *       * Note: a resolved timeout of 0 means "immediate timeout." Components
 *         that do not want timeouts should return UINT32_MAX from their invoker.
 *
 *  @param target        The state to orchestrate toward.
 *  @param trigger       The event that triggered this orchestration.
 *  @param reason        Origin metadata for telemetry.
 *  @param transitionId  Unique identifier (must not be 0).
 *  @return false if requestTransition() rejected the request. */
bool Supervisor::beginOrchestration(SystemState target,
                                          SystemEvent trigger,
                                          SystemReason reason,
                                          uint32_t transitionId) {
    if (!requestTransition(observedState_, target, transitionId)) {
        return false;
    }

    orchestration_.active = true;
    orchestration_.transitionId = transitionId;
    orchestration_.target = target;
    orchestration_.trigger = trigger;
    orchestration_.reason = reason;
    orchestration_.requiredFailure = false;

    for (auto& p : pendingTransitions_) p = {};
    size_t registeredCount = 0;
    for (size_t i = 0; i < static_cast<size_t>(ComponentID::Count); i++) {
        const ComponentRegistryEntry& entry = componentRegistry_[i];
        if (!entry.isRegistered || entry.isDisabled) {
            continue;
        }
        pendingTransitions_[i].transitionId = transitionId;
        registeredCount++;
    }

    if (registeredCount == 0) {
        // Save user intent before setObservedStateImmediate may reset targetMode_ to SLEEP
        const SystemState savedTarget = targetMode_;

        setObservedStateImmediate(target, trigger, reason, transitionId);
        (void)finishTransition(transitionId);
        orchestration_.active = false;

        // Continue toward saved intent only if we have not yet arrived
        if (observedState_ != savedTarget) {
            stepTowardTarget(orchestration_.trigger, orchestration_.reason);
        }

        return true;
    }

    for (size_t i = 0; i < static_cast<size_t>(ComponentID::Count); i++) {
        PendingComponentTransition& pending = pendingTransitions_[i];
        if (pending.transitionId != transitionId) {
            continue;
        }

        const ComponentTransitionHooks& hooks = componentHooks_[i];
        if (!hooks.transitionInvoker) {
            DEBUG_LOG(kLogSource, "No transition hooks registered for component %s",
                      componentName(static_cast<ComponentID>(i)));
            continue;
        }

        pending.startedAtMs = nowMs();
        pending.timeoutHandled = false;

        // Always call invoker for side effects (component setup/shutdown)
        uint32_t invokerTimeout = hooks.transitionInvoker(target, transitionId);

        // Use matrix timeout if available (replaces invoker timeout)
        if (hooks.stateMatrix && hooks.stateMatrixSize > 0) {
            size_t idx = stateRank(target) / 10;
            if (idx < hooks.stateMatrixSize) {
                bool isForward = stateRank(target) > stateRank(observedState_);
                pending.timeoutMs = isForward
                    ? hooks.stateMatrix[idx].forwardTimeoutMs
                    : hooks.stateMatrix[idx].backwardTimeoutMs;
            } else {
                pending.timeoutMs = invokerTimeout;
            }
        } else {
            pending.timeoutMs = invokerTimeout;
        }
    }

    return true;
}

/** @brief Check whether an orchestration is currently in flight.
 *  @return true if beginOrchestration() has been called and the
 *          orchestration has not yet completed. */
bool Supervisor::isOrchestrationActive() const {
    return orchestration_.active;
}

/** @brief Count components still waiting to report completion.
 *  @return Number of pending transitions with non-zero transitionId.
 *          O(n) scan where n = ComponentID::Count (currently 4). */
size_t Supervisor::componentsWaitingForCompletion() const {
    size_t count = 0;
    for (const auto& p : pendingTransitions_) {
        if (p.transitionId != 0) count++;
    }
    return count;
}

/** @brief Check whether a state transition has been claimed.
 *  @return true if requestTransition() succeeded and finishTransition()
 *          has not yet been called. */
bool Supervisor::hasActiveTransition() const {
    return hasActiveStateTransition_;
}

/** @brief Get the active state transition's ID.
 *  @return transitionId from activeStateTransition_.
 *          Meaningful only when hasActiveTransition() is true. */
uint32_t Supervisor::activeTransitionId() const {
    return activeStateTransition_.transitionId;
}

/** @brief Get the source state of the active transition.
 *  @return The `from` field of activeStateTransition_. */
SystemState Supervisor::activeTransitionFrom() const {
    return activeStateTransition_.from;
}

/** @brief Get the target state of the active transition.
 *  @return The `target` field of activeStateTransition_. */
SystemState Supervisor::activeTransitionTarget() const {
    return activeStateTransition_.target;
}

/** @brief Check all component transitions for timeouts.
 *
 *  Only acts when orchestration_.active is true.
 *  Two-phase algorithm to avoid iterator invalidation:
 *    Phase 1: Collect indices of components whose elapsed time
 *             (currentMs - startedAtMs) >= timeoutMs.
 *    Phase 2: For each collected index, re-validate (transitionId
 *             match and not yet handled), then:
 *             - If timeoutHook exists: call it.
 *             - Otherwise: fall back to reportCompletion(Failed).
 *
 *  @note There is a TOCTOU gap between phases — re-validation guards
 *        mitigate but cannot fully close it. The vector allocation for
 *        timedOutIndices occurs on every call (heap). */
void Supervisor::checkTransitionTimeouts() {
    if (!orchestration_.active) {
        return;
    }
    
    std::vector<size_t> timedOutIndices;
    const uint32_t currentMs = nowMs();
    
    for (size_t i = 0; i < static_cast<size_t>(ComponentID::Count); i++) {
        const PendingComponentTransition& pending = pendingTransitions_[i];
        if (pending.transitionId != orchestration_.transitionId) {
            continue;
        }
        
        if (pending.timeoutHandled) {
            continue;
        }
        
        const bool timeoutReached = (currentMs - pending.startedAtMs >= pending.timeoutMs);
        if (timeoutReached) {
            timedOutIndices.push_back(i);
        }
    }
    
    for (const size_t i : timedOutIndices) {
        PendingComponentTransition& pending = pendingTransitions_[i];
        if (pending.transitionId != orchestration_.transitionId || pending.timeoutHandled) {
            continue;
        }
        
        pending.timeoutHandled = true;
        
        const ComponentTransitionHooks& hooks = componentHooks_[i];
        if (!hooks.transitionInvoker) {
            (void)reportCompletion(static_cast<ComponentID>(i),
                                   orchestration_.transitionId,
                                   TransitionStatus::Failed,
                                   "timeout hook missing");
            continue;
        }
        
        ERROR_LOG(kLogSource, "Transition id=%lu timed out for component %s",
                  static_cast<unsigned long>(orchestration_.transitionId),
                  componentName(static_cast<ComponentID>(i)));
        hooks.timeoutHook(orchestration_.transitionId);
    }
}

/** @brief Rank-based stepper toward targetMode_.
 *
 *  Computes and executes the next intermediate orchestration step toward
 *  targetMode_. Called when no orchestration is in flight (i.e., after
 *  a STATE_REQUESTED is accepted or after orchestration completes).
 *
 *  **Guards:**
 *    - ERROR/FATAL: no-op. Recovery and exit are out of scope.
 *
 *  **Stepping rules:**
 *    - At target (targetRank == obsRank) && LIVE:
 *      Orchestrates READY ("LIVE replay") so completion triggers a step
 *      back to LIVE, re-initializing components.
 *    - At target && not LIVE: no-op (already there).
 *    - Moving up (targetRank > obsRank):
 *      * obsRank ≤ 30 (BOOTING): immediate CONNECTING (zero-dwell pass-through),
 *        then orchestrate READY.
 *      * obsRank == 50 (READY): orchestrate LIVE.
 *    - Moving down (targetRank < obsRank):
 *      * obsRank == 60 (LIVE): orchestrate READY (transient step-down).
 *      * obsRank == 50 (READY): orchestrate targetMode_ directly (SLEEP).
 *
 *  @param event  The trigger event to propagate to beginOrchestration().
 *  @param reason Origin metadata for telemetry. */
void Supervisor::stepTowardTarget(SystemEvent event, SystemReason reason) {
    // Do not step from ERROR/FATAL — recovery/exit is out of scope
    if (observedState_ == SystemState::ERROR || observedState_ == SystemState::FATAL) return;

    const uint8_t targetRank = stateRank(targetMode_);
    const uint8_t obsRank = stateRank(observedState_);

    if (targetRank == obsRank) {
        // LIVE replay: step to READY so auto-continuation brings us back to LIVE
        if (targetRank == 60) {
            uint32_t tid = nextTransitionId_;
            ++nextTransitionId_;
            if (nextTransitionId_ == 0) nextTransitionId_ = 1;
            (void)beginOrchestration(SystemState::READY, event, reason, tid);
        }
        return;
    }

    auto request = [this, event, reason](SystemState target) {
        uint32_t tid = nextTransitionId_;
        ++nextTransitionId_;
        if (nextTransitionId_ == 0) nextTransitionId_ = 1;
        (void)beginOrchestration(target, event, reason, tid);
    };

    if (targetRank > obsRank) {
        // Moving up: step through the L2 upward sequence
        if (obsRank <= 30) {
            setObservedStateImmediate(SystemState::CONNECTING, event, reason);
            request(SystemState::READY);
        } else {
            // obsRank == 50 (READY): step to LIVE
            request(SystemState::LIVE);
        }
    } else {
        // Moving down: obsRank > targetRank
        if (obsRank == 60) {
            // LIVE: step down through READY first
            request(SystemState::READY);
        } else {
            // READY to SLEEP: direct orchestration
            request(targetMode_);
        }
    }
}

/** @brief Core event dispatcher.
 *
 *  Called from postEvent() (native path) or processMailbox() (Arduino path).
 *  Gates on FATAL — if observed state is FATAL, all events are silently dropped.
 *
 *  **STATE_REQUESTED handling:**
 *    Reads the target from mailbox_.targetState (set by 3-param postEvent).
 *    - ERROR target: immediate non-orchestrated transition. If already ERROR,
 *      escalates to FATAL.
 *    - FATAL target: immediate transition. Sets targetMode_ = FATAL to persist
 *      the halt across all future events.
 *    - Orchestration in flight: stores target as targetMode_ (queued intent)
 *      but does NOT step. The step occurs when the current orchestration
 *      completes (in reportCompletion).
 *    - Otherwise: sets targetMode_ and calls stepTowardTarget().
 *
 *  **COMPONENT_SETUP_FAILED handling:**
 *    Immediate transition to ERROR. If already ERROR, escalates to FATAL.
 *    Note: this path is reachable via direct postEvent(COMPONENT_SETUP_FAILED).
 *    The error-event path in processMailbox() uses setObservedStateImmediate
 *    directly, bypassing this escalation logic.
 *
 *  @param event  The event to handle.
 *  @param reason Origin metadata for telemetry. */
void Supervisor::handleEvent(SystemEvent event, SystemReason reason) {
    if (observedState_ == SystemState::FATAL) {
        return;
    }

    if (event == SystemEvent::STATE_REQUESTED) {
        const SystemState target = mailbox_.targetState;

        // ERROR: immediate non-orchestrated state update
        if (target == SystemState::ERROR) {
            setObservedStateImmediate(observedState_ == SystemState::ERROR
                                          ? SystemState::FATAL : SystemState::ERROR,
                                      event, reason);
            return;
        }

        // FATAL: immediate non-orchestrated state update, halt all processing
        if (target == SystemState::FATAL) {
            targetMode_ = SystemState::FATAL;
            setObservedStateImmediate(SystemState::FATAL, event, reason);
            return;
        }

        // Orchestration in flight: store user intent, do not overwrite Mailbox targetState
        if (orchestration_.active) {
            targetMode_ = target;
            return;
        }

        // Set intent and step toward it (fixes READY bug: targetMode_ is now correct)
        targetMode_ = target;
        stepTowardTarget(event, reason);
        return;
    }

    if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
        setObservedStateImmediate(observedState_ == SystemState::ERROR ? SystemState::FATAL : SystemState::ERROR, event, reason);
        return;
    }
}

/** @brief Immediately set the observed state, notify observers, and log.
 *
 *  This is the ONLY function that writes observedState_. Every state change
 *  in the system flows through this method.
 *
 *  **Side effects:**
 *    - No-op if next == observedState_ (idempotent guard).
 *    - ERROR:
 *      * Sets transientError_ = true.
 *      * Resets targetMode_ to SLEEP (abandons previous user intent).
 *    - SLEEP / CONNECTING / READY:
 *      * Clears transientError_ (error-recovery signal).
 *    - Reaching target (next == targetMode_):
 *      * Resets targetMode_ to SLEEP. This means that when a user-requested
 *        target is reached, the system idles at SLEEP until a new request.
 *        The caller (reportCompletion / stepTowardTarget) is responsible
 *        for continuing toward savedTarget if further steps are needed.
 *
 *  **Notification:** Calls every registered StateObserver with the new state.
 *  Observers are invoked synchronously and must not block.
 *
 *  @param next         The new state (must differ from observedState_).
 *  @param trigger      The event that caused this change (for logging).
 *  @param reason       Origin metadata (for logging).
 *  @param transitionId Associated transition ID (0 if none; for logging). */
void Supervisor::setObservedStateImmediate(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId) {
    if (next == observedState_) {
        return;
    }

    const SystemState previous = observedState_;
    observedState_ = next;

    if (next == SystemState::ERROR) {
        transientError_ = true;
        targetMode_ = SystemState::SLEEP;
    }
    if (next == SystemState::SLEEP || next == SystemState::CONNECTING || next == SystemState::READY) {
        transientError_ = false;
    }

    PROD_LOG(kLogSource, "State transition id=%lu: %s -> %s (event=%s reason=%s)",
             static_cast<unsigned long>(transitionId),
             toString(previous),
             toString(next),
             toString(trigger),
             toString(reason));

    for (const auto& observer : observers_) {
        observer(next);
    }

    if (next == targetMode_) {
        targetMode_ = SystemState::SLEEP;
    }
}
