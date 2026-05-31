#include <unity.h>

#include "support/supervisor_access.h"

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
    Supervisor supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(SupervisorAccess::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL_STRING("component failed", SupervisorAccess::errorEvent(supervisor).reason);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(SupervisorAccess::errorEvent(supervisor).source));
}

// ============================================================================
// Test group: checkOrchestrationResponse — mixed timeout
// ============================================================================

void test_check_response_mixed_timeout() {
    Supervisor supervisor;
    TestComponent wifi, audio, cli;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    SupervisorAccess::setMaxRecoveries(supervisor, 3);

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    EventBits_t timedOut = (1 << static_cast<int>(ComponentID::WiFi))
                         | (1 << static_cast<int>(ComponentID::AudioRuntime))
                         | (1 << static_cast<int>(ComponentID::CLI));
    SupervisorAccess::postResponse(supervisor, OrchestrationResult::TIMED_OUT, timedOut);

    SupervisorAccess::checkOrchestrationResponse(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(SupervisorAccess::getComponentStatus(supervisor, ComponentID::WiFi)));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(SupervisorAccess::getComponentStatus(supervisor, ComponentID::AudioRuntime)));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(SupervisorAccess::getComponentStatus(supervisor, ComponentID::CLI)));
    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
}

// ============================================================================
// Test group: startOrchestration — zero registered components
// ============================================================================

void test_start_orchestration_empty_bits_mask() {
    Supervisor supervisor;
    supervisor.setup();

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_TRUE(SupervisorAccess::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(0, SupervisorAccess::getOrderExpectedBits(supervisor));
}

// ============================================================================
// Test group: setMaxRecoveries — rejection of invalid values
// ============================================================================

void test_set_max_recoveries_rejects_invalid_values() {
    Supervisor supervisor;
    int original = SupervisorAccess::retryPolicy(supervisor).maxRecoveries;

    SupervisorAccess::setMaxRecoveries(supervisor, 0);
    TEST_ASSERT_EQUAL(original, SupervisorAccess::retryPolicy(supervisor).maxRecoveries);

    SupervisorAccess::setMaxRecoveries(supervisor, -1);
    TEST_ASSERT_EQUAL(original, SupervisorAccess::retryPolicy(supervisor).maxRecoveries);
}

void test_set_max_recoveries_accepts_valid_value() {
    Supervisor supervisor;

    SupervisorAccess::setMaxRecoveries(supervisor, 1);
    TEST_ASSERT_EQUAL(1, SupervisorAccess::retryPolicy(supervisor).maxRecoveries);

    SupervisorAccess::setMaxRecoveries(supervisor, 5);
    TEST_ASSERT_EQUAL(5, SupervisorAccess::retryPolicy(supervisor).maxRecoveries);
}

// ============================================================================
// Test group: getTransitionTimeout — per-state timeout lookup
// ============================================================================

void test_get_transition_timeout_forward_and_backward() {
    Supervisor supervisor;
    supervisor.setup();

    uint32_t forward = SupervisorAccess::getTransitionTimeout(supervisor, SystemState::BOOTING, true);
    uint32_t backward = SupervisorAccess::getTransitionTimeout(supervisor, SystemState::BOOTING, false);

    TEST_ASSERT_EQUAL(5000, forward);
    TEST_ASSERT_EQUAL(5000, backward);
}

void test_get_transition_timeout_invalid_state_returns_zero() {
    Supervisor supervisor;
    SystemState badState = static_cast<SystemState>(99);

    uint32_t result = SupervisorAccess::getTransitionTimeout(supervisor, badState, true);

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
