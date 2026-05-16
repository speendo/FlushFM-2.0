#pragma once

#ifndef UNIT_TEST
#error "s2v2_access.h requires -DUNIT_TEST build flag"
#endif

#include "supervisor/supervisor_v2.h"

struct S2V2Access {
    static void setObservedState(SupervisorV2& s, SystemState state) { s.observedState_ = state; }
    static SystemState getObservedState(const SupervisorV2& s) { return s.observedState_; }

    static void setTargetState(SupervisorV2& s, SystemState state) { s.targetState_ = state; }
    static SystemState getTargetState(const SupervisorV2& s) { return s.targetState_; }

    static void setHasActiveOrchestration(SupervisorV2& s, bool v) { s.hasActiveOrchestration_ = v; }
    static bool getHasActiveOrchestration(const SupervisorV2& s) { return s.hasActiveOrchestration_; }

    static ActiveTransition& nextState(SupervisorV2& s) { return s.nextState_; }

    static RetryPolicy& retryPolicy(SupervisorV2& s) { return s.retryPolicy_; }
    static ErrorEvent& errorEvent(SupervisorV2& s) { return s.errorEvent_; }
    static Mailbox& stateRequestMailbox(SupervisorV2& s) { return s.stateRequestMailbox_; }

    static void setFatalEnteredTicks(SupervisorV2& s, TickType_t v) { s.fatalEnteredTicks_ = v; }
    static bool getFatalDeadlineElapsed(const SupervisorV2& s) { return s.fatalDeadlineElapsed_; }
    static void setFatalDeadlineElapsed(SupervisorV2& s, bool v) { s.fatalDeadlineElapsed_ = v; }

    static void setFatalTaskSpawned(SupervisorV2& s, bool v) { s.fatalTaskSpawned_ = v; }
    static bool getFatalTaskSpawned(const SupervisorV2& s) { return s.fatalTaskSpawned_; }

    static SystemState getLastTargetBeforeError(const SupervisorV2& s) { return s.lastTargetBeforeError_; }
    static void setLastTargetBeforeError(SupervisorV2& s, SystemState state) { s.lastTargetBeforeError_ = state; }

    static ComponentStatus getComponentStatus(const SupervisorV2& s, ComponentID id) {
        return s.componentStatuses_[static_cast<int>(id)];
    }
    static void setComponentStatus(SupervisorV2& s, ComponentID id, ComponentStatus status) {
        s.componentStatuses_[static_cast<int>(id)] = status;
    }

    static ComponentMailbox* getComponentMailbox(SupervisorV2& s, ComponentID id) {
        return s.componentMailboxes_[static_cast<int>(id)];
    }

    static bool getOrderPending(const SupervisorV2& s) { return s.orderMailbox_.pending; }
};
