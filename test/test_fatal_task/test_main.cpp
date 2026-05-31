#include <unity.h>

#include "support/supervisor_access.h"
#include "supervisor/supervisor.h"

void fatalTask(Supervisor* supervisor);

namespace {

void test_fatal_task_sets_elapsed_flag() {
    Supervisor supervisor;
    nativeTickCount = 0;
    SupervisorAccess::setFatalEnteredTicks(supervisor, 0);

    fatalTask(&supervisor);

    // vTaskDelay(60000) in native stubs advances tick count past the deadline
    TEST_ASSERT_TRUE(SupervisorAccess::getFatalDeadlineElapsed(supervisor));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    return UNITY_END();
}
