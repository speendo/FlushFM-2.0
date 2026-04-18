#include "state_machine/system_controller.h"

#include <chrono>
#include <cstring>
#include <utility>

#include "core/debug.h"

namespace {

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
           event == SystemEvent::ENTER_OFF;
}

}  // namespace

SystemController::SystemController() {
#if defined(ARDUINO)
    queue_ = xQueueCreate(16, sizeof(QueuedEvent));
#else
    queue_ = nullptr;
#endif
}

SystemState SystemController::state() const {
    return state_;
}

void SystemController::subscribe(StateObserver observer) {
    observers_.push_back(observer);
}

bool SystemController::postEvent(SystemEvent event, SystemReason reason, EventPolicy policy) {
#if !defined(ARDUINO)
    (void)policy;
    handleEvent(event, reason);
    return true;
#else
    if (!queue_) {
        return false;
    }
    const QueuedEvent queued{event, reason};
    
    const TickType_t timeout = (policy == EventPolicy::BOUNDED_BLOCKING) ? pdMS_TO_TICKS(10) : 0;
    const bool success = xQueueSend(queue_, &queued, timeout) == pdTRUE;
    
    if (!success && policy == EventPolicy::BOUNDED_BLOCKING) {
        // Critical event lost: set sticky flag and log for recovery in dispatchPending().
        ERROR_LOG("SystemController", "Event queue full; critical event %s will retry on next dispatch", toString(event));
        pendingCriticalEvent_ = true;
        pendingEvent_ = event;
        pendingReason_ = reason;
    }
    
    return success;
#endif
}

void SystemController::dispatchPending() {
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
            PROD_LOG("SystemController", "Processing pending critical event: %s", toString(pendingEvent_));
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

std::string SystemController::normalizeComponentName(const char* name) {
    if (!name || name[0] == '\0') {
        return {};
    }

    std::string normalized{name};
    if (normalized.size() > kMaxComponentNameLen) {
        ERROR_LOG("SystemController", "Component name truncated to %lu chars", static_cast<unsigned long>(kMaxComponentNameLen));
        normalized.resize(kMaxComponentNameLen);
    }
    return normalized;
}

void SystemController::copyFailureReason(char* destination, size_t destinationSize, const char* reason) {
    if (!destination || destinationSize == 0) {
        return;
    }

    destination[0] = '\0';
    if (!reason) {
        return;
    }

    const size_t reasonLength = std::strlen(reason);
    if (reasonLength >= destinationSize) {
        ERROR_LOG("SystemController", "Failure reason truncated to %lu chars", static_cast<unsigned long>(destinationSize - 1));
    }

    std::strncpy(destination, reason, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

bool SystemController::registerComponent(const char* name, bool isRequired) {
    const std::string normalizedName = normalizeComponentName(name);
    if (normalizedName.empty()) {
        ERROR_LOG("SystemController", "Rejected component registration with empty or null name");
        return false;
    }

    auto [it, inserted] = componentRegistry_.emplace(normalizedName, ComponentRegistryEntry{});
    it->second.isRequired = isRequired;
    if (inserted) {
        it->second.lifeCycleStatus = ComponentLifecycleStatus::Unknown;
        it->second.isDisabled = false;
        copyFailureReason(it->second.lastFailureReason, sizeof(it->second.lastFailureReason), nullptr);
    }

    PROD_LOG("SystemController", "Registered component %s (required=%s)",
             normalizedName.c_str(), isRequired ? "true" : "false");
    return true;
}

bool SystemController::setComponentTransitionHooks(const char* name,
                                                   TransitionInvoker transitionInvoker,
                                                   TransitionTimeoutHook timeoutHook) {
    const std::string normalizedName = normalizeComponentName(name);
    if (normalizedName.empty()) {
        ERROR_LOG("SystemController", "Rejected transition hooks for empty or null component name");
        return false;
    }

    if (!transitionInvoker || !timeoutHook) {
        ERROR_LOG("SystemController", "Rejected transition hooks for %s due to missing callbacks", normalizedName.c_str());
        return false;
    }

    if (componentRegistry_.find(normalizedName) == componentRegistry_.end()) {
        ERROR_LOG("SystemController", "Rejected transition hooks for unknown component %s", normalizedName.c_str());
        return false;
    }

    componentHooks_[normalizedName] = ComponentTransitionHooks{std::move(transitionInvoker), std::move(timeoutHook)};
    return true;
}

ComponentLifecycleStatus SystemController::getComponentStatus(const char* name) const {
    const std::string normalizedName = normalizeComponentName(name);
    if (normalizedName.empty()) {
        return ComponentLifecycleStatus::Unknown;
    }

    const auto it = componentRegistry_.find(normalizedName);
    if (it == componentRegistry_.end()) {
        return ComponentLifecycleStatus::Unknown;
    }

    return it->second.lifeCycleStatus;
}

bool SystemController::markComponentFailed(const char* name, const char* reason) {
    const std::string normalizedName = normalizeComponentName(name);
    if (normalizedName.empty()) {
        ERROR_LOG("SystemController", "Rejected component failure for empty or null name");
        return false;
    }

    auto [it, inserted] = componentRegistry_.emplace(normalizedName, ComponentRegistryEntry{});
    (void)inserted;
    it->second.lifeCycleStatus = ComponentLifecycleStatus::Failed;
    it->second.isDisabled = true;
    copyFailureReason(it->second.lastFailureReason, sizeof(it->second.lastFailureReason), reason);

    ERROR_LOG("SystemController", "Component %s marked failed: %s",
              normalizedName.c_str(), reason ? reason : "<none>");
    return true;
}

bool SystemController::isComponentRequired(const char* name) const {
    const std::string normalizedName = normalizeComponentName(name);
    if (normalizedName.empty()) {
        return false;
    }

    const auto it = componentRegistry_.find(normalizedName);
    if (it == componentRegistry_.end()) {
        return false;
    }

    return it->second.isRequired;
}

bool SystemController::beginComponentTransition(const char* name, uint32_t transitionId) {
    const std::string normalizedName = normalizeComponentName(name);
    if (normalizedName.empty()) {
        ERROR_LOG("SystemController", "Rejected transition begin for empty or null component name");
        return false;
    }

    const auto registryIt = componentRegistry_.find(normalizedName);
    if (registryIt == componentRegistry_.end()) {
        ERROR_LOG("SystemController", "Rejected transition begin for unknown component %s", normalizedName.c_str());
        return false;
    }

    if (pendingTransitions_.find(normalizedName) != pendingTransitions_.end()) {
        DEBUG_LOG("SystemController", "Rejected transition begin for %s: component already has in-flight transition", normalizedName.c_str());
        return false;
    }

    pendingTransitions_.emplace(normalizedName, PendingComponentTransition{transitionId});
    DEBUG_LOG("SystemController", "Transition armed for %s with id=%lu",
              normalizedName.c_str(),
              static_cast<unsigned long>(transitionId));
    return true;
}

bool SystemController::reportCompletion(const char* componentName,
                                        uint32_t transitionId,
                                        TransitionStatus status,
                                        DebugReason reason) {
    const std::string normalizedName = normalizeComponentName(componentName);
    if (normalizedName.empty()) {
        ERROR_LOG("SystemController", "Rejected completion report for empty or null component name");
        return false;
    }

    auto pendingIt = pendingTransitions_.find(normalizedName);
    if (pendingIt == pendingTransitions_.end()) {
        DEBUG_LOG("SystemController", "Ignoring completion for %s: no in-flight transition", normalizedName.c_str());
        return false;
    }

    if (pendingIt->second.transitionId != transitionId) {
        DEBUG_LOG("SystemController", "Ignoring stale completion for %s: expected id=%lu got id=%lu",
                  normalizedName.c_str(),
                  static_cast<unsigned long>(pendingIt->second.transitionId),
                  static_cast<unsigned long>(transitionId));
        return false;
    }

    auto registryIt = componentRegistry_.find(normalizedName);
    if (registryIt == componentRegistry_.end()) {
        ERROR_LOG("SystemController", "Ignoring completion for unknown component %s", normalizedName.c_str());
        pendingTransitions_.erase(pendingIt);
        return false;
    }

    if (status == TransitionStatus::Completed) {
        registryIt->second.lifeCycleStatus = ComponentLifecycleStatus::Ready;
        registryIt->second.isDisabled = false;
        copyFailureReason(registryIt->second.lastFailureReason, sizeof(registryIt->second.lastFailureReason), nullptr);
        PROD_LOG("SystemController", "Component %s reported completion for transition id=%lu",
                 normalizedName.c_str(),
                 static_cast<unsigned long>(transitionId));
    } else {
        registryIt->second.lifeCycleStatus = ComponentLifecycleStatus::Failed;
        registryIt->second.isDisabled = true;
        copyFailureReason(registryIt->second.lastFailureReason, sizeof(registryIt->second.lastFailureReason), reason);
        if (orchestration_.active && orchestration_.transitionId == transitionId && registryIt->second.isRequired) {
            orchestration_.requiredFailure = true;
        }
        ERROR_LOG("SystemController", "Component %s reported failure for transition id=%lu: %s",
                  normalizedName.c_str(),
                  static_cast<unsigned long>(transitionId),
                  reason ? reason : "<none>");
    }

    pendingTransitions_.erase(pendingIt);

    if (orchestration_.active && orchestration_.transitionId == transitionId && pendingTransitions_.empty()) {
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

        if (deferredReplayEvent_ || deferredPlayAfterReadyEvent_) {
            const bool dispatchReplay = deferredReplayEvent_;
            const bool dispatchStartupPlay = deferredPlayAfterReadyEvent_;
            deferredReplayEvent_ = false;
            deferredPlayAfterReadyEvent_ = false;
            if (dispatchReplay || dispatchStartupPlay) {
                handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
            }
        }
    }

    return true;
}

TransitionRequestDecision SystemController::requestTransition(SystemState from,
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

bool SystemController::finishTransition(uint32_t transitionId) {
    if (!hasActiveStateTransition_) {
        return false;
    }

    if (activeStateTransition_.transitionId != transitionId) {
        return false;
    }

    if (hasQueuedStateTransition_) {
        activeStateTransition_ = queuedStateTransition_;
        hasQueuedStateTransition_ = false;
        return true;
    }

    hasActiveStateTransition_ = false;
    return true;
}

bool SystemController::beginOrchestration(SystemState target,
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

    pendingTransitions_.clear();
    for (const auto& [componentName, entry] : componentRegistry_) {
        if (entry.isDisabled) {
            continue;
        }
        pendingTransitions_.emplace(componentName, PendingComponentTransition{transitionId});
    }

    if (pendingTransitions_.empty()) {
        transitionTo(target, trigger, reason, transitionId);
        (void)finishTransition(transitionId);
        orchestration_.active = false;

        if (deferredReplayEvent_ || deferredPlayAfterReadyEvent_) {
            const bool dispatchReplay = deferredReplayEvent_;
            const bool dispatchStartupPlay = deferredPlayAfterReadyEvent_;
            deferredReplayEvent_ = false;
            deferredPlayAfterReadyEvent_ = false;
            if (dispatchReplay || dispatchStartupPlay) {
                handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
            }
        }

        return true;
    }

    std::vector<std::string> componentNames;
    componentNames.reserve(pendingTransitions_.size());
    for (const auto& [componentName, pending] : pendingTransitions_) {
        (void)pending;
        componentNames.push_back(componentName);
    }

    for (const std::string& componentName : componentNames) {
        auto pendingIt = pendingTransitions_.find(componentName);
        if (pendingIt == pendingTransitions_.end()) {
            continue;
        }

        const auto hooksIt = componentHooks_.find(componentName);
        if (hooksIt == componentHooks_.end()) {
            DEBUG_LOG("SystemController", "No transition hooks registered for component %s", componentName.c_str());
            continue;
        }

        pendingIt->second.startedAtMs = nowMs();
        pendingIt->second.timeoutHandled = false;
        pendingIt->second.timeoutMs = hooksIt->second.transitionInvoker(target, transitionId);
    }

    return true;
}

bool SystemController::isOrchestrationActive() const {
    return orchestration_.active;
}

size_t SystemController::componentsWaitingForCompletion() const {
    return pendingTransitions_.size();
}

bool SystemController::hasActiveTransition() const {
    return hasActiveStateTransition_;
}

bool SystemController::hasQueuedTransition() const {
    return hasQueuedStateTransition_;
}

uint32_t SystemController::activeTransitionId() const {
    return activeStateTransition_.transitionId;
}

SystemState SystemController::activeTransitionFrom() const {
    return activeStateTransition_.from;
}

SystemState SystemController::activeTransitionTarget() const {
    return activeStateTransition_.target;
}

uint32_t SystemController::queuedTransitionId() const {
    return queuedStateTransition_.transitionId;
}

SystemState SystemController::queuedTransitionFrom() const {
    return queuedStateTransition_.from;
}

SystemState SystemController::queuedTransitionTarget() const {
    return queuedStateTransition_.target;
}
void SystemController::checkTransitionTimeouts() {
    if (!orchestration_.active) {
        return;
    }
    
    std::vector<std::string> timedOutComponents;
    const uint32_t currentMs = nowMs();
    
    for (const auto& [componentName, pending] : pendingTransitions_) {
        if (pending.transitionId != orchestration_.transitionId) {
            continue;
        }
        
        if (pending.timeoutHandled) {
            continue;
        }
        
        const bool timeoutReached = (pending.timeoutMs == 0) ||
                                    (currentMs - pending.startedAtMs >= pending.timeoutMs);
        if (timeoutReached) {
            timedOutComponents.push_back(componentName);
        }
    }
    
    for (const std::string& componentName : timedOutComponents) {
        auto pendingIt = pendingTransitions_.find(componentName);
        if (pendingIt == pendingTransitions_.end()) {
            continue;
        }
        
        if (pendingIt->second.transitionId != orchestration_.transitionId || pendingIt->second.timeoutHandled) {
            continue;
        }
        
        pendingIt->second.timeoutHandled = true;
        
        const auto hooksIt = componentHooks_.find(componentName);
        if (hooksIt == componentHooks_.end()) {
            (void)reportCompletion(componentName.c_str(),
                                   orchestration_.transitionId,
                                   TransitionStatus::Failed,
                                   "timeout hook missing");
            continue;
        }
        
        ERROR_LOG("SystemController", "Transition id=%lu timed out for component %s",
                  static_cast<unsigned long>(orchestration_.transitionId),
                  componentName.c_str());
        hooksIt->second.timeoutHook(orchestration_.transitionId);
    }
}

void SystemController::handleEvent(SystemEvent event, SystemReason reason) {
    auto requestStateTransition = [this, event, reason](SystemState target) {
        uint32_t transitionId = nextTransitionId_;
        ++nextTransitionId_;
        if (nextTransitionId_ == 0) {
            nextTransitionId_ = 1;
        }

        const bool started = beginOrchestration(target, event, reason, transitionId);
        DEBUG_LOG("SystemController", "Transition request id=%lu %s -> %s started=%s",
                  static_cast<unsigned long>(transitionId),
                  toString(state_),
                  toString(target),
                  started ? "true" : "false");
    };

    // User intents are state-independent and map directly to target states.
    if (event == SystemEvent::ENTER_OFF) {
        pendingReplayRequested_ = false;
        pendingPlayAfterReady_ = false;
        requestStateTransition(SystemState::SLEEP);
        return;
    }
    if (event == SystemEvent::STOP_REQUESTED) {
        pendingReplayRequested_ = false;
        pendingPlayAfterReady_ = false;
        if (state_ == SystemState::CONNECTING) {
            return;
        }
        requestStateTransition(SystemState::READY);
        return;
    }
    if (event == SystemEvent::PLAY_REQUESTED) {
        if (state_ == SystemState::CONNECTING) {
            pendingPlayAfterReady_ = true;
            return;
        }

        if (state_ == SystemState::SLEEP) {
            pendingPlayAfterReady_ = true;
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

void SystemController::transitionTo(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId) {
    if (next == state_) {
        return;
    }

    const SystemState previous = state_;
    state_ = next;

    if (next == SystemState::ERROR) {
        transientError_ = true;
        pendingPlayAfterReady_ = false;
    }
    if (next == SystemState::SLEEP || next == SystemState::CONNECTING || next == SystemState::READY) {
        transientError_ = false;
    }

    PROD_LOG("SystemController", "State transition id=%lu: %s -> %s (event=%s reason=%s)",
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

    if (previous == SystemState::CONNECTING && next == SystemState::READY && pendingPlayAfterReady_) {
        pendingPlayAfterReady_ = false;
        deferredPlayAfterReadyEvent_ = true;
    }
}

const char* toString(SystemState state) {
    switch (state) {
        case SystemState::BOOTING:
            return "BOOTING";
        case SystemState::SLEEP:
            return "SLEEP";
        case SystemState::CONNECTING:
            return "CONNECTING";
        case SystemState::READY:
            return "READY";
        case SystemState::LIVE:
            return "LIVE";
        case SystemState::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* toString(SystemEvent event) {
    switch (event) {
        case SystemEvent::BOOT:
            return "BOOT";
        case SystemEvent::COMPONENT_SETUP_FAILED:
            return "COMPONENT_SETUP_FAILED";
        case SystemEvent::WIFI_READY:
            return "WIFI_READY";
        case SystemEvent::AUDIO_INIT_OK:
            return "AUDIO_INIT_OK";
        case SystemEvent::AUDIO_INIT_FAILED:
            return "AUDIO_INIT_FAILED";
        case SystemEvent::PLAY_REQUESTED:
            return "PLAY_REQUESTED";
        case SystemEvent::STOP_REQUESTED:
            return "STOP_REQUESTED";
        case SystemEvent::WIFI_DISCONNECTED:
            return "WIFI_DISCONNECTED";
        case SystemEvent::STREAM_LOST:
            return "STREAM_LOST";
        case SystemEvent::RECOVER:
            return "RECOVER";
        case SystemEvent::ENTER_OFF:
            return "ENTER_OFF";
    }
    return "UNKNOWN";
}

const char* toString(SystemReason reason) {
    switch (reason) {
        case SystemReason::NONE:
            return "NONE";
        case SystemReason::BOOT_SEQUENCE:
            return "BOOT_SEQUENCE";
        case SystemReason::COMPONENT_SETUP:
            return "COMPONENT_SETUP";
        case SystemReason::WIFI_INITIALIZED:
            return "WIFI_INITIALIZED";
        case SystemReason::AUDIO_TASK_STARTED:
            return "AUDIO_TASK_STARTED";
        case SystemReason::AUDIO_TASK_INIT_FAILED:
            return "AUDIO_TASK_INIT_FAILED";
        case SystemReason::USER_REQUEST:
            return "USER_REQUEST";
        case SystemReason::RECOVERY:
            return "RECOVERY";
        case SystemReason::POWER_POLICY:
            return "POWER_POLICY";
    }
    return "UNKNOWN";
}

const char* toString(TransitionRequestDecision decision) {
    switch (decision) {
        case TransitionRequestDecision::Ignored:
            return "Ignored";
        case TransitionRequestDecision::Started:
            return "Started";
        case TransitionRequestDecision::Superseded:
            return "Superseded";
        case TransitionRequestDecision::Queued:
            return "Queued";
    }
    return "UNKNOWN";
}
