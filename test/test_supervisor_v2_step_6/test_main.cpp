#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private

namespace {

// --- setTargetState snapshot tests ---

void test_set_target_to_error_saves_last_target() {
    SupervisorV2 supervisor;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.lastTargetBeforeError_ = SystemState::BOOTING;

    supervisor.setTargetState(SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

void test_set_target_to_fatal_saves_last_target() {
    SupervisorV2 supervisor;
    supervisor.targetState_ = SystemState::CONNECTING;
    supervisor.lastTargetBeforeError_ = SystemState::BOOTING;

    supervisor.setTargetState(SystemState::FATAL);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

void test_set_target_error_to_error_does_not_restamp() {
    SupervisorV2 supervisor;
    supervisor.targetState_ = SystemState::ERROR;
    supervisor.lastTargetBeforeError_ = SystemState::LIVE;

    supervisor.setTargetState(SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

void test_set_target_non_error_does_not_snapshot() {
    SupervisorV2 supervisor;
    supervisor.lastTargetBeforeError_ = SystemState::READY;

    supervisor.setTargetState(SystemState::CONNECTING);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

// --- setObservedState enhancement tests ---

void test_set_observed_state_logs_and_resets_recovery() {
    SupervisorV2 supervisor;
    supervisor.retryPolicy_.recoveryCounter = 2;
    supervisor.hasActiveOrchestration_ = true;

    supervisor.setObservedState(SystemState::READY);

    TEST_ASSERT_EQUAL(0, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_set_observed_state_during_error_does_not_reset_recovery() {
    SupervisorV2 supervisor;
    supervisor.retryPolicy_.recoveryCounter = 2;

    supervisor.setObservedState(SystemState::ERROR);

    TEST_ASSERT_EQUAL(2, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(supervisor.observedState_));
}

void test_set_observed_state_during_fatal_does_not_reset_recovery() {
    SupervisorV2 supervisor;
    supervisor.retryPolicy_.recoveryCounter = 3;

    supervisor.setObservedState(SystemState::FATAL);

    TEST_ASSERT_EQUAL(3, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(supervisor.observedState_));
}

void test_set_observed_state_clears_active_orchestration() {
    SupervisorV2 supervisor;
    supervisor.hasActiveOrchestration_ = true;

    supervisor.setObservedState(SystemState::CONNECTING);

    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

// --- determineRecoveryTarget tests ---

void test_determine_recovery_target_returns_saved_target() {
    SupervisorV2 supervisor;
    supervisor.lastTargetBeforeError_ = SystemState::LIVE;

    SystemState result = supervisor.determineRecoveryTarget();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(result));
}

void test_determine_recovery_target_after_booting() {
    SupervisorV2 supervisor;
    supervisor.lastTargetBeforeError_ = SystemState::CONNECTING;

    SystemState result = supervisor.determineRecoveryTarget();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(result));
}

// --- handleFatal tests ---

void test_handle_fatal_sets_deadline_on_first_call() {
    SupervisorV2 supervisor;

    supervisor.handleFatal();

    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalDeadlineMs_);
    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

void test_handle_fatal_no_elapsed_before_deadline() {
    SupervisorV2 supervisor;
    supervisor.handleFatal();

    supervisor.fatalDeadlineElapsed_ = false;
    supervisor.handleFatal();

    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

void test_handle_fatal_detects_elapsed_deadline() {
    SupervisorV2 supervisor;
    supervisor.fatalDeadlineMs_ = 0;

    supervisor.handleFatal();

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_target_to_error_saves_last_target);
    RUN_TEST(test_set_target_to_fatal_saves_last_target);
    RUN_TEST(test_set_target_error_to_error_does_not_restamp);
    RUN_TEST(test_set_target_non_error_does_not_snapshot);
    RUN_TEST(test_set_observed_state_logs_and_resets_recovery);
    RUN_TEST(test_set_observed_state_during_error_does_not_reset_recovery);
    RUN_TEST(test_set_observed_state_during_fatal_does_not_reset_recovery);
    RUN_TEST(test_set_observed_state_clears_active_orchestration);
    RUN_TEST(test_determine_recovery_target_returns_saved_target);
    RUN_TEST(test_determine_recovery_target_after_booting);
    RUN_TEST(test_handle_fatal_sets_deadline_on_first_call);
    RUN_TEST(test_handle_fatal_no_elapsed_before_deadline);
    RUN_TEST(test_handle_fatal_detects_elapsed_deadline);
    return UNITY_END();
}
