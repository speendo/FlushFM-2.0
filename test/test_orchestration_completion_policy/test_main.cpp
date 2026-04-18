#include <type_traits>

#include <unity.h>

#include "../../src/state_machine/system_controller.cpp"

namespace {

static_assert(std::is_same<decltype(&SystemController::beginOrchestration), bool (SystemController::*)(SystemState, SystemEvent, SystemReason, uint32_t)>::value,
              "SystemController::beginOrchestration signature mismatch");

void test_begin_orchestration_tracks_registered_components() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::LIVE,
                                                   SystemEvent::PLAY_REQUESTED,
                                                   SystemReason::USER_REQUEST,
                                                   100));

    TEST_ASSERT_TRUE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(controller.componentsWaitingForCompletion()));
    TEST_ASSERT_EQUAL_UINT32(100, controller.activeTransitionId());
}

void test_all_successful_completions_commit_target_state() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::READY,
                                                   SystemEvent::RECOVER,
                                                   SystemReason::RECOVERY,
                                                   200));

    TEST_ASSERT_TRUE(controller.reportCompletion("WiFi", 200, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(controller.reportCompletion("CLI", 200, TransitionStatus::Completed, nullptr));

    TEST_ASSERT_FALSE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(controller.state()));
}

void test_required_failure_forces_error_state() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::LIVE,
                                                   SystemEvent::PLAY_REQUESTED,
                                                   SystemReason::USER_REQUEST,
                                                   300));

    TEST_ASSERT_TRUE(controller.reportCompletion("CLI", 300, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(controller.reportCompletion("WiFi", 300, TransitionStatus::Failed, "disconnected"));

    TEST_ASSERT_FALSE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.state()));
}

void test_optional_failure_still_commits_target_state() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::LIVE,
                                                   SystemEvent::PLAY_REQUESTED,
                                                   SystemReason::USER_REQUEST,
                                                   400));

    TEST_ASSERT_TRUE(controller.reportCompletion("CLI", 400, TransitionStatus::Failed, "busy"));
    TEST_ASSERT_TRUE(controller.reportCompletion("WiFi", 400, TransitionStatus::Completed, nullptr));

    TEST_ASSERT_FALSE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(controller.state()));
}

void test_stale_completion_is_ignored_during_orchestration() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::READY,
                                                   SystemEvent::RECOVER,
                                                   SystemReason::RECOVERY,
                                                   500));

    TEST_ASSERT_FALSE(controller.reportCompletion("WiFi", 499, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(controller.componentsWaitingForCompletion()));
    TEST_ASSERT_TRUE(controller.reportCompletion("WiFi", 500, TransitionStatus::Completed, nullptr));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_begin_orchestration_tracks_registered_components);
    RUN_TEST(test_all_successful_completions_commit_target_state);
    RUN_TEST(test_required_failure_forces_error_state);
    RUN_TEST(test_optional_failure_still_commits_target_state);
    RUN_TEST(test_stale_completion_is_ignored_during_orchestration);
    return UNITY_END();
}