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

void SupervisorV2::postNextComponentState(ComponentID id) {
    ComponentMailbox* mailbox = componentMailboxes_[static_cast<int>(id)];
    if (mailbox == nullptr) return;
    portENTER_CRITICAL(&mailbox->spinlock);
    mailbox->pending = true;
    mailbox->targetState = nextState_.transitionTarget;
    portEXIT_CRITICAL(&mailbox->spinlock);
}

void SupervisorV2::completeTransition(ComponentID id, TransitionStatus status) {
    if (status == TransitionStatus::Completed) {
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
        return;
    }

    if (isRequired_[static_cast<int>(id)]) {
        postErrorEvent("component failed", id);
    } else {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
    }
}

void SupervisorV2::postStateRequest(SystemState target) {
    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    stateRequestMailbox_.pending = true;
    stateRequestMailbox_.requestedTarget = target;
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);
}

void SupervisorV2::postErrorEvent(DebugReason reason, ComponentID source) {
    portENTER_CRITICAL(&errorEvent_.spinlock);
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
    portEXIT_CRITICAL(&errorEvent_.spinlock);
}
