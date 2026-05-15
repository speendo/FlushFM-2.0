#include "supervisor/supervisor_v2.h"

#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "Supervisor";

}  // namespace

constexpr TickType_t kFatalDwellMs = 60000;

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
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] == nullptr && isRequired_[i]) {
            postErrorEvent("component absent", static_cast<ComponentID>(i));
        }
    }
}

/** @brief Compute the next stepping state and begin an orchestration.
 *  Delegates to getNextState() which uses the state rank table to determine
 *  the next intermediate state along the path from observedState_ toward
 *  targetState_. If already at the target (getNextState returns the same
 *  value as observedState_), this is a no-op — no orchestration is started,
 *  no component mailboxes are written, and the event group is left alone.
 *
 *  Called by run() when targetState_ != observedState_ and no orchestration
 *  is currently in flight (hasActiveOrchestration_ == false).
 */
void SupervisorV2::stepTowardTarget() {
    SystemState nextSteppingState = getNextState(observedState_, targetState_);
    if (nextSteppingState == observedState_) return;
    startOrchestration(nextSteppingState);
}

/** @brief Execute one tick of the supervisor state machine loop.
 *
 *  This is the top-level entry point called by the FreeRTOS state machine
 *  task. It implements a four-phase processing pipeline:
 *
 *  Phase 1 — Wait for wake signal:
 *    Calls ulTaskNotifyTake(pdTRUE, portMAX_DELAY) which blocks the task
 *    until another task or ISR calls xTaskNotifyGive() on its handle.
 *    Three wake sources exist:
 *      - postStateRequest()  — external component requests a new target state
 *      - postErrorEvent()    — component reports a failure
 *      - orchestrationWorker — reports COMPLETED or TIMED_OUT via responseMailbox_
 *    On native builds ulTaskNotifyTake is a no-op stub that returns 0,
 *    so run() executes synchronously in tests.
 *
 *  Phase 2 — Drain pending events (skipped in FATAL):
 *    consumeErrorEvent() and consumeStateRequest() drain the two
 *    single-slot mailboxes. Errors are consumed FIRST so they can override
 *    any stale state request that arrived before the error.
 *
 *  Phase 3 — State stepping (skipped in FATAL):
 *    Three mutually exclusive branches based on current conditions:
 *    A. Need to move: targetState_ != observedState_ with no active
 *       orchestration → stepTowardTarget()
 *    B. Active orchestration in flight → checkOrchestrationResponse()
 *    C. Idle in ERROR → determineRecoveryTarget() + postStateRequest()
 *
 *  Phase 4 — FATAL housekeeping:
 *    If observedState_ == FATAL, calls handleFatal() which arms a 60-second
 *    dwell timer on first entry and triggers deep sleep once the timer
 *    expires. FATAL is absorbent: no state transitions can exit it.
 */
void SupervisorV2::run() {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (observedState_ != SystemState::FATAL) {
        consumeErrorEvent();
        consumeStateRequest();

        if (targetState_ != observedState_ && !hasActiveOrchestration_) {
            stepTowardTarget();
        } else if (hasActiveOrchestration_) {
            checkOrchestrationResponse();
        } else if (observedState_ == SystemState::ERROR) {
            SystemState recoveryTarget = determineRecoveryTarget();
            if (recoveryTarget != observedState_) {
                postStateRequest(recoveryTarget);
            }
        }
    }

    if (observedState_ == SystemState::FATAL) {
        handleFatal();
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
    // hasActiveOrchestration_ already cleared by checkOrchestrationResponse()
    resetRecoveryIfOutOfError();
}

/** @brief Determine the target state to aim for after ERROR recovery.
 *  Placeholder: returns the pre-error target snapshot captured by
 *  setTargetState() when the target entered ERROR or FATAL. This will
 *  be replaced with real logic once recovery policies are defined.
 *  @return The recovery target state.
 */
SystemState SupervisorV2::determineRecoveryTarget() {
    return lastTargetBeforeError_;
}

/** @brief Manage the deep sleep shutdown after FATAL.
 *  On first call, records the deadline 60 seconds from now. On subsequent
 *  calls, checks whether the deadline has elapsed. When it has, sets the
 *  fatalDeadlineElapsed_ flag so tests can observe the state. On actual
 *  hardware, this would also trigger esp_deep_sleep_start().
 */
void SupervisorV2::handleFatal() {
    if (!fatalEntered_) {
        fatalEntered_ = true;
        fatalDeadlineMs_ = xTaskGetTickCount() + pdMS_TO_TICKS(kFatalDwellMs);
        return;
    }

    if (xTaskGetTickCount() >= fatalDeadlineMs_) {
        fatalDeadlineElapsed_ = true;
#if defined(ARDUINO)
        esp_deep_sleep_start();
#endif
    }
}
