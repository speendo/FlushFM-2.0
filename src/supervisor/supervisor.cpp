#include "supervisor/supervisor.h"

Supervisor::Supervisor() = default;

void Supervisor::setup() {
    eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_);
    if (eventGroup_ == nullptr) {
        return;
    }
    loadTransitionTimeoutConfig();

    supervisorTaskHandle_ = xTaskGetCurrentTaskHandle();

    xTaskCreatePinnedToCore(
        orchestrationWorker,            // entry point: the function the task runs
        "OrchWorker",                   // human-readable name (visible in debugger/FreeRTOS tracing)
        4096,                           // stack size in bytes
        this,                           // argument passed as void*
        1,                              // priority: 1 = below default 2, yields to state machine
        &workerTaskHandle_,             // out parameter: receives the task handle
        0                               // core affinity: 0 = Core 0, same as state machine
    );
}

int Supervisor::getMaxRecoveries() const {
    return retryPolicy_.maxRecoveries;
}

void Supervisor::setMaxRecoveries(int recoveries) {
    if (recoveries >= 1) {
        retryPolicy_.maxRecoveries = recoveries;
    }
}

uint32_t Supervisor::getTransitionTimeout(SystemState state, bool isForward) const {
    int idx = getIndex(state);
    if (idx >= 0 && idx < static_cast<int>(stateCount)) {
        return isForward ? timeoutConfig_.forwardTimeouts[idx]
                            : timeoutConfig_.backwardTimeouts[idx];
    }
    return 0;
}

void Supervisor::loadTransitionTimeoutConfig() {
    timeoutConfig_.forwardTimeouts = kDefaultForwardTimeouts;
    timeoutConfig_.backwardTimeouts = kDefaultBackwardTimeouts;
}

SystemState Supervisor::getObservedState() const {
    return observedState_;
}

SystemState Supervisor::getTargetState() const {
    return targetState_;
}

void Supervisor::registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired) {
    configASSERT(static_cast<size_t>(id) < componentCount);
    // Store the mailbox pointer for cross-core writes. Null means absent.
    componentMailboxes_[static_cast<size_t>(id)] = mailbox;
    // Track required/optional for boot presence checks and failure handling.
    isRequired_[static_cast<size_t>(id)] = isRequired;
}
