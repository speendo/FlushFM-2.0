#include <unity.h>

#include "support/s2v2_access.h"
#include "supervisor/supervisor_v2.h"

void fatalTask(SupervisorV2* supervisor);

namespace {

void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    S2V2Access::setFatalEnteredTicks(supervisor, 1);

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(S2V2Access::getFatalDeadlineElapsed(supervisor));
}

void test_fatal_task_does_not_set_elapsed_before_dwell() {
    SupervisorV2 supervisor;
    S2V2Access::setFatalEnteredTicks(supervisor, 0);

    fatalTask(&supervisor);

    TEST_ASSERT_FALSE(S2V2Access::getFatalDeadlineElapsed(supervisor));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    RUN_TEST(test_fatal_task_does_not_set_elapsed_before_dwell);
    return UNITY_END();
}
