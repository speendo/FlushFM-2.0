#include "state_machine/supervisor.h"

#include <utility>

#if !defined(ARDUINO)
#include <chrono>
#else
#endif

#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "Supervisor";

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

Supervisor::Supervisor() {
}

// Idempotent boot entry: initiates internal BOOTING flow when observedState_ is BOOTING.
// Sets target to LIVE and uses existing orchestration paths — never calls postEvent(BOOTING).
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

SystemState Supervisor::state() const {
    return observedState_;
}

void Supervisor::subscribe(StateObserver observer) {
    observers_.push_back(observer);
}

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

void Supervisor::setErrorEvent(DebugReason reason, ComponentID source) {
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
}

#if !defined(ARDUINO)
void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.pending = true;
}

void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason, SystemState target) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.targetState = target;
    mailbox_.pending = true;
}

void Supervisor::triggerFatal() {
    setObservedStateImmediate(SystemState::FATAL, static_cast<SystemEvent>(0), SystemReason::NONE);
}

uint32_t Supervisor::getPendingTimeout(ComponentID id) const {
    if (id == ComponentID::Count) return 0;
    return pendingTransitions_[static_cast<size_t>(id)].timeoutMs;
}
#endif

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

ComponentLifecycleStatus Supervisor::getComponentStatus(ComponentID id) const {
    if (id == ComponentID::Count) return ComponentLifecycleStatus::Unknown;
    return componentRegistry_[static_cast<size_t>(id)].lifeCycleStatus;
}

bool Supervisor::isComponentRequired(ComponentID id) const {
    if (id == ComponentID::Count) return false;
    return componentRegistry_[static_cast<size_t>(id)].isRequired;
}

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

bool Supervisor::finishTransition(uint32_t transitionId) {
    if (!hasActiveStateTransition_) return false;
    if (activeStateTransition_.transitionId != transitionId) return false;
    hasActiveStateTransition_ = false;
    return true;
}

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

bool Supervisor::isOrchestrationActive() const {
    return orchestration_.active;
}

size_t Supervisor::componentsWaitingForCompletion() const {
    size_t count = 0;
    for (const auto& p : pendingTransitions_) {
        if (p.transitionId != 0) count++;
    }
    return count;
}

bool Supervisor::hasActiveTransition() const {
    return hasActiveStateTransition_;
}

uint32_t Supervisor::activeTransitionId() const {
    return activeStateTransition_.transitionId;
}

SystemState Supervisor::activeTransitionFrom() const {
    return activeStateTransition_.from;
}

SystemState Supervisor::activeTransitionTarget() const {
    return activeStateTransition_.target;
}
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

// Hierarchy-driven stepper: computes the next state to orchestrate toward targetMode_
// using rank comparison (> / < / ==). Called when no orchestration is in flight.
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
