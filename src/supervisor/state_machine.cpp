#include "supervisor/supervisor_v2.h"

#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "Supervisor";

}  // namespace

const char* stateToString(SystemState state) {
    switch (state) {
        case SystemState::FATAL: return "FATAL";
        case SystemState::ERROR: return "ERROR";
        case SystemState::SLEEP: return "SLEEP";
        case SystemState::BOOTING: return "BOOTING";
        case SystemState::CONNECTING: return "CONNECTING";
        case SystemState::READY: return "READY";
        case SystemState::LIVE: return "LIVE";
    }
    return "UNKNOWN";
}

bool isErrorState(SystemState state) {
    return state == SystemState::ERROR || state == SystemState::FATAL;
}

/** @brief Get the next system state based on the current and target states.
 *  @param current The current system state.
 *  @param target The target system state.
 *  @return The next system state.
 */
SystemState getNextState(SystemState current, SystemState target) {
    // FATAL is absorbent — no state transitions out of FATAL.
    if (current == SystemState::FATAL) return SystemState::FATAL;

    // ERROR and FATAL as target are immediate — no stepping needed.
    if (isErrorState(target)) {
        return target;
    }

    // Recovery from ERROR jumps directly to BOOTING, skipping SLEEP.
    // Only applies when the target is not SLEEP itself.
    if (isErrorState(current) && target != SystemState::SLEEP) {
        return SystemState::BOOTING;
    }

    // For all other combinations, step through the route based on rank comparison.
    // Lower rank = less active (FATAL=0, ERROR=10, SLEEP=20, BOOTING=30,
    // CONNECTING=40, READY=50, LIVE=60). Step up or down one rank at a time.
    int currentIndex = getIndex(current);
    int targetIndex = getIndex(target);

    if (currentIndex < 0 || targetIndex < 0) {
        ERROR_LOG(kLogSource, "Invalid state in getNextState: current=%s target=%s; falling back to FATAL",
                  stateToString(current), stateToString(target));
        return SystemState::FATAL;
    }

    if (currentIndex < targetIndex) {
        return stateRoute[currentIndex + 1]; // Step up toward target
    }
    if (currentIndex > targetIndex) {
        return stateRoute[currentIndex - 1]; // Step down toward target
    }
    return current; // Already at target
}

void SupervisorV2::checkComponentPresence() {
    // Scan all registered components. Post an error for any required
    // component that never called registerComponent (null mailbox pointer).
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] == nullptr && isRequired_[i]) {
            postErrorEvent("component absent", static_cast<ComponentID>(i));
        }
    }
}

bool SupervisorV2::consumeStateRequest() {
    SystemState target;
    bool hadPending = false;

    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    if (stateRequestMailbox_.pending) {
        target = stateRequestMailbox_.requestedTarget;
        stateRequestMailbox_.pending = false;
        hadPending = true;
    }
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);

    if (hadPending) {
        setTargetState(target);
    }
    return hadPending;
}

void SupervisorV2::consumeErrorEvent() {
    DebugReason reasonCopy = nullptr;
    ComponentID sourceCopy = ComponentID::Count;
    bool gotError = false;

    portENTER_CRITICAL(&errorEvent_.spinlock);
    if (errorEvent_.pending) {
        reasonCopy = errorEvent_.reason;
        sourceCopy = errorEvent_.source;
        errorEvent_.pending = false;
        errorEvent_.reason = nullptr;
        errorEvent_.source = ComponentID::Count;
        gotError = true;
    }
    portEXIT_CRITICAL(&errorEvent_.spinlock);

    if (!gotError) return;

    PROD_LOG(kLogSource, "[%s] %s - recovery attempt #%d/%d",
             componentName(sourceCopy), reasonCopy,
             retryPolicy_.recoveryCounter + 1, retryPolicy_.maxRecoveries);

    retryPolicy_.recoveryCounter++;

    if (retryPolicy_.isExhausted()) {
        setTargetState(SystemState::FATAL);
    } else {
        setTargetState(SystemState::ERROR);
    }
}

void SupervisorV2::setTargetState(SystemState target) {
    if (isErrorState(target) && !isErrorState(targetState_)) {
        lastTargetBeforeError_ = targetState_;
    }

    PROD_LOG(kLogSource, "Setting target state to %s", stateToString(target));
    targetState_ = target;
}

void SupervisorV2::resetRecoveryIfOutOfError() {
    if (!isErrorState(observedState_)) {
        retryPolicy_.recoveryCounter = 0;
    }
}

/** @brief Commit a new observed state.
 *  Logs the state transition, resets the recovery counter if exiting an error
 *  state, and clears the active orchestration flag.
 *  @param state The new observed state.
 */
void SupervisorV2::setObservedState(SystemState state) {
    PROD_LOG(kLogSource, "%s -> %s",
             stateToString(observedState_), stateToString(state));

    observedState_ = state;
    hasActiveOrchestration_ = false;

    resetRecoveryIfOutOfError();
}
