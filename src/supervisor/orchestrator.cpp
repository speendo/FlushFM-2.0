#include "supervisor/supervisor_v2.h"

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
        // Set this component's bit in the event group. The orchestration
        // completes when all required, non-degraded components have set
        // their bits — checked on each run() tick.
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
        return;
    }

    // Component reported Failed. How we handle it depends on whether this
    // component is required or optional:
    //   - Required: post an error event which the supervisor consumes on the
    //     next run() tick. This sets targetState_ to ERROR and aborts the
    //     current orchestration. The recovery logic then decides what to do.
    //   - Optional: mark as DEGRADED and exclude from the orchestration
    //     quorum. The remaining components are expected to finish normally.
    if (isRequired_[static_cast<int>(id)]) {
        postErrorEvent("component failed", id);
    } else {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
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
