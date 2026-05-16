#include <unity.h>

#include "support/s2v2_access.h"
#include "supervisor/supervisor_v2.h"

void fatalTask(SupervisorV2* supervisor);

namespace {

void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    nativeTickCount = 0;
    S2V2Access::setFatalEnteredTicks(supervisor, 0);

    fatalTask(&supervisor);

    // vTaskDelay(60000) in native stubs advances tick count past the deadline
    TEST_ASSERT_TRUE(S2V2Access::getFatalDeadlineElapsed(supervisor));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    return UNITY_END();
}
