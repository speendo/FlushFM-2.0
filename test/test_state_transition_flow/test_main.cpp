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

    void reachSleep() {
        controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
        if (controller.isOrchestrationActive()) {
            completeAllActive();
        }
    }
};

// Helper: reach SLEEP from initial BOOTING (no orchestration needed, registeredCount==0)
void reachSleep(Supervisor& c) {
    c.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
}

void test_sleep_to_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
}

void test_sleep_to_live() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_ready_to_live() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_live_to_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}

void test_live_to_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_ready_to_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    // Go LIVE→READY, then READY→SLEEP
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_setup_failure_triggers_error() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_error_event_triggers_error() {
    TransitionHooksFixture fixture;
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    fixture.controller.setErrorEvent("wifi disconnected", ComponentID::WiFi);
    fixture.controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_error_event_ignores_duplicates() {
    TransitionHooksFixture fixture;
    fixture.controller.setErrorEvent("first error", ComponentID::WiFi);
    fixture.controller.setErrorEvent("second error", ComponentID::AudioRuntime);
    // Only first error stored; processMailbox transitions to ERROR
    fixture.controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_error_to_fatal() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(fixture.controller.state()));
}

void test_fatal_absorbing() {
    Supervisor controller;
    controller.triggerFatal();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));

    controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));
}

void test_play_requested_during_connecting_is_deferred_until_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    // STATE_REQUESTED(LIVE) again while CONNECTING — should defer (targetMode=LIVE)
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_play_requested_while_streaming_restarts_after_idle_transition() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    // STATE_REQUESTED(LIVE) in LIVE → replay: targetMode=LIVE, orchestrate to READY
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_optional_component_failure_does_not_block_orchestration() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
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
    fixture.reachSleep();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    // observedState_ is CONNECTING — READY is the target but state() shows CONNECTING
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
}

void test_matrix_forward_timeout() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    uint32_t wifiTimeout = fixture.controller.getPendingTimeout(ComponentID::WiFi);
    TEST_ASSERT_GREATER_THAN(0, wifiTimeout);
}

void test_state_requested_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}

void test_state_requested_live_from_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

void test_state_requested_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}

void test_state_requested_error() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::ERROR));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}

void test_state_requested_fatal() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::FATAL));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(fixture.controller.state()));
}

void test_state_requested_booting_ignored() {
    Supervisor controller;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING), static_cast<int>(controller.state()));
    controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::BOOTING);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING), static_cast<int>(controller.state()));
}

void test_state_requested_deferred_in_connecting() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sleep_to_ready);
    RUN_TEST(test_sleep_to_live);
    RUN_TEST(test_ready_to_live);
    RUN_TEST(test_live_to_ready);
    RUN_TEST(test_live_to_sleep);
    RUN_TEST(test_ready_to_sleep);
    RUN_TEST(test_setup_failure_triggers_error);
    RUN_TEST(test_error_event_triggers_error);
    RUN_TEST(test_error_event_ignores_duplicates);
    RUN_TEST(test_error_to_fatal);
    RUN_TEST(test_fatal_absorbing);
    RUN_TEST(test_play_requested_during_connecting_is_deferred_until_ready);
    RUN_TEST(test_play_requested_while_streaming_restarts_after_idle_transition);
    RUN_TEST(test_optional_component_failure_does_not_block_orchestration);
    RUN_TEST(test_observed_state_lags_until_orchestration_confirms);
    RUN_TEST(test_matrix_forward_timeout);
    RUN_TEST(test_state_requested_sleep);
    RUN_TEST(test_state_requested_live_from_sleep);
    RUN_TEST(test_state_requested_ready);
    RUN_TEST(test_state_requested_error);
    RUN_TEST(test_state_requested_fatal);
    RUN_TEST(test_state_requested_booting_ignored);
    RUN_TEST(test_state_requested_deferred_in_connecting);
    return UNITY_END();
}
