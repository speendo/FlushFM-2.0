#pragma once

#ifndef UNIT_TEST
#error "s2v2_access.h requires -DUNIT_TEST build flag"
#endif

#include "supervisor/supervisor_v2.h"

/** @brief Controlled access to SupervisorV2 private members for unit tests.
 *  Friend struct declared in SupervisorV2 under #ifdef UNIT_TEST.
 *  Tests use S2V2Access::method() instead of #define private public.
 */
struct S2V2Access {
    // --- observed / target state ---
    static void setObservedState(SupervisorV2& s, SystemState state) { s.observedState_ = state; }
    static SystemState getObservedState(const SupervisorV2& s) { return s.observedState_; }

    static void setTargetState(SupervisorV2& s, SystemState state) { s.targetState_ = state; }
    static SystemState getTargetState(const SupervisorV2& s) { return s.targetState_; }

    // --- orchestration flag ---
    static void setHasActiveOrchestration(SupervisorV2& s, bool v) { s.hasActiveOrchestration_ = v; }
    static bool getHasActiveOrchestration(const SupervisorV2& s) { return s.hasActiveOrchestration_; }

    // --- transition state ---
    static ActiveTransition& nextState(SupervisorV2& s) { return s.nextState_; }

    // --- private methods ---
    static void startOrchestration(SupervisorV2& s, SystemState target) { s.startOrchestration(target); }
    static void checkOrchestrationResponse(SupervisorV2& s) { s.checkOrchestrationResponse(); }
    static bool consumeStateRequest(SupervisorV2& s) { return s.consumeStateRequest(); }
    static void consumeErrorEvent(SupervisorV2& s) { s.consumeErrorEvent(); }
    static void setMaxRecoveries(SupervisorV2& s, int v) { s.setMaxRecoveries(v); }
    static uint32_t getTransitionTimeout(const SupervisorV2& s, SystemState state, bool isForward) {
        return s.getTransitionTimeout(state, isForward);
    }
    static void callSetTargetState(SupervisorV2& s, SystemState target) { s.setTargetState(target); }
    static void callSetObservedState(SupervisorV2& s, SystemState state) { s.setObservedState(state); }
    static SystemState callDetermineRecoveryTarget(SupervisorV2& s) { return s.determineRecoveryTarget(); }
    static void postNextComponentState(SupervisorV2& s, ComponentID id) { s.postNextComponentState(id); }
    static void checkComponentPresence(SupervisorV2& s) { s.checkComponentPresence(); }

    // --- retry policy ---
    static RetryPolicy& retryPolicy(SupervisorV2& s) { return s.retryPolicy_; }

    // --- error event ---
    static ErrorEvent& errorEvent(SupervisorV2& s) { return s.errorEvent_; }

    // --- state request mailbox ---
    static Mailbox& stateRequestMailbox(SupervisorV2& s) { return s.stateRequestMailbox_; }

    // --- response mailbox ---
    static void postResponse(SupervisorV2& s, OrchestrationResult result, EventBits_t bits) {
        s.responseMailbox_.post(result, bits);
    }
    static bool getResponsePending(const SupervisorV2& s) { return s.responseMailbox_.pending; }
    static void setResponsePending(SupervisorV2& s, bool v) { s.responseMailbox_.pending = v; }

    // --- event group ---
    static EventGroupHandle_t getEventGroup(const SupervisorV2& s) { return s.eventGroup_; }

    // --- order mailbox ---
    static bool getOrderPending(const SupervisorV2& s) { return s.orderMailbox_.pending; }
    static EventBits_t getOrderExpectedBits(const SupervisorV2& s) { return s.orderMailbox_.expectedBits; }
    static TickType_t getOrderTimeout(const SupervisorV2& s) { return s.orderMailbox_.timeoutTicks; }

    // --- fatal task ---
    static void setFatalEnteredTicks(SupervisorV2& s, TickType_t v) { s.fatalEnteredTicks_ = v; }
    static bool getFatalDeadlineElapsed(const SupervisorV2& s) { return s.fatalDeadlineElapsed_; }
    static void setFatalDeadlineElapsed(SupervisorV2& s, bool v) { s.fatalDeadlineElapsed_ = v; }
    static void setFatalTaskSpawned(SupervisorV2& s, bool v) { s.fatalTaskSpawned_ = v; }
    static bool getFatalTaskSpawned(const SupervisorV2& s) { return s.fatalTaskSpawned_; }

    // --- recovery target ---
    static SystemState getLastTargetBeforeError(const SupervisorV2& s) { return s.lastTargetBeforeError_; }
    static void setLastTargetBeforeError(SupervisorV2& s, SystemState state) { s.lastTargetBeforeError_ = state; }

    // --- component tracking ---
    static ComponentStatus getComponentStatus(const SupervisorV2& s, ComponentID id) {
        return s.componentStatuses_[static_cast<int>(id)];
    }
    static void setComponentStatus(SupervisorV2& s, ComponentID id, ComponentStatus status) {
        s.componentStatuses_[static_cast<int>(id)] = status;
    }
    static ComponentMailbox* getComponentMailbox(SupervisorV2& s, ComponentID id) {
        return s.componentMailboxes_[static_cast<int>(id)];
    }
};
