#include <array>

#include <unity.h>

#include "../../src/state_machine/supervisor.cpp"

namespace {

// Test matrix with known timeout values for verification
constexpr ComponentStateMatrix kTestMatrix[] = {
    {0, 0, 100, 100},
    {10, 10, 500, 500},
    {20, 50, 1000, 500},
    {30, 40, 2000, 500},
    {40, 50, 5000, 1000},
    {50, 0xFF, 3000, 500},
    {50, 0xFF, 4000, 1000},
};

struct TransitionHooksFixture {
    Supervisor controller;
    std::array<ComponentID, 4> components{ComponentID::WiFi, ComponentID::AudioRuntime, ComponentID::CLI, ComponentID::BoardInfo};

    void install() {
        TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::WiFi, true));
        TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::AudioRuntime, true));
        TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::CLI, false));
        TEST_ASSERT_TRUE(controller.registerComponent(ComponentID::BoardInfo, false));

        for (const ComponentID id : components) {
            TEST_ASSERT_TRUE(controller.setComponentTransitionHooks(
                id,
                [](SystemState, uint32_t) {
                    return 1000;
                },
                [&controller = controller, id](uint32_t transitionId) {
                    (void)controller.reportCompletion(id, transitionId, TransitionStatus::Failed, "timeout");
                },
                kTestMatrix,
                sizeof(kTestMatrix) / sizeof(kTestMatrix[0])));
        }
    }

    void completeAllActive(TransitionStatus status = TransitionStatus::Completed) {
        TEST_ASSERT_TRUE(controller.hasActiveTransition());
        const uint32_t transitionId = controller.activeTransitionId();
        for (const ComponentID id : components) {
            TEST_ASSERT_TRUE(controller.reportCompletion(id, transitionId, status, status == TransitionStatus::Completed ? nullptr : "failed"));
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

void test_enter_sleep_during_connecting_cancels_deferred_play() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST));
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

void test_enter_sleep_requires_orchestration_completion_before_sleep() {
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

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();

    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_play_requested_while_sleep_wakes_to_connecting_then_streaming() {
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
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    // Now test: PLAY_REQUESTED while SLEEP should transition SLEEP -> CONNECTING -> STREAMING
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
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
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_ready_wifi_disconnect_triggers_error() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_ready_setup_failure_triggers_error() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_live_wifi_disconnect_triggers_error() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_live_stream_lost_triggers_error() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STREAM_LOST, SystemReason::NONE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_sleep_wifi_disconnect_updates_registry() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown),
                      static_cast<int>(fixture.controller.getComponentStatus(ComponentID::WiFi)));
}

void test_sleep_audio_failed_updates_registry() {
    TransitionHooksFixture fixture;

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed),
                      static_cast<int>(fixture.controller.getComponentStatus(ComponentID::AudioRuntime)));
}

void test_optional_component_failure_does_not_block_orchestration() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    const uint32_t tid = fixture.controller.activeTransitionId();
    TEST_ASSERT_TRUE(fixture.controller.reportCompletion(ComponentID::CLI, tid, TransitionStatus::Failed, "test"));
    TEST_ASSERT_TRUE(fixture.controller.reportCompletion(ComponentID::WiFi, tid, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(fixture.controller.reportCompletion(ComponentID::AudioRuntime, tid, TransitionStatus::Completed, nullptr));
    TEST_ASSERT_TRUE(fixture.controller.reportCompletion(ComponentID::BoardInfo, tid, TransitionStatus::Completed, nullptr));

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed),
                      static_cast<int>(fixture.controller.getComponentStatus(ComponentID::CLI)));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
}

void test_observed_state_lags_until_orchestration_confirms() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));

    // PLAY_REQUESTED from SLEEP: direct transitionTo(CONNECTING), then quick-path orchestration to READY.
    // observedState_ is CONNECTING — READY is the orchestration target but state() doesn't show it yet.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
}

void test_fatal_rejects_all_events_except_boot() {
    Supervisor controller;
    controller.triggerFatal();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));
}

void test_matrix_forward_timeout_from_sleep_to_ready() {
    TransitionHooksFixture fixture;
    fixture.install();

    // PLAY_REQUESTED from SLEEP → direct transitionTo(CONNECTING) → quick-path orchestration to READY
    // The orchestration to READY is forward (CONNECTING→READY is stateRank 30→50)
    // WiFi matrix index for READY is 5, forwardTimeoutMs = 3000
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));

    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    uint32_t wifiTimeout = fixture.controller.getPendingTimeout(ComponentID::WiFi);
    TEST_ASSERT_GREATER_THAN(0, wifiTimeout);
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
    RUN_TEST(test_enter_sleep_during_connecting_cancels_deferred_play);
    RUN_TEST(test_connecting_transitions_to_error_on_init_failure_events);
    RUN_TEST(test_play_requested_requires_orchestration_completion_before_streaming);
    RUN_TEST(test_stop_requested_requires_orchestration_completion_before_idle);
    RUN_TEST(test_play_requested_while_streaming_restarts_after_idle_transition);
    RUN_TEST(test_enter_sleep_requires_orchestration_completion_before_sleep);
    RUN_TEST(test_play_requested_while_sleep_wakes_to_connecting_then_streaming);
    RUN_TEST(test_error_state_transitions);
    RUN_TEST(test_ready_wifi_disconnect_triggers_error);
    RUN_TEST(test_ready_setup_failure_triggers_error);
    RUN_TEST(test_live_wifi_disconnect_triggers_error);
    RUN_TEST(test_live_stream_lost_triggers_error);
    RUN_TEST(test_sleep_wifi_disconnect_updates_registry);
    RUN_TEST(test_sleep_audio_failed_updates_registry);
    RUN_TEST(test_optional_component_failure_does_not_block_orchestration);
    RUN_TEST(test_observed_state_lags_until_orchestration_confirms);
    RUN_TEST(test_fatal_rejects_all_events_except_boot);
    RUN_TEST(test_matrix_forward_timeout_from_sleep_to_ready);
    return UNITY_END();
}
