#include <unity.h>

#include "support/s2v2_access.h"
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#include "../../src/supervisor/fatal_task.cpp"

void fatalTask(SupervisorV2* supervisor);

namespace {

// --- setTargetState snapshot tests ---

void test_set_target_to_error_saves_last_target() {
    SupervisorV2 supervisor;
    S2V2Access::setTargetState(supervisor, SystemState::LIVE);
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::BOOTING);

    S2V2Access::callSetTargetState(supervisor, SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(S2V2Access::getLastTargetBeforeError(supervisor)));
}

void test_set_target_to_fatal_saves_last_target() {
    SupervisorV2 supervisor;
    S2V2Access::setTargetState(supervisor, SystemState::CONNECTING);
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::BOOTING);

    S2V2Access::callSetTargetState(supervisor, SystemState::FATAL);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::getLastTargetBeforeError(supervisor)));
}

void test_set_target_error_to_error_does_not_restamp() {
    SupervisorV2 supervisor;
    S2V2Access::setTargetState(supervisor, SystemState::ERROR);
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::LIVE);

    S2V2Access::callSetTargetState(supervisor, SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(S2V2Access::getLastTargetBeforeError(supervisor)));
}

void test_set_target_non_error_does_not_snapshot() {
    SupervisorV2 supervisor;
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::READY);

    S2V2Access::callSetTargetState(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(S2V2Access::getLastTargetBeforeError(supervisor)));
}

// --- setObservedState enhancement tests ---

void test_set_observed_state_logs_and_resets_recovery() {
    SupervisorV2 supervisor;
    S2V2Access::retryPolicy(supervisor).recoveryCounter = 2;
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    S2V2Access::callSetObservedState(supervisor, SystemState::READY);

    TEST_ASSERT_EQUAL(0, S2V2Access::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_set_observed_state_during_error_does_not_reset_recovery() {
    SupervisorV2 supervisor;
    S2V2Access::retryPolicy(supervisor).recoveryCounter = 2;

    S2V2Access::callSetObservedState(supervisor, SystemState::ERROR);

    TEST_ASSERT_EQUAL(2, S2V2Access::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
}

void test_set_observed_state_during_fatal_does_not_reset_recovery() {
    SupervisorV2 supervisor;
    S2V2Access::retryPolicy(supervisor).recoveryCounter = 3;

    S2V2Access::callSetObservedState(supervisor, SystemState::FATAL);

    TEST_ASSERT_EQUAL(3, S2V2Access::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
}

// --- determineRecoveryTarget tests ---

void test_determine_recovery_target_returns_saved_target() {
    SupervisorV2 supervisor;
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::LIVE);

    SystemState result = S2V2Access::callDetermineRecoveryTarget(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(result));
}

void test_determine_recovery_target_after_booting() {
    SupervisorV2 supervisor;
    S2V2Access::setLastTargetBeforeError(supervisor, SystemState::CONNECTING);

    SystemState result = S2V2Access::callDetermineRecoveryTarget(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(result));
}

// --- fatalTask tests ---

void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    S2V2Access::setFatalEnteredTicks(supervisor, 1);

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(S2V2Access::getFatalDeadlineElapsed(supervisor));
}

void test_fatal_task_no_elapsed_before_deadline() {
    SupervisorV2 supervisor;
    S2V2Access::setFatalEnteredTicks(supervisor, 0);

    fatalTask(&supervisor);

    TEST_ASSERT_FALSE(S2V2Access::getFatalDeadlineElapsed(supervisor));
}

void test_run_wakes_then_spawns_fatal_task() {
    SupervisorV2 supervisor;
    S2V2Access::setObservedState(supervisor, SystemState::FATAL);

    supervisor.run();

    TEST_ASSERT_TRUE(S2V2Access::getFatalTaskSpawned(supervisor));
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
    RUN_TEST(test_determine_recovery_target_returns_saved_target);
    RUN_TEST(test_determine_recovery_target_after_booting);
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    RUN_TEST(test_fatal_task_no_elapsed_before_deadline);
    RUN_TEST(test_run_wakes_then_spawns_fatal_task);
    return UNITY_END();
}
