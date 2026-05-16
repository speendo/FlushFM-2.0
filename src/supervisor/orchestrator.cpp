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
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
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

    xEventGroupClearBits(eventGroup_, kAllComponentBits);

    nextState_.subState = SubState::PENDING;
    hasActiveOrchestration_ = true;

    orderMailbox_.post(bits, xTaskGetTickCount() + getTransitionTimeout(target,
        getIndex(target) > getIndex(observedState_)), target);
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

/** @brief Orchestration worker task. Reads orders from orderMailbox_ and blocks
 *  on xEventGroupWaitBits until all expected bits are set or the deadline expires.
 *  Posts a result back to responseMailbox_ for the state machine to consume.
 *  @param param Pointer to the SupervisorV2 instance (cast from void*).
 */
void orchestrationWorker(void* param) {
    auto* supervisor = static_cast<SupervisorV2*>(param);
    for (;;) {
        // Read an order from the state machine. If none pending, yield briefly
        // (10 FreeRTOS ticks = 10ms with 1ms/tick config) and try again.
        // consume() uses an embedded spinlock so this is safe cross-core.
        if (!supervisor->orderMailbox_.consume()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Compute how long to wait: absolute deadline (in ms) minus current time (in ms).
        // pdMS_TO_TICKS converts ms to FreeRTOS ticks. With default 1ms/tick config
        // this is a no-op, but using it explicitly ensures portability.
        TickType_t now = xTaskGetTickCount();
        TickType_t waitTicks = pdMS_TO_TICKS(supervisor->orderMailbox_.deadlineMs - now);

        // FreeRTOS xEventGroupWaitBits blocks the task until either:
        //   1. All bits in expectedBits are set -> returns the matched bits
        //   2. waitTicks elapses -> returns only the bits that ARE set
        // The two pdTRUE arguments mean:
        //   pdTRUE (clear on exit) - atomically clear the matched bits when returning
        //   pdTRUE (wait for all)  - ALL expectedBits must be set, not just any one
        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                supervisor->orderMailbox_.expectedBits,
                                                pdTRUE,
                                                pdTRUE,
                                                waitTicks);

        // If all expected bits are accounted for in the return value, the
        // orchestration completed successfully. Otherwise, some bits are
        // missing - compute which ones for the TIMED_OUT response.
        if ((bits & supervisor->orderMailbox_.expectedBits) == supervisor->orderMailbox_.expectedBits) {
            supervisor->responseMailbox_.post(OrchestrationResult::COMPLETED, 0);
        } else {
            EventBits_t missing = supervisor->orderMailbox_.expectedBits & ~bits;
            supervisor->responseMailbox_.post(OrchestrationResult::TIMED_OUT, missing);
        }

        // Wake the state machine task so it processes the response immediately.
        // xTaskNotifyGive sends a direct-to-task notification; the state
        // machine's run() loop is blocked on ulTaskNotifyTake() and will
        // unblock when this call completes.
        xTaskNotifyGive(supervisor->supervisorTaskHandle_);
    }
}
