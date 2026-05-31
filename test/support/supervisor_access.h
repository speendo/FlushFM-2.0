#pragma once

#ifndef UNIT_TEST
#error "s2v2_access.h requires -DUNIT_TEST build flag"
#endif

#include "supervisor/supervisor.h"

/** @brief Controlled access to Supervisor private members for unit tests.
 *  Friend struct declared in Supervisor under #ifdef UNIT_TEST.
 *  Tests use SupervisorAccess::method() instead of #define private public.
 */
struct SupervisorAccess {
    // --- observed / target state ---
    static void setObservedState(Supervisor& s, SystemState state) { s.observedState_ = state; }
    static SystemState getObservedState(const Supervisor& s) { return s.observedState_; }

    static void setTargetState(Supervisor& s, SystemState state) { s.targetState_ = state; }
    static SystemState getTargetState(const Supervisor& s) { return s.targetState_; }

    // --- orchestration flag ---
    static void setHasActiveOrchestration(Supervisor& s, bool v) { s.hasActiveOrchestration_ = v; }
    static bool getHasActiveOrchestration(const Supervisor& s) { return s.hasActiveOrchestration_; }

    // --- transition state ---
    static ActiveTransition& nextState(Supervisor& s) { return s.nextState_; }

    // --- private methods ---
    static void startOrchestration(Supervisor& s, SystemState target) { s.startOrchestration(target); }
    static void checkOrchestrationResponse(Supervisor& s) { s.checkOrchestrationResponse(); }
    static bool consumeStateRequest(Supervisor& s) { return s.consumeStateRequest(); }
    static void consumeErrorEvent(Supervisor& s) { s.consumeErrorEvent(); }
    static void setMaxRecoveries(Supervisor& s, int v) { s.setMaxRecoveries(v); }
    static uint32_t getTransitionTimeout(const Supervisor& s, SystemState state, bool isForward) {
        return s.getTransitionTimeout(state, isForward);
    }
    static void callSetTargetState(Supervisor& s, SystemState target) { s.setTargetState(target); }
    static void callSetObservedState(Supervisor& s, SystemState state) { s.setObservedState(state); }
    static SystemState callDetermineRecoveryTarget(Supervisor& s) { return s.determineRecoveryTarget(); }
    static void postNextComponentState(Supervisor& s, ComponentID id) { s.postNextComponentState(id); }
    static void checkComponentPresence(Supervisor& s) { s.checkComponentPresence(); }

    // --- retry policy ---
    static RetryPolicy& retryPolicy(Supervisor& s) { return s.retryPolicy_; }

    // --- error event ---
    static ErrorEvent& errorEvent(Supervisor& s) { return s.errorEvent_; }

    // --- state request mailbox ---
    static Mailbox& stateRequestMailbox(Supervisor& s) { return s.stateRequestMailbox_; }

    // --- response mailbox ---
    static void postResponse(Supervisor& s, OrchestrationResult result, EventBits_t bits) {
        s.responseMailbox_.post(result, bits);
    }
    static bool getResponsePending(const Supervisor& s) { return s.responseMailbox_.pending; }
    static void setResponsePending(Supervisor& s, bool v) { s.responseMailbox_.pending = v; }

    // --- event group ---
    static EventGroupHandle_t getEventGroup(const Supervisor& s) { return s.eventGroup_; }

    // --- order mailbox ---
    static bool getOrderPending(const Supervisor& s) { return s.orderMailbox_.pending; }
    static EventBits_t getOrderExpectedBits(const Supervisor& s) { return s.orderMailbox_.expectedBits; }
    static TickType_t getOrderTimeout(const Supervisor& s) { return s.orderMailbox_.timeoutTicks; }

    // --- fatal task ---
    static void setFatalEnteredTicks(Supervisor& s, TickType_t v) { s.fatalEnteredTicks_ = v; }
    static bool getFatalDeadlineElapsed(const Supervisor& s) { return s.fatalDeadlineElapsed_; }
    static void setFatalDeadlineElapsed(Supervisor& s, bool v) { s.fatalDeadlineElapsed_ = v; }
    static void setFatalTaskSpawned(Supervisor& s, bool v) { s.fatalTaskSpawned_ = v; }
    static bool getFatalTaskSpawned(const Supervisor& s) { return s.fatalTaskSpawned_; }

    // --- recovery target ---
    static SystemState getLastTargetBeforeError(const Supervisor& s) { return s.lastTargetBeforeError_; }
    static void setLastTargetBeforeError(Supervisor& s, SystemState state) { s.lastTargetBeforeError_ = state; }

    // --- component tracking ---
    static ComponentStatus getComponentStatus(const Supervisor& s, ComponentID id) {
        return s.componentStatuses_[static_cast<int>(id)];
    }
    static void setComponentStatus(Supervisor& s, ComponentID id, ComponentStatus status) {
        s.componentStatuses_[static_cast<int>(id)] = status;
    }
    static ComponentMailbox* getComponentMailbox(Supervisor& s, ComponentID id) {
        return s.componentMailboxes_[static_cast<int>(id)];
    }
};
