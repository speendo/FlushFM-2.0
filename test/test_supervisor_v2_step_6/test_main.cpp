#include <unity.h>

#include "support/supervisor_access.h"

void fatalTask(Supervisor* supervisor);

namespace {

// --- setTargetState snapshot tests ---

void test_set_target_to_error_saves_last_target() {
    Supervisor supervisor;
    SupervisorAccess::setTargetState(supervisor, SystemState::LIVE);
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::BOOTING);

    SupervisorAccess::callSetTargetState(supervisor, SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(SupervisorAccess::getLastTargetBeforeError(supervisor)));
}

void test_set_target_to_fatal_saves_last_target() {
    Supervisor supervisor;
    SupervisorAccess::setTargetState(supervisor, SystemState::CONNECTING);
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::BOOTING);

    SupervisorAccess::callSetTargetState(supervisor, SystemState::FATAL);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(SupervisorAccess::getLastTargetBeforeError(supervisor)));
}

void test_set_target_error_to_error_does_not_restamp() {
    Supervisor supervisor;
    SupervisorAccess::setTargetState(supervisor, SystemState::ERROR);
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::LIVE);

    SupervisorAccess::callSetTargetState(supervisor, SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(SupervisorAccess::getLastTargetBeforeError(supervisor)));
}

void test_set_target_non_error_does_not_snapshot() {
    Supervisor supervisor;
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::READY);

    SupervisorAccess::callSetTargetState(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(SupervisorAccess::getLastTargetBeforeError(supervisor)));
}

// --- setObservedState enhancement tests ---

void test_set_observed_state_logs_and_resets_recovery() {
    Supervisor supervisor;
    SupervisorAccess::retryPolicy(supervisor).recoveryCounter = 2;
    SupervisorAccess::setHasActiveOrchestration(supervisor, false);

    SupervisorAccess::callSetObservedState(supervisor, SystemState::READY);

    TEST_ASSERT_EQUAL(0, SupervisorAccess::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(SupervisorAccess::getHasActiveOrchestration(supervisor));
}

void test_set_observed_state_during_error_does_not_reset_recovery() {
    Supervisor supervisor;
    SupervisorAccess::retryPolicy(supervisor).recoveryCounter = 2;

    SupervisorAccess::callSetObservedState(supervisor, SystemState::ERROR);

    TEST_ASSERT_EQUAL(2, SupervisorAccess::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
}

void test_set_observed_state_during_fatal_does_not_reset_recovery() {
    Supervisor supervisor;
    SupervisorAccess::retryPolicy(supervisor).recoveryCounter = 3;

    SupervisorAccess::callSetObservedState(supervisor, SystemState::FATAL);

    TEST_ASSERT_EQUAL(3, SupervisorAccess::retryPolicy(supervisor).recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(SupervisorAccess::getObservedState(supervisor)));
}

// --- determineRecoveryTarget tests ---

void test_determine_recovery_target_returns_saved_target() {
    Supervisor supervisor;
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::LIVE);

    SystemState result = SupervisorAccess::callDetermineRecoveryTarget(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(result));
}

void test_determine_recovery_target_after_booting() {
    Supervisor supervisor;
    SupervisorAccess::setLastTargetBeforeError(supervisor, SystemState::CONNECTING);

    SystemState result = SupervisorAccess::callDetermineRecoveryTarget(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(result));
}

// --- fatalTask tests ---

void test_fatal_task_sets_elapsed_flag() {
    Supervisor supervisor;
    nativeTickCount = 0;
    SupervisorAccess::setFatalEnteredTicks(supervisor, 0);

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(SupervisorAccess::getFatalDeadlineElapsed(supervisor));
}

void test_run_wakes_then_spawns_fatal_task() {
    Supervisor supervisor;
    SupervisorAccess::setObservedState(supervisor, SystemState::FATAL);

    supervisor.run();

    TEST_ASSERT_TRUE(SupervisorAccess::getFatalTaskSpawned(supervisor));
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
    RUN_TEST(test_run_wakes_then_spawns_fatal_task);
    return UNITY_END();
}
