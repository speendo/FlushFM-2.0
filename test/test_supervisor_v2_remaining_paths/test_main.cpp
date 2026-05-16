#include <unity.h>

#include "support/s2v2_access.h"

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

// ============================================================================
// Test group: getNextState — rank-based transitions
// ============================================================================

void test_get_next_state_downward_rank_stepping() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(getNextState(SystemState::LIVE, SystemState::SLEEP)));

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(getNextState(SystemState::READY, SystemState::CONNECTING)));
}

void test_get_next_state_upward_rank_stepping() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(getNextState(SystemState::SLEEP, SystemState::LIVE)));

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

    TEST_ASSERT_TRUE(S2V2Access::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL_STRING("component failed", S2V2Access::errorEvent(supervisor).reason);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(S2V2Access::errorEvent(supervisor).source));
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
    S2V2Access::setMaxRecoveries(supervisor, 3);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    EventBits_t timedOut = (1 << static_cast<int>(ComponentID::WiFi))
                         | (1 << static_cast<int>(ComponentID::AudioRuntime))
                         | (1 << static_cast<int>(ComponentID::CLI));
    S2V2Access::postResponse(supervisor, OrchestrationResult::TIMED_OUT, timedOut);

    S2V2Access::checkOrchestrationResponse(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::WiFi)));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::AudioRuntime)));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::CLI)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

// ============================================================================
// Test group: startOrchestration — zero registered components
// ============================================================================

void test_start_orchestration_empty_bits_mask() {
    SupervisorV2 supervisor;
    supervisor.setup();

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(0, S2V2Access::getOrderExpectedBits(supervisor));
}

// ============================================================================
// Test group: setMaxRecoveries — rejection of invalid values
// ============================================================================

void test_set_max_recoveries_rejects_invalid_values() {
    SupervisorV2 supervisor;
    int original = S2V2Access::retryPolicy(supervisor).maxRecoveries;

    S2V2Access::setMaxRecoveries(supervisor, 0);
    TEST_ASSERT_EQUAL(original, S2V2Access::retryPolicy(supervisor).maxRecoveries);

    S2V2Access::setMaxRecoveries(supervisor, -1);
    TEST_ASSERT_EQUAL(original, S2V2Access::retryPolicy(supervisor).maxRecoveries);
}

void test_set_max_recoveries_accepts_valid_value() {
    SupervisorV2 supervisor;

    S2V2Access::setMaxRecoveries(supervisor, 1);
    TEST_ASSERT_EQUAL(1, S2V2Access::retryPolicy(supervisor).maxRecoveries);

    S2V2Access::setMaxRecoveries(supervisor, 5);
    TEST_ASSERT_EQUAL(5, S2V2Access::retryPolicy(supervisor).maxRecoveries);
}

// ============================================================================
// Test group: getTransitionTimeout — per-state timeout lookup
// ============================================================================

void test_get_transition_timeout_forward_and_backward() {
    SupervisorV2 supervisor;
    supervisor.setup();

    uint32_t forward = S2V2Access::getTransitionTimeout(supervisor, SystemState::BOOTING, true);
    uint32_t backward = S2V2Access::getTransitionTimeout(supervisor, SystemState::BOOTING, false);

    TEST_ASSERT_EQUAL(5000, forward);
    TEST_ASSERT_EQUAL(5000, backward);
}

void test_get_transition_timeout_invalid_state_returns_zero() {
    SupervisorV2 supervisor;
    SystemState badState = static_cast<SystemState>(99);

    uint32_t result = S2V2Access::getTransitionTimeout(supervisor, badState, true);

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
