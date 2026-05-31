#include <unity.h>

#include "support/supervisor_access.h"

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

// --- idle (no transition needed) ---

void test_run_already_at_target_does_nothing() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setTargetState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
}

// --- stepping toward target ---

void test_run_steps_toward_target() {
    Supervisor supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setTargetState(supervisor, SystemState::CONNECTING);
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_TRUE(SupervisorAccess::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(SupervisorAccess::nextState(supervisor).transitionTarget));
    TEST_ASSERT_TRUE(SupervisorAccess::getOrderPending(supervisor));
}

void test_run_step_noop_when_already_at_target() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::LIVE);
    SupervisorAccess::setTargetState(supervisor, SystemState::LIVE);
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
}

// --- active orchestration: check response ---

void test_run_checks_orchestration_response_completed() {
    Supervisor supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setHasActiveOrchestration(supervisor, true);
    SupervisorAccess::nextState(supervisor).transitionTarget = SystemState::CONNECTING;
    SupervisorAccess::postResponse(supervisor, OrchestrationResult::COMPLETED, 0);

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
}

void test_run_checks_orchestration_response_timed_out() {
    Supervisor supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);
    SupervisorAccess::setMaxRecoveries(supervisor, 3);

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setHasActiveOrchestration(supervisor, true);
    SupervisorAccess::postResponse(supervisor, OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::WiFi));

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(SupervisorAccess::getComponentStatus(supervisor, ComponentID::WiFi)));
    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
}

void test_run_active_orchestration_blocks_stepping() {
    Supervisor supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setTargetState(supervisor, SystemState::LIVE);
    SupervisorAccess::setHasActiveOrchestration(supervisor, true);

    supervisor.run();

    TEST_ASSERT_TRUE(SupervisorAccess::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
}

// --- event processing ---

void test_run_consumes_state_request() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setTargetState(supervisor, SystemState::BOOTING);
    SupervisorAccess::stateRequestMailbox(supervisor).pending = true;
    SupervisorAccess::stateRequestMailbox(supervisor).requestedTarget = SystemState::LIVE;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(SupervisorAccess::getTargetState(supervisor)));
    TEST_ASSERT_FALSE(SupervisorAccess::stateRequestMailbox(supervisor).pending);
}

void test_run_consumes_error_event() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setTargetState(supervisor, SystemState::LIVE);
    SupervisorAccess::retryPolicy(supervisor).recoveryCounter = 0;
    SupervisorAccess::setMaxRecoveries(supervisor, 3);
    SupervisorAccess::errorEvent(supervisor).pending = true;
    SupervisorAccess::errorEvent(supervisor).reason = "test error";
    SupervisorAccess::errorEvent(supervisor).source = ComponentID::WiFi;

    supervisor.run();

    TEST_ASSERT_EQUAL(1, SupervisorAccess::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(SupervisorAccess::getTargetState(supervisor)));
}

void test_run_consumes_both_events_and_steps() {
    Supervisor supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    SupervisorAccess::setObservedState(supervisor, SystemState::BOOTING);
    SupervisorAccess::setTargetState(supervisor, SystemState::BOOTING);
    SupervisorAccess::stateRequestMailbox(supervisor).pending = true;
    SupervisorAccess::stateRequestMailbox(supervisor).requestedTarget = SystemState::CONNECTING;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(SupervisorAccess::getTargetState(supervisor)));
    TEST_ASSERT_FALSE(SupervisorAccess::stateRequestMailbox(supervisor).pending);
    TEST_ASSERT_TRUE(SupervisorAccess::getHasActiveOrchestration(supervisor));
}

// --- FATAL behavior ---

void test_run_skips_event_processing_in_fatal() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::FATAL);
    SupervisorAccess::setTargetState(supervisor, SystemState::BOOTING);
    SupervisorAccess::stateRequestMailbox(supervisor).pending = true;
    SupervisorAccess::stateRequestMailbox(supervisor).requestedTarget = SystemState::LIVE;
    SupervisorAccess::errorEvent(supervisor).pending = true;

    supervisor.run();

    TEST_ASSERT_TRUE(SupervisorAccess::stateRequestMailbox(supervisor).pending);
    TEST_ASSERT_TRUE(SupervisorAccess::errorEvent(supervisor).pending);
}

void test_run_skips_state_stepping_in_fatal() {
    Supervisor supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    SupervisorAccess::setObservedState(supervisor, SystemState::FATAL);
    SupervisorAccess::setTargetState(supervisor, SystemState::LIVE);
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
}

void test_run_calls_handle_fatal() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::FATAL);

    supervisor.run();

    TEST_ASSERT_TRUE(SupervisorAccess::getFatalTaskSpawned(supervisor));
}

// --- error recovery ---

void test_run_error_recovery_posts_state_request() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::ERROR);
    SupervisorAccess::setTargetState(supervisor, SystemState::ERROR);
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::LIVE);

    supervisor.run();

    TEST_ASSERT_TRUE(SupervisorAccess::stateRequestMailbox(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(SupervisorAccess::stateRequestMailbox(supervisor).requestedTarget));
}

void test_run_error_recovery_noop_when_target_matches() {
    Supervisor supervisor;

    SupervisorAccess::setObservedState(supervisor, SystemState::ERROR);
    SupervisorAccess::setTargetState(supervisor, SystemState::ERROR);
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::ERROR);

    supervisor.run();

    TEST_ASSERT_FALSE(SupervisorAccess::stateRequestMailbox(supervisor).pending);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_run_already_at_target_does_nothing);
    RUN_TEST(test_run_steps_toward_target);
    RUN_TEST(test_run_step_noop_when_already_at_target);
    RUN_TEST(test_run_checks_orchestration_response_completed);
    RUN_TEST(test_run_checks_orchestration_response_timed_out);
    RUN_TEST(test_run_active_orchestration_blocks_stepping);
    RUN_TEST(test_run_consumes_state_request);
    RUN_TEST(test_run_consumes_error_event);
    RUN_TEST(test_run_consumes_both_events_and_steps);
    RUN_TEST(test_run_skips_event_processing_in_fatal);
    RUN_TEST(test_run_skips_state_stepping_in_fatal);
    RUN_TEST(test_run_calls_handle_fatal);
    RUN_TEST(test_run_error_recovery_posts_state_request);
    RUN_TEST(test_run_error_recovery_noop_when_target_matches);
    return UNITY_END();
}
