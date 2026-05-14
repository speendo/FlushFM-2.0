#include "state_machine/supervisor_v2.h"

#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "Supervisor";

}  // namespace

/** @brief Get the index of a system state in the state route array.
 *  @param state The system state to find.
 *  @return The index of the state, or -1 if not found.
 */
static int getIndex(SystemState state) {
    for (size_t i = 0; i < stateCount; ++i) {
        if (stateRoute[i] == state) return static_cast<int>(i);
    }
    return -1; // Invalid state
}

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
    if (isErrorState(target)) {
        return target; // Immediate transition for error states
    }

    // Recovery from ERROR/FATAL jumps directly to BOOTING, skipping SLEEP
    if (isErrorState(current) && target != SystemState::SLEEP) {
        return SystemState::BOOTING;
    }
    
    // For other states, step through the route based on rank comparison
    int currentIndex = getIndex(current);
    int targetIndex = getIndex(target);

    if (currentIndex < 0 || targetIndex < 0) {
        ERROR_LOG(kLogSource, "Invalid state in getNextState: current=%s target=%s; falling back to FATAL",
                  stateToString(current), stateToString(target));
        return SystemState::FATAL; // Invalid states, enter FATAL as a safe fallback
    }

    if (currentIndex < targetIndex) {
        return stateRoute[currentIndex + 1]; // Step up
    }
    if (currentIndex > targetIndex) {
        return stateRoute[currentIndex - 1]; // Step down
    }
    return current; // Already at target
}

SupervisorV2::SupervisorV2() = default;

void SupervisorV2::setup() {
	loadTransitionTimeoutConfig();
}

int SupervisorV2::getMaxRecoveries() const {
	return retryPolicy_.maxRecoveries;
}

void SupervisorV2::setMaxRecoveries(int recoveries) {
	if (recoveries >= 1) {
		retryPolicy_.maxRecoveries = recoveries;
	}
}

uint32_t SupervisorV2::getTransitionTimeout(SystemState state, bool isForward) const {
    int idx = getIndex(state);
    if (idx >= 0 && idx < static_cast<int>(stateCount)) {
        return isForward ? timeoutConfig_.forwardTimeouts[idx]
                            : timeoutConfig_.backwardTimeouts[idx];
    }
    return 0;
}

void SupervisorV2::loadTransitionTimeoutConfig() {
	timeoutConfig_.forwardTimeouts = kDefaultForwardTimeouts;
	timeoutConfig_.backwardTimeouts = kDefaultBackwardTimeouts;
}

SystemState SupervisorV2::getObservedState() const {
    return observedState_;
}

SystemState SupervisorV2::getTargetState() const {
    return targetState_;
}

void SupervisorV2::postStateRequest(SystemState target) {
	stateRequestMailbox_.pending = true;
	stateRequestMailbox_.requestedTarget = target;
}

void SupervisorV2::postErrorEvent(DebugReason reason, ComponentID source) {
	if (!errorEvent_.pending) {
		errorEvent_.pending = true;
		errorEvent_.reason = reason;
		errorEvent_.source = source;
	}
}

bool SupervisorV2::consumeStateRequest() {
    if (!stateRequestMailbox_.pending) {
        return false;
    }
    setTargetState(stateRequestMailbox_.requestedTarget);
    stateRequestMailbox_.pending = false;
    return true;
}

void SupervisorV2::consumeErrorEvent() {
	if (!errorEvent_.pending) {
		return;
	}

	PROD_LOG(kLogSource, "[%s] %s - recovery attempt #%d/%d",
	         componentName(errorEvent_.source), errorEvent_.reason,
	         retryPolicy_.recoveryCounter + 1, retryPolicy_.maxRecoveries);

	retryPolicy_.recoveryCounter++;

	if (retryPolicy_.isExhausted()) {
		setTargetState(SystemState::FATAL);
	}

	errorEvent_.pending = false;
	errorEvent_.reason = nullptr;
	errorEvent_.source = ComponentID::Count;
}

void SupervisorV2::setTargetState(SystemState target) {
    PROD_LOG(kLogSource, "Setting target state to %s", stateToString(target));
    targetState_ = target;
}

void SupervisorV2::resetRecoveryIfOutOfError() {
	if (!isErrorState(observedState_)) {
		retryPolicy_.recoveryCounter = 0;
	}
}