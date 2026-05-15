#include "state_machine/supervisor_v2.h"

SupervisorV2::SupervisorV2() = default;

void SupervisorV2::setup() {
    eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_);
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

void SupervisorV2::registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired) {
    componentMailboxes_[static_cast<int>(id)] = mailbox;
    isRequired_[static_cast<int>(id)] = isRequired;
}
