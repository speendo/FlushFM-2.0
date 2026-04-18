#include <array>
#include <string>

#include <unity.h>

#include "../../src/state_machine/system_controller.cpp"

namespace {

struct TransitionHooksFixture {
    SystemController controller;
    std::array<const char*, 4> components{"WiFi", "AudioRuntime", "CLI", "BoardInfo"};

    void install() {
        TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
        TEST_ASSERT_TRUE(controller.registerComponent("AudioRuntime", true));
        TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
        TEST_ASSERT_TRUE(controller.registerComponent("BoardInfo", false));

        for (const char* name : components) {
            TEST_ASSERT_TRUE(controller.setComponentTransitionHooks(
                name,
                [](SystemState, uint32_t) {
                    return 1000;
                },
                [&controller = controller, name](uint32_t transitionId) {
                    (void)controller.reportCompletion(name, transitionId, TransitionStatus::Failed, "timeout");
                }));
        }
    }

    void completeAllActive(TransitionStatus status = TransitionStatus::Completed) {
        TEST_ASSERT_TRUE(controller.hasActiveTransition());
        const uint32_t transitionId = controller.activeTransitionId();
        for (const char* name : components) {
            TEST_ASSERT_TRUE(controller.reportCompletion(name, transitionId, status, status == TransitionStatus::Completed ? nullptr : "failed"));
        }
    }
};

void test_boot_transitions_booting_to_sleep() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_connecting_waits_for_both_ready_signals_audio_first() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}

void test_connecting_waits_for_both_ready_signals_wifi_first() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}

void test_play_requested_during_connecting_is_deferred_until_ready() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_play_requested_uses_readiness_signals_received_while_sleeping() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    // Signals can arrive before autoplay/user play request while system is idle.
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_stop_requested_during_connecting_cancels_deferred_play() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}

void test_enter_off_during_connecting_cancels_deferred_play() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_OFF, SystemReason::USER_REQUEST));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_connecting_transitions_to_error_on_init_failure_events() {
    TransitionHooksFixture fixtureA;
    TEST_ASSERT_TRUE(fixtureA.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixtureA.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixtureA.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixtureA.controller.state()));

    TransitionHooksFixture fixtureB;
    TEST_ASSERT_TRUE(fixtureB.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixtureB.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixtureB.controller.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixtureB.controller.state()));
}

void test_play_requested_requires_orchestration_completion_before_streaming() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_stop_requested_requires_orchestration_completion_before_idle() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}

void test_play_requested_while_streaming_restarts_after_idle_transition() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_enter_off_requires_orchestration_completion_before_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_OFF, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_play_requested_while_sleep_transitions_directly_to_streaming() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    // Transition to SLEEP
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_OFF, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    // Now test: PLAY_REQUESTED while SLEEP should transition SLEEP -> STREAMING
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_error_state_transitions() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::RECOVER, SystemReason::RECOVERY));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_OFF, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_transitions_booting_to_sleep);
    RUN_TEST(test_connecting_waits_for_both_ready_signals_audio_first);
    RUN_TEST(test_connecting_waits_for_both_ready_signals_wifi_first);
    RUN_TEST(test_play_requested_during_connecting_is_deferred_until_ready);
    RUN_TEST(test_play_requested_uses_readiness_signals_received_while_sleeping);
    RUN_TEST(test_stop_requested_during_connecting_cancels_deferred_play);
    RUN_TEST(test_enter_off_during_connecting_cancels_deferred_play);
    RUN_TEST(test_connecting_transitions_to_error_on_init_failure_events);
    RUN_TEST(test_play_requested_requires_orchestration_completion_before_streaming);
    RUN_TEST(test_stop_requested_requires_orchestration_completion_before_idle);
    RUN_TEST(test_play_requested_while_streaming_restarts_after_idle_transition);
    RUN_TEST(test_enter_off_requires_orchestration_completion_before_sleep);
    RUN_TEST(test_play_requested_while_sleep_transitions_directly_to_streaming);
    RUN_TEST(test_error_state_transitions);
    return UNITY_END();
}
