#include "state_machine/supervisor_v2.h"

void SupervisorV2::postNextComponentState(ComponentID id) {
    // Write the current stepping state to a component's mailbox under spinlock.
    // The component will read this in its own loop and react.
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
