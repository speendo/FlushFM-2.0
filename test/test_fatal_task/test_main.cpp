#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#include "../../src/supervisor/fatal_task.cpp"
#undef private

void fatalTask(SupervisorV2* supervisor);

namespace {

void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    supervisor.fatalEnteredTicks_ = 1;

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}

void test_fatal_task_does_not_set_elapsed_before_dwell() {
    SupervisorV2 supervisor;
    supervisor.fatalEnteredTicks_ = 0;

    fatalTask(&supervisor);

    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    RUN_TEST(test_fatal_task_does_not_set_elapsed_before_dwell);
    return UNITY_END();
}
