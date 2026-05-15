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

/** @brief Begin an orchestration toward the given target state.
 *  Computes the expected-bits mask, clears the event group, writes all
 *  component mailboxes, and posts an OrchestrationOrder for the worker task.
 *  @param target The intermediate stepping state to orchestrate toward.
 */
void SupervisorV2::startOrchestration(SystemState target) {
    // Build the expected-bits mask: one bit per registered, non-degraded
    // component. Both required and optional components participate in the
    // quorum — optional components are only excluded after they time out or
    // explicitly fail (at which point they become DEGRADED).
    EventBits_t bits = 0;
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] != nullptr
            && componentStatuses_[i] != ComponentStatus::DEGRADED) {
            bits |= (1 << i);
        }
    }

    xEventGroupClearBits(eventGroup_, kAllComponentBits);

    // Set the transition target before writing mailboxes — postNextComponentState
    // reads nextState_.transitionTarget to know what to write.
    nextState_.transitionTarget = target;

    // Write the stepping target to every registered component's mailbox.
    // Components read this on their own task loop and react accordingly.
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] != nullptr) {
            postNextComponentState(static_cast<ComponentID>(i));
        }
    }

    // Look up the per-state timeout. Forward if the target has a higher rank
    // than the current observed state, backward otherwise.
    bool isForward = (getIndex(target) > getIndex(observedState_));
    uint32_t timeout = getTransitionTimeout(target, isForward);

    orderMailbox_.post(bits, xTaskGetTickCount() + timeout, target);

    nextState_.subState = SubState::PENDING;
    hasActiveOrchestration_ = true;
}

/** @brief Check for a pending orchestration response from the worker task.
 *  Reads responseMailbox_ (non-blocking, spinlock inside).
 *  On COMPLETED: advances observedState_ to the orchestration target.
 *  On TIMED_OUT: clears the active flag and handles overdue components.
 */
void SupervisorV2::checkOrchestrationResponse() {
    if (!responseMailbox_.consume()) return;

    // Clear the active flag regardless of outcome — the orchestration cycle
    // is done (either all bits arrived or the deadline elapsed).
    hasActiveOrchestration_ = false;

    if (responseMailbox_.result == OrchestrationResult::COMPLETED) {
        nextState_.subState = SubState::COMMITTED;
        setObservedState(nextState_.transitionTarget);
    } else {
        // TIMED_OUT — some components did not set their event group bits
        EventBits_t timedOut = responseMailbox_.timedOutComponents;
        for (size_t i = 0; i < componentCount; i++) {
            if (!(timedOut & (1 << i))) continue;
            if (isRequired_[i]) {
                componentStatuses_[i] = ComponentStatus::FAILED;
                postErrorEvent("transition timeout", static_cast<ComponentID>(i));
            } else {
                componentStatuses_[i] = ComponentStatus::DEGRADED;
            }
        }
        // The posted error event will be consumed on the next run() tick,
        // setting targetState_ to ERROR or FATAL as appropriate.
    }
}
