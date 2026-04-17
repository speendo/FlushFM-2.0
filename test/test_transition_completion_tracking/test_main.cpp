#include <type_traits>

#include <unity.h>

#include "../../src/state_machine/system_controller.cpp"

namespace {

static_assert(std::is_same<decltype(&SystemController::beginComponentTransition), bool (SystemController::*)(const char*, uint32_t)>::value,
              "SystemController::beginComponentTransition signature mismatch");
static_assert(std::is_same<decltype(&SystemController::reportCompletion), bool (SystemController::*)(const char*, uint32_t, TransitionStatus, DebugReason)>::value,
              "SystemController::reportCompletion signature mismatch");

void test_completed_report_marks_component_ready() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.beginComponentTransition("WiFi", 100));
    TEST_ASSERT_TRUE(controller.reportCompletion("WiFi", 100, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Ready), static_cast<int>(controller.getComponentStatus("WiFi")));
}

void test_failed_report_marks_component_failed() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("AudioRuntime", true));
    TEST_ASSERT_TRUE(controller.beginComponentTransition("AudioRuntime", 9));
    TEST_ASSERT_TRUE(controller.reportCompletion("AudioRuntime", 9, TransitionStatus::Failed, "timeout"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed), static_cast<int>(controller.getComponentStatus("AudioRuntime")));
}

void test_stale_transition_report_is_ignored() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
    TEST_ASSERT_TRUE(controller.beginComponentTransition("CLI", 42));
    TEST_ASSERT_FALSE(controller.reportCompletion("CLI", 41, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus("CLI")));
    TEST_ASSERT_TRUE(controller.reportCompletion("CLI", 42, TransitionStatus::Completed, nullptr));
}

void test_double_report_is_ignored_after_completion() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("BoardInfo", false));
    TEST_ASSERT_TRUE(controller.beginComponentTransition("BoardInfo", 7));
    TEST_ASSERT_TRUE(controller.reportCompletion("BoardInfo", 7, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_FALSE(controller.reportCompletion("BoardInfo", 7, TransitionStatus::Completed, nullptr));
}

void test_second_begin_is_rejected_while_transition_pending() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("Sensor", false));
    TEST_ASSERT_TRUE(controller.beginComponentTransition("Sensor", 1));
    TEST_ASSERT_FALSE(controller.beginComponentTransition("Sensor", 2));
    TEST_ASSERT_TRUE(controller.reportCompletion("Sensor", 1, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(controller.beginComponentTransition("Sensor", 2));
}

void test_transition_begin_for_unknown_component_is_rejected() {
    SystemController controller;

    TEST_ASSERT_FALSE(controller.beginComponentTransition("Missing", 3));
    TEST_ASSERT_FALSE(controller.reportCompletion("Missing", 3, TransitionStatus::Completed, nullptr));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_completed_report_marks_component_ready);
    RUN_TEST(test_failed_report_marks_component_failed);
    RUN_TEST(test_stale_transition_report_is_ignored);
    RUN_TEST(test_double_report_is_ignored_after_completion);
    RUN_TEST(test_second_begin_is_rejected_while_transition_pending);
    RUN_TEST(test_transition_begin_for_unknown_component_is_rejected);
    return UNITY_END();
}