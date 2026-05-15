#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

// ============================================================================
// Test group: getNextState — rank-based transitions
// ============================================================================
// The state rank table is: FATAL=0, ERROR=10, SLEEP=20, BOOTING=30,
// CONNECTING=40, READY=50, LIVE=60.
// Prior tests cover FATAL absorbent (always stays FATAL) and ERROR
// recovery jump (ERROR -> BOOTING). These tests cover the general
// downward and upward stepping paths and the invalid-state fallback.

void test_get_next_state_downward_rank_stepping() {
    // LIVE (rank 60, route index 6) -> SLEEP (rank 20, route index 2).
    // currentIndex=6 > targetIndex=2, so step down: stateRoute[5]=READY.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(getNextState(SystemState::LIVE, SystemState::SLEEP)));

    // READY (index 5) -> CONNECTING (index 4). Single step down.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(getNextState(SystemState::READY, SystemState::CONNECTING)));
}

void test_get_next_state_upward_rank_stepping() {
    // SLEEP (rank 20, route index 2) -> LIVE (rank 60, route index 6).
    // currentIndex=2 < targetIndex=6, so step up: stateRoute[3]=BOOTING.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(getNextState(SystemState::SLEEP, SystemState::LIVE)));

    // BOOTING (index 3) -> LIVE (index 6). One step: CONNECTING.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(getNextState(SystemState::BOOTING, SystemState::LIVE)));
}

void test_get_next_state_invalid_falls_back_to_fatal() {
    SystemState badState = static_cast<SystemState>(99);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::BOOTING, badState)));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(badState, SystemState::LIVE)));
}

// ============================================================================
// Test group: completeTransition — required component failure detail
// ============================================================================

void test_complete_transition_required_failed_writes_error_event() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(supervisor.errorEvent_.pending);
    TEST_ASSERT_EQUAL_STRING("component failed", supervisor.errorEvent_.reason);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(supervisor.errorEvent_.source));
}

// ============================================================================
// Test group: checkOrchestrationResponse — mixed timeout
// ============================================================================

void test_check_response_mixed_timeout() {
    SupervisorV2 supervisor;
    TestComponent wifi, audio, cli;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setMaxRecoveries(3);

    supervisor.observedState_ = SystemState::BOOTING;
    EventBits_t timedOut = (1 << static_cast<int>(ComponentID::WiFi))
                         | (1 << static_cast<int>(ComponentID::AudioRuntime))
                         | (1 << static_cast<int>(ComponentID::CLI));
    supervisor.responseMailbox_.post(OrchestrationResult::TIMED_OUT, timedOut);

    supervisor.checkOrchestrationResponse();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::WiFi)]));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::AudioRuntime)]));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::CLI)]));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

// ============================================================================
// Test group: startOrchestration — zero registered components
// ============================================================================

void test_start_orchestration_empty_bits_mask() {
    SupervisorV2 supervisor;
    supervisor.setup();

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(0, supervisor.orderMailbox_.expectedBits);
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::PENDING),
                      static_cast<int>(supervisor.nextState_.subState));
}

// ============================================================================
// Test group: setMaxRecoveries — rejection of invalid values
// ============================================================================

void test_set_max_recoveries_rejects_invalid_values() {
    SupervisorV2 supervisor;
    int original = supervisor.retryPolicy_.maxRecoveries;

    supervisor.setMaxRecoveries(0);
    TEST_ASSERT_EQUAL(original, supervisor.retryPolicy_.maxRecoveries);

    supervisor.setMaxRecoveries(-1);
    TEST_ASSERT_EQUAL(original, supervisor.retryPolicy_.maxRecoveries);
}

void test_set_max_recoveries_accepts_valid_value() {
    SupervisorV2 supervisor;

    supervisor.setMaxRecoveries(1);
    TEST_ASSERT_EQUAL(1, supervisor.retryPolicy_.maxRecoveries);

    supervisor.setMaxRecoveries(5);
    TEST_ASSERT_EQUAL(5, supervisor.retryPolicy_.maxRecoveries);
}

// ============================================================================
// Test group: getTransitionTimeout — per-state timeout lookup
// ============================================================================

void test_get_transition_timeout_forward_and_backward() {
    SupervisorV2 supervisor;
    supervisor.setup();

    uint32_t forward = supervisor.getTransitionTimeout(SystemState::BOOTING, true);
    uint32_t backward = supervisor.getTransitionTimeout(SystemState::BOOTING, false);

    TEST_ASSERT_EQUAL(5000, forward);
    TEST_ASSERT_EQUAL(5000, backward);
}

void test_get_transition_timeout_invalid_state_returns_zero() {
    SupervisorV2 supervisor;
    SystemState badState = static_cast<SystemState>(99);

    uint32_t result = supervisor.getTransitionTimeout(badState, true);

    TEST_ASSERT_EQUAL(0, result);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_next_state_downward_rank_stepping);
    RUN_TEST(test_get_next_state_upward_rank_stepping);
    RUN_TEST(test_get_next_state_invalid_falls_back_to_fatal);
    RUN_TEST(test_complete_transition_required_failed_writes_error_event);
    RUN_TEST(test_check_response_mixed_timeout);
    RUN_TEST(test_start_orchestration_empty_bits_mask);
    RUN_TEST(test_set_max_recoveries_rejects_invalid_values);
    RUN_TEST(test_set_max_recoveries_accepts_valid_value);
    RUN_TEST(test_get_transition_timeout_forward_and_backward);
    RUN_TEST(test_get_transition_timeout_invalid_state_returns_zero);
    return UNITY_END();
}
