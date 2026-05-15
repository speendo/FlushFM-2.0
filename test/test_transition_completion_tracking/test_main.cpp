#include <unity.h>

#include "../../src/supervisor/supervisor.cpp"

namespace {

void test_completed_report_marks_component_ready() {
    Supervisor controller;

    TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::WiFi, true));
    TEST_ASSERT_TRUE(controller.beginComponentTransition(ComponentID::WiFi, 100));
    TEST_ASSERT_TRUE(controller.reportCompletion(ComponentID::WiFi, 100, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Ready), static_cast<int>(controller.getComponentStatus(ComponentID::WiFi)));
}

void test_failed_report_marks_component_failed() {
    Supervisor controller;

    TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::AudioRuntime, true));
    TEST_ASSERT_TRUE(controller.beginComponentTransition(ComponentID::AudioRuntime, 9));
    TEST_ASSERT_TRUE(controller.reportCompletion(ComponentID::AudioRuntime, 9, TransitionStatus::Failed, "timeout"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed), static_cast<int>(controller.getComponentStatus(ComponentID::AudioRuntime)));
}

void test_stale_transition_report_is_ignored() {
    Supervisor controller;

    TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::CLI, false));
    TEST_ASSERT_TRUE(controller.beginComponentTransition(ComponentID::CLI, 42));
    TEST_ASSERT_FALSE(controller.reportCompletion(ComponentID::CLI, 41, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus(ComponentID::CLI)));
    TEST_ASSERT_TRUE(controller.reportCompletion(ComponentID::CLI, 42, TransitionStatus::Completed, nullptr));
}

void test_double_report_is_ignored_after_completion() {
    Supervisor controller;

    TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::BoardInfo, false));
    TEST_ASSERT_TRUE(controller.beginComponentTransition(ComponentID::BoardInfo, 7));
    TEST_ASSERT_TRUE(controller.reportCompletion(ComponentID::BoardInfo, 7, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_FALSE(controller.reportCompletion(ComponentID::BoardInfo, 7, TransitionStatus::Completed, nullptr));
}

void test_second_begin_is_rejected_while_transition_pending() {
    Supervisor controller;

    TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::CLI, false));
    TEST_ASSERT_TRUE(controller.beginComponentTransition(ComponentID::CLI, 1));
    TEST_ASSERT_FALSE(controller.beginComponentTransition(ComponentID::CLI, 2));
    TEST_ASSERT_TRUE(controller.reportCompletion(ComponentID::CLI, 1, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(controller.beginComponentTransition(ComponentID::CLI, 2));
}

void test_transition_begin_for_unknown_component_is_rejected() {
    Supervisor controller;

    TEST_ASSERT_FALSE(controller.beginComponentTransition(ComponentID::Count, 3));
    TEST_ASSERT_FALSE(controller.reportCompletion(ComponentID::Count, 3, TransitionStatus::Completed, nullptr));
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
