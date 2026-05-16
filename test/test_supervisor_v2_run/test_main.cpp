#include <unity.h>

#include "support/s2v2_access.h"

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

// --- idle (no transition needed) ---

void test_run_already_at_target_does_nothing() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::BOOTING);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
}

// --- stepping toward target ---

void test_run_steps_toward_target() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::CONNECTING);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::nextState(supervisor).transitionTarget));
    TEST_ASSERT_TRUE(S2V2Access::getOrderPending(supervisor));
}

void test_run_step_noop_when_already_at_target() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::LIVE);
    S2V2Access::setTargetState(supervisor, SystemState::LIVE);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
}

// --- active orchestration: check response ---

void test_run_checks_orchestration_response_completed() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setHasActiveOrchestration(supervisor, true);
    S2V2Access::nextState(supervisor).transitionTarget = SystemState::CONNECTING;
    S2V2Access::postResponse(supervisor, OrchestrationResult::COMPLETED, 0);

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_run_checks_orchestration_response_timed_out() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);
    S2V2Access::setMaxRecoveries(supervisor, 3);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setHasActiveOrchestration(supervisor, true);
    S2V2Access::postResponse(supervisor, OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::WiFi));

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::WiFi)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_run_active_orchestration_blocks_stepping() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::LIVE);
    S2V2Access::setHasActiveOrchestration(supervisor, true);

    supervisor.run();

    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
}

// --- event processing ---

void test_run_consumes_state_request() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::BOOTING);
    S2V2Access::stateRequestMailbox(supervisor).pending = true;
    S2V2Access::stateRequestMailbox(supervisor).requestedTarget = SystemState::LIVE;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(S2V2Access::getTargetState(supervisor)));
    TEST_ASSERT_FALSE(S2V2Access::stateRequestMailbox(supervisor).pending);
}

void test_run_consumes_error_event() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::LIVE);
    S2V2Access::retryPolicy(supervisor).recoveryCounter = 0;
    S2V2Access::setMaxRecoveries(supervisor, 3);
    S2V2Access::errorEvent(supervisor).pending = true;
    S2V2Access::errorEvent(supervisor).reason = "test error";
    S2V2Access::errorEvent(supervisor).source = ComponentID::WiFi;

    supervisor.run();

    TEST_ASSERT_EQUAL(1, S2V2Access::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(S2V2Access::getTargetState(supervisor)));
}

void test_run_consumes_both_events_and_steps() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::BOOTING);
    S2V2Access::stateRequestMailbox(supervisor).pending = true;
    S2V2Access::stateRequestMailbox(supervisor).requestedTarget = SystemState::CONNECTING;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::getTargetState(supervisor)));
    TEST_ASSERT_FALSE(S2V2Access::stateRequestMailbox(supervisor).pending);
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
}

// --- FATAL behavior ---

void test_run_skips_event_processing_in_fatal() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::FATAL);
    S2V2Access::setTargetState(supervisor, SystemState::BOOTING);
    S2V2Access::stateRequestMailbox(supervisor).pending = true;
    S2V2Access::stateRequestMailbox(supervisor).requestedTarget = SystemState::LIVE;
    S2V2Access::errorEvent(supervisor).pending = true;

    supervisor.run();

    TEST_ASSERT_TRUE(S2V2Access::stateRequestMailbox(supervisor).pending);
    TEST_ASSERT_TRUE(S2V2Access::errorEvent(supervisor).pending);
}

void test_run_skips_state_stepping_in_fatal() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    S2V2Access::setObservedState(supervisor, SystemState::FATAL);
    S2V2Access::setTargetState(supervisor, SystemState::LIVE);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_run_calls_handle_fatal() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::FATAL);

    supervisor.run();

    TEST_ASSERT_TRUE(S2V2Access::getFatalTaskSpawned(supervisor));
}

// --- error recovery ---

void test_run_error_recovery_posts_state_request() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::ERROR);
    S2V2Access::setTargetState(supervisor, SystemState::ERROR);
    S2V2Access::setHasActiveOrchestration(supervisor, false);
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::LIVE);

    supervisor.run();

    TEST_ASSERT_TRUE(S2V2Access::stateRequestMailbox(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(S2V2Access::stateRequestMailbox(supervisor).requestedTarget));
}

void test_run_error_recovery_noop_when_target_matches() {
    SupervisorV2 supervisor;

    S2V2Access::setObservedState(supervisor, SystemState::ERROR);
    S2V2Access::setTargetState(supervisor, SystemState::ERROR);
    S2V2Access::setHasActiveOrchestration(supervisor, false);
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::ERROR);

    supervisor.run();

    TEST_ASSERT_FALSE(S2V2Access::stateRequestMailbox(supervisor).pending);
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
