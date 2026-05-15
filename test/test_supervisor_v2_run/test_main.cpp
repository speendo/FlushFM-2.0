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

// --- idle (no transition needed) ---

void test_run_already_at_target_does_nothing() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(supervisor.observedState_));
}

// --- stepping toward target ---

void test_run_steps_toward_target() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::CONNECTING;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.nextState_.transitionTarget));
    TEST_ASSERT_TRUE(supervisor.orderMailbox_.pending);
}

void test_run_step_noop_when_already_at_target() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::LIVE;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.observedState_));
}

// --- active orchestration: check response ---

void test_run_checks_orchestration_response_completed() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.hasActiveOrchestration_ = true;
    supervisor.nextState_.transitionTarget = SystemState::CONNECTING;
    supervisor.responseMailbox_.post(OrchestrationResult::COMPLETED, 0);

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_run_checks_orchestration_response_timed_out() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);
    supervisor.setMaxRecoveries(3);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.hasActiveOrchestration_ = true;
    supervisor.responseMailbox_.post(OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::WiFi));

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::WiFi)]));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_run_active_orchestration_blocks_stepping() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.hasActiveOrchestration_ = true;

    supervisor.run();

    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(supervisor.observedState_));
}

// --- event processing ---

void test_run_consumes_state_request() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.stateRequestMailbox_.pending = true;
    supervisor.stateRequestMailbox_.requestedTarget = SystemState::LIVE;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.targetState_));
    TEST_ASSERT_FALSE(supervisor.stateRequestMailbox_.pending);
}

void test_run_consumes_error_event() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.retryPolicy_.recoveryCounter = 0;
    supervisor.setMaxRecoveries(3);
    supervisor.errorEvent_.pending = true;
    supervisor.errorEvent_.reason = "test error";
    supervisor.errorEvent_.source = ComponentID::WiFi;

    supervisor.run();

    TEST_ASSERT_EQUAL(1, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(supervisor.targetState_));
}

void test_run_consumes_both_events_and_steps() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.stateRequestMailbox_.pending = true;
    supervisor.stateRequestMailbox_.requestedTarget = SystemState::CONNECTING;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.targetState_));
    TEST_ASSERT_FALSE(supervisor.stateRequestMailbox_.pending);
    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
}

// --- FATAL behavior ---

void test_run_skips_event_processing_in_fatal() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::FATAL;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.stateRequestMailbox_.pending = true;
    supervisor.stateRequestMailbox_.requestedTarget = SystemState::LIVE;
    supervisor.errorEvent_.pending = true;

    supervisor.run();

    TEST_ASSERT_TRUE(supervisor.stateRequestMailbox_.pending);
    TEST_ASSERT_TRUE(supervisor.errorEvent_.pending);
}

void test_run_skips_state_stepping_in_fatal() {
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::FATAL;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_run_calls_handle_fatal() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::FATAL;
    supervisor.fatalEntered_ = false;

    supervisor.run();

    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalDeadlineMs_);
    TEST_ASSERT_TRUE(supervisor.fatalEntered_);
}

// --- error recovery ---

void test_run_error_recovery_posts_state_request() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::ERROR;
    supervisor.hasActiveOrchestration_ = false;
    supervisor.lastTargetBeforeError_ = SystemState::LIVE;

    supervisor.run();

    TEST_ASSERT_TRUE(supervisor.stateRequestMailbox_.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.stateRequestMailbox_.requestedTarget));
}

void test_run_error_recovery_noop_when_target_matches() {
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::ERROR;
    supervisor.hasActiveOrchestration_ = false;
    supervisor.lastTargetBeforeError_ = SystemState::ERROR;

    supervisor.run();

    TEST_ASSERT_FALSE(supervisor.stateRequestMailbox_.pending);
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
