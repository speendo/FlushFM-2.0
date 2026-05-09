#include "state_machine/supervisor.h"

#include <utility>

#if !defined(ARDUINO)
#include <chrono>
#else
#include <esp_system.h>
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

bool isIntentEvent(SystemEvent event) {
    return event == SystemEvent::PLAY_REQUESTED ||
           event == SystemEvent::STOP_REQUESTED ||
           event == SystemEvent::ENTER_SLEEP;
}

}  // namespace

Supervisor::Supervisor() {
#if defined(ARDUINO)
    queue_ = xQueueCreate(16, sizeof(QueuedEvent));
    if (!queue_) {
        ERROR_LOG(kLogSource, "Critical: event queue allocation failed; state-machine event processing is unavailable");
    }
#else
    // Native/host builds do not use a FreeRTOS queue; postEvent() dispatches directly.
    queue_ = nullptr;
#endif
}

SystemState Supervisor::state() const {
    return state_;
}

void Supervisor::subscribe(StateObserver observer) {
    observers_.push_back(observer);
}

bool Supervisor::postEvent(SystemEvent event, SystemReason reason, EventPolicy policy) {
#if !defined(ARDUINO)
    (void)policy;
    handleEvent(event, reason);
    return true;
#else
    if (!queue_) {
        ERROR_LOG(kLogSource, "Critical: event queue missing in postEvent(); restarting");
        delay(50);
        esp_restart();
        return false;
    }
    const QueuedEvent queued{event, reason};
    
    const TickType_t timeout = (policy == EventPolicy::Critical) ? pdMS_TO_TICKS(10) : 0;
    const bool success = xQueueSend(queue_, &queued, timeout) == pdTRUE;
    
    if (!success && policy == EventPolicy::Critical) {
        // Critical event lost: set sticky flag and log for recovery in processEventQueue().
        ERROR_LOG(kLogSource, "Event queue full; critical event %s will retry on next dispatch", toString(event));
        pendingCriticalEvent_ = true;
        pendingEvent_ = event;
        pendingReason_ = reason;
    }
    
    return success;
#endif
}

void Supervisor::processEventQueue() {
#if !defined(ARDUINO)
    const bool transitionBusy = orchestration_.active || hasActiveStateTransition_ || state_ == SystemState::BOOTING || state_ == SystemState::CONNECTING;
    if (!transitionBusy && !deferredIntentEvents_.empty()) {
        const QueuedEvent deferred = deferredIntentEvents_.front();
        deferredIntentEvents_.erase(deferredIntentEvents_.begin());
        handleEvent(deferred.event, deferred.reason);
    }

    checkTransitionTimeouts();
    return;
#else
    if (!queue_) {
        ERROR_LOG(kLogSource, "Critical: event queue missing in processEventQueue(); restarting");
        delay(50);
        esp_restart();
        return;
    }

    auto isTransitionBusy = [this]() {
        return orchestration_.active || hasActiveStateTransition_ || state_ == SystemState::BOOTING || state_ == SystemState::CONNECTING;
    };

    // Process any pending critical event first (sticky fallback from queue-full condition).
    if (pendingCriticalEvent_) {
        if (isIntentEvent(pendingEvent_) && isTransitionBusy()) {
            deferredIntentEvents_.push_back(QueuedEvent{pendingEvent_, pendingReason_});
        } else {
            PROD_LOG(kLogSource, "Processing pending critical event: %s", toString(pendingEvent_));
            handleEvent(pendingEvent_, pendingReason_);
        }
        pendingCriticalEvent_ = false;
    }

    // Drain normal queue.
    QueuedEvent queued{};
    while (xQueueReceive(queue_, &queued, 0) == pdTRUE) {
        if (isIntentEvent(queued.event) && isTransitionBusy()) {
            deferredIntentEvents_.push_back(queued);
            continue;
        }

        handleEvent(queued.event, queued.reason);
    }

    if (!isTransitionBusy() && !deferredIntentEvents_.empty()) {
        const QueuedEvent deferred = deferredIntentEvents_.front();
        deferredIntentEvents_.erase(deferredIntentEvents_.begin());
        handleEvent(deferred.event, deferred.reason);
    }

    checkTransitionTimeouts();
#endif
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
                                                   TransitionTimeoutHook timeoutHook) {
    if (id == ComponentID::Count) return false;
    componentHooks_[static_cast<size_t>(id)] = ComponentTransitionHooks{std::move(transitionInvoker), std::move(timeoutHook)};
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
        if (orchestration_.requiredFailure) {
            transitionTo(SystemState::ERROR,
                         SystemEvent::COMPONENT_SETUP_FAILED,
                         SystemReason::RECOVERY,
                         transitionId);
        } else {
            transitionTo(orchestration_.target,
                         orchestration_.trigger,
                         orchestration_.reason,
                         transitionId);
        }

        (void)finishTransition(transitionId);
        orchestration_.active = false;

        if (state_ == SystemState::READY && targetState_ == SystemState::LIVE) {
            handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
        }
        if (deferredReplayEvent_) {
            deferredReplayEvent_ = false;
            handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
        }
    }

    return true;
}

TransitionRequestDecision Supervisor::requestTransition(SystemState from,
                                                              SystemState target,
                                                              uint32_t transitionId) {
    if (from == target) {
        return TransitionRequestDecision::Ignored;
    }

    if (!hasActiveStateTransition_) {
        hasActiveStateTransition_ = true;
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        return TransitionRequestDecision::Started;
    }

    if (target == activeStateTransition_.from) {
        // Reciprocal transition supersedes the currently active transition.
        const StateTransitionInfo previous = activeStateTransition_;
        (void)previous;
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        hasQueuedStateTransition_ = false;
        return TransitionRequestDecision::Superseded;
    }

    queuedStateTransition_ = StateTransitionInfo{transitionId, from, target};
    hasQueuedStateTransition_ = true;
    return TransitionRequestDecision::Queued;
}

bool Supervisor::finishTransition(uint32_t transitionId) {
    if (!hasActiveStateTransition_) {
        return false;
    }

    if (activeStateTransition_.transitionId != transitionId) {
        return false;
    }

    if (hasQueuedStateTransition_) {
        activeStateTransition_ = queuedStateTransition_;
        hasQueuedStateTransition_ = false;
        if (activeStateTransition_.target == state_) {
            hasActiveStateTransition_ = false;
        }
        return true;
    }

    hasActiveStateTransition_ = false;
    return true;
}

bool Supervisor::beginOrchestration(SystemState target,
                                          SystemEvent trigger,
                                          SystemReason reason,
                                          uint32_t transitionId) {
    const TransitionRequestDecision decision = requestTransition(state_, target, transitionId);
    if (decision == TransitionRequestDecision::Ignored || decision == TransitionRequestDecision::Queued) {
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
        transitionTo(target, trigger, reason, transitionId);
        (void)finishTransition(transitionId);
        orchestration_.active = false;

        if (state_ == SystemState::READY && targetState_ == SystemState::LIVE) {
            handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
        }
        if (deferredReplayEvent_) {
            deferredReplayEvent_ = false;
            handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
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
        pending.timeoutMs = hooks.transitionInvoker(target, transitionId);
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

bool Supervisor::hasQueuedTransition() const {
    return hasQueuedStateTransition_;
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

uint32_t Supervisor::queuedTransitionId() const {
    return queuedStateTransition_.transitionId;
}

SystemState Supervisor::queuedTransitionFrom() const {
    return queuedStateTransition_.from;
}

SystemState Supervisor::queuedTransitionTarget() const {
    return queuedStateTransition_.target;
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

void Supervisor::handleEvent(SystemEvent event, SystemReason reason) {
    auto requestStateTransition = [this, event, reason](SystemState target) {
        uint32_t transitionId = nextTransitionId_;
        ++nextTransitionId_;
        if (nextTransitionId_ == 0) {
            nextTransitionId_ = 1;
        }

        const bool started = beginOrchestration(target, event, reason, transitionId);
        DEBUG_LOG(kLogSource, "Transition request id=%lu %s -> %s started=%s",
                  static_cast<unsigned long>(transitionId),
                  toString(state_),
                  toString(target),
                  started ? "true" : "false");
    };

    // User intents are state-independent and map directly to target states.
    if (event == SystemEvent::ENTER_SLEEP) {
        pendingReplayRequested_ = false;
        targetState_ = SystemState::SLEEP;
        requestStateTransition(SystemState::SLEEP);
        return;
    }
    if (event == SystemEvent::STOP_REQUESTED) {
        pendingReplayRequested_ = false;
        targetState_ = SystemState::SLEEP;
        if (state_ == SystemState::CONNECTING) {
            return;
        }
        requestStateTransition(SystemState::READY);
        return;
    }
    if (event == SystemEvent::PLAY_REQUESTED) {
        if (state_ == SystemState::CONNECTING) {
            targetState_ = SystemState::LIVE;
            return;
        }

        if (state_ == SystemState::SLEEP) {
            targetState_ = SystemState::LIVE;
            transitionTo(SystemState::CONNECTING, event, reason);
            if (startupWiFiReady_ && startupAudioReady_) {
                requestStateTransition(SystemState::READY);
            }
            return;
        }

        if (state_ == SystemState::LIVE) {
            // Keep UX responsive: replay/switch while stopping is deferred until READY is reached.
            pendingReplayRequested_ = true;
            requestStateTransition(SystemState::READY);
            return;
        }

        requestStateTransition(SystemState::LIVE);
        return;
    }

    switch (state_) {
        case SystemState::BOOTING:
            if (event == SystemEvent::BOOT) {
                startupWiFiReady_ = false;
                startupAudioReady_ = false;
                transitionTo(SystemState::SLEEP, event, reason);
            }
            break;

        case SystemState::SLEEP:
            // Keep readiness flags up to date while idle so PLAY can immediately continue.
            if (event == SystemEvent::WIFI_READY) {
                startupWiFiReady_ = true;
            } else if (event == SystemEvent::AUDIO_INIT_OK) {
                startupAudioReady_ = true;
            } else if (event == SystemEvent::WIFI_DISCONNECTED) {
                startupWiFiReady_ = false;
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                startupAudioReady_ = false;
            }
            break;

        case SystemState::CONNECTING:
            if (event == SystemEvent::AUDIO_INIT_OK) {
                startupAudioReady_ = true;
                if (startupWiFiReady_) {
                    requestStateTransition(SystemState::READY);
                }
            } else if (event == SystemEvent::WIFI_READY) {
                startupWiFiReady_ = true;
                if (startupAudioReady_) {
                    requestStateTransition(SystemState::READY);
                }
            } else if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            }
            break;

        case SystemState::READY:
            if (event == SystemEvent::WIFI_DISCONNECTED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            }
            break;

        case SystemState::LIVE:
            if (event == SystemEvent::WIFI_DISCONNECTED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::STREAM_LOST) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            }
            break;

        case SystemState::ERROR:
            if (event == SystemEvent::RECOVER) {
                transitionTo(SystemState::READY, event, reason);
            }
            break;
    }
}

void Supervisor::transitionTo(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId) {
    if (next == state_) {
        return;
    }

    const SystemState previous = state_;
    state_ = next;

    if (next == SystemState::ERROR) {
        transientError_ = true;
        targetState_ = SystemState::SLEEP;
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

    if (previous == SystemState::LIVE && next == SystemState::READY && pendingReplayRequested_) {
        pendingReplayRequested_ = false;
        deferredReplayEvent_ = true;
    }

    if (next == targetState_) {
        targetState_ = SystemState::SLEEP;
    }
}
