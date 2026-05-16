#include "supervisor/supervisor_v2.h"
#include "supervisor/orchestrator.h"

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
        if (eventGroup_ == nullptr) return;
        // Set this component's bit in the event group. The orchestration
        // completes when all required, non-degraded components have set
        // their bits — checked on each run() tick.
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
        return;
    }

    // Component reported Failed. How we handle it depends on whether this
    // component is required or optional:
    //   - Required: post an error event; the state machine processes it after
    //     the current orchestration cycle ends (via timeout or completion),
    //     then decides the next target.
    //   - Optional: mark as DEGRADED and exclude from the orchestration
    //     quorum. The remaining components are expected to finish normally.
    if (isRequired_[static_cast<int>(id)]) {
        postErrorEvent("component failed", id);
    } else {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
        if (eventGroup_ != nullptr) {
            xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
        }
    }
}

void SupervisorV2::postStateRequest(SystemState target) {
    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    stateRequestMailbox_.pending = true;
    stateRequestMailbox_.requestedTarget = target;
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);

    if (supervisorTaskHandle_ != nullptr) {
        xTaskNotifyGive(supervisorTaskHandle_);
    }
}

void SupervisorV2::postErrorEvent(DebugReason reason, ComponentID source) {
    portENTER_CRITICAL(&errorEvent_.spinlock);
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
    portEXIT_CRITICAL(&errorEvent_.spinlock);

    if (supervisorTaskHandle_ != nullptr) {
        xTaskNotifyGive(supervisorTaskHandle_);
    }
}

/** @brief Begin an orchestration toward the given target state.
 *  Computes the expected-bits mask, clears the event group, writes all
 *  component mailboxes, and posts an OrchestrationOrder for the worker task.
 *  @param target The intermediate stepping state to orchestrate toward.
 */
void SupervisorV2::startOrchestration(SystemState target) {
    // Set the transition target before writing mailboxes — postNextComponentState
    // reads nextState_.transitionTarget to know what to write.
    nextState_.transitionTarget = target;

    if (firstOrchestration_) {
        firstOrchestration_ = false;
        checkComponentPresence();
    }

    xEventGroupClearBits(eventGroup_, kAllComponentBits);

    // Build expected-bits mask and write mailboxes in a single pass.
    // Unregistered components (nullptr) are skipped entirely.
    // DEGRADED components get a mailbox write but no event-group bit.
    EventBits_t bits = 0;
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] == nullptr) continue;  // unregistered

        if (componentStatuses_[i] != ComponentStatus::DEGRADED) {
            bits |= (1 << i);                              // part of quorum
        }

        postNextComponentState(static_cast<ComponentID>(i)); // always write mailbox
    }

    nextState_.subState = SubState::PENDING;
    hasActiveOrchestration_ = true;

    orderMailbox_.post(bits,
        pdMS_TO_TICKS(getTransitionTimeout(target,
            getIndex(target) > getIndex(observedState_))),
        target);
    if (workerTaskHandle_ != nullptr) {
        xTaskNotifyGive(workerTaskHandle_);
    }
}

/** @brief Check for a pending orchestration response from the worker task.
 *  Reads responseMailbox_ (non-blocking, spinlock inside).
 *  On COMPLETED: advances observedState_ to the orchestration target.
 *  On TIMED_OUT: clears the active flag and handles overdue components.
 */
void SupervisorV2::checkOrchestrationResponse() {
    OrchestrationResult result;
    EventBits_t timedOutComponents;
    if (!responseMailbox_.consume(result, timedOutComponents)) return;

    hasActiveOrchestration_ = false;

    if (result == OrchestrationResult::COMPLETED) {
        nextState_.subState = SubState::COMMITTED;
        setObservedState(nextState_.transitionTarget);
    } else {
        for (size_t i = 0; i < componentCount; i++) {
            if (!(timedOutComponents & (1 << i))) continue;
            if (isRequired_[i]) {
                componentStatuses_[i] = ComponentStatus::FAILED;
                postErrorEvent("transition timeout", static_cast<ComponentID>(i));
            } else {
                componentStatuses_[i] = ComponentStatus::DEGRADED;
            }
        }
    }
}

/** @brief Orchestration worker task. Reads orders from orderMailbox_ and blocks
 *  on xEventGroupWaitBits until all expected bits are set or the deadline expires.
 *  Posts a result back to responseMailbox_ for the state machine to consume.
 *  @param param Pointer to the SupervisorV2 instance (cast from void*).
 */
void orchestrationWorker(void* param) {
    auto* supervisor = static_cast<SupervisorV2*>(param);
    for (;;) {
        EventBits_t expectedBits;
        TickType_t timeoutTicks;
        SystemState transitionTarget;
        if (!supervisor->orderMailbox_.consume(expectedBits, timeoutTicks, transitionTarget)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                 expectedBits,
                                                 pdTRUE,
                                                 pdTRUE,
                                                 timeoutTicks);

        if ((bits & expectedBits) == expectedBits) {
            supervisor->responseMailbox_.post(OrchestrationResult::COMPLETED, 0);
        } else {
            EventBits_t missing = expectedBits & ~bits;
            supervisor->responseMailbox_.post(OrchestrationResult::TIMED_OUT, missing);
        }

        xTaskNotifyGive(supervisor->supervisorTaskHandle_);
    }
}
