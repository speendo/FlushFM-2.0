#include <chrono>
#include <thread>

#include <unity.h>

#include "../../src/state_machine/system_controller.cpp"

namespace {

void test_timeout_hook_is_invoked_when_component_does_not_complete() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.setComponentTransitionHooks(
        "WiFi",
        [](SystemState, uint32_t) { return 20; },
        [&controller](uint32_t transitionId) {
            (void)controller.reportCompletion("WiFi", transitionId, TransitionStatus::Failed, "timeout");
        }));

    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::STREAMING,
                                                   SystemEvent::PLAY_REQUESTED,
                                                   SystemReason::USER_REQUEST,
                                                   900));
    TEST_ASSERT_TRUE(controller.isOrchestrationActive());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    controller.dispatchPending();

    TEST_ASSERT_FALSE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.state()));
}

void test_zero_timeout_is_treated_as_immediate_timeout() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("AudioRuntime", true));
    TEST_ASSERT_TRUE(controller.setComponentTransitionHooks(
        "AudioRuntime",
        [](SystemState, uint32_t) { return 0; },
        [&controller](uint32_t transitionId) {
            (void)controller.reportCompletion("AudioRuntime", transitionId, TransitionStatus::Failed, "timeout");
        }));

    TEST_ASSERT_TRUE(controller.beginOrchestration(SystemState::STREAMING,
                                                   SystemEvent::PLAY_REQUESTED,
                                                   SystemReason::USER_REQUEST,
                                                   901));
    controller.dispatchPending();

    TEST_ASSERT_FALSE(controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.state()));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_timeout_hook_is_invoked_when_component_does_not_complete);
    RUN_TEST(test_zero_timeout_is_treated_as_immediate_timeout);
    return UNITY_END();
}