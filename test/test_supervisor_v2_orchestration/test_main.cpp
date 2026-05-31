#include <unity.h>

#include "support/s2v2_access.h"

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

void test_start_orchestration_sets_active_flag() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::nextState(supervisor).transitionTarget));
}

void test_start_orchestration_writes_all_component_mailboxes() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_TRUE(board.mailbox.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(board.mailbox.targetState));
    TEST_ASSERT_TRUE(wifi.mailbox.pending);
    TEST_ASSERT_TRUE(audio.mailbox.pending);
    TEST_ASSERT_TRUE(cli.mailbox.pending);
}

void test_start_orchestration_posts_order_with_correct_bits() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_TRUE(S2V2Access::getOrderPending(supervisor));
    int boardBit = 1 << static_cast<int>(ComponentID::BoardInfo);
    int wifiBit  = 1 << static_cast<int>(ComponentID::WiFi);
    int audioBit = 1 << static_cast<int>(ComponentID::AudioRuntime);
    int cliBit   = 1 << static_cast<int>(ComponentID::CLI);
    TEST_ASSERT_EQUAL(boardBit | wifiBit | audioBit | cliBit,
                      S2V2Access::getOrderExpectedBits(supervisor));
}

void test_start_orchestration_excludes_degraded_from_order() {
    SupervisorV2 supervisor;
    TestComponent board, wifi;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    S2V2Access::setComponentStatus(supervisor, ComponentID::WiFi, ComponentStatus::DEGRADED);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    EventBits_t expected = 1 << static_cast<int>(ComponentID::BoardInfo);
    TEST_ASSERT_EQUAL(expected, S2V2Access::getOrderExpectedBits(supervisor));
}

void test_start_orchestration_clears_event_group_bits() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.setup();

    xEventGroupSetBits(S2V2Access::getEventGroup(supervisor),
                       1 << static_cast<int>(ComponentID::WiFi));
    TEST_ASSERT_NOT_EQUAL(0, xEventGroupGetBits(S2V2Access::getEventGroup(supervisor)));

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_EQUAL(0, xEventGroupGetBits(S2V2Access::getEventGroup(supervisor)));
}

void test_start_orchestration_sets_deadline_in_order() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.setup();

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);

    TEST_ASSERT_NOT_EQUAL(0, S2V2Access::getOrderTimeout(supervisor));
}

void test_complete_transition_optional_failed_sets_event_bit() {
    SupervisorV2 supervisor;
    TestComponent cli;
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    supervisor.completeTransition(ComponentID::CLI, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(xEventGroupGetBits(S2V2Access::getEventGroup(supervisor))
                     & (1 << static_cast<int>(ComponentID::CLI)));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::CLI)));
}

void test_check_response_completed_advances_observed_state() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::startOrchestration(supervisor, SystemState::CONNECTING);
    S2V2Access::setResponsePending(supervisor, false);

    S2V2Access::postResponse(supervisor, OrchestrationResult::COMPLETED, 0);

    S2V2Access::checkOrchestrationResponse(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_check_response_timed_out_required_posts_error() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    S2V2Access::setMaxRecoveries(supervisor, 3);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::postResponse(supervisor, OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::WiFi));

    S2V2Access::checkOrchestrationResponse(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::WiFi)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_check_response_timed_out_optional_is_degraded() {
    SupervisorV2 supervisor;
    TestComponent cli;
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::postResponse(supervisor, OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::CLI));

    S2V2Access::checkOrchestrationResponse(supervisor);

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::CLI)));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
}

void test_check_response_returns_when_no_pending() {
    SupervisorV2 supervisor;

    S2V2Access::setHasActiveOrchestration(supervisor, true);
    S2V2Access::setResponsePending(supervisor, false);

    S2V2Access::checkOrchestrationResponse(supervisor);

    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
}

// Complete the in-flight orchestration by setting all registered component bits
// and posting a COMPLETED response — simulates the orchestration worker.
static void completeInFlightOrchestration(SupervisorV2& supervisor) {
    EventGroupHandle_t eg = S2V2Access::getEventGroup(supervisor);
    EventBits_t allBits = (1 << static_cast<int>(ComponentID::BoardInfo))
                        | (1 << static_cast<int>(ComponentID::WiFi))
                        | (1 << static_cast<int>(ComponentID::AudioRuntime))
                        | (1 << static_cast<int>(ComponentID::CLI));
    xEventGroupSetBits(eg, allBits);
    S2V2Access::postResponse(supervisor, OrchestrationResult::COMPLETED, 0);
}

void test_multi_step_transition_sleep_to_live_completes_autonomously() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    // Path: SLEEP(20) → BOOTING(30) → CONNECTING(40) → READY(50) → LIVE(60)
    S2V2Access::setObservedState(supervisor, SystemState::SLEEP);
    S2V2Access::setTargetState(supervisor, SystemState::LIVE);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    // Run 1: SLEEP → start BOOTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 2: BOOTING complete → advance to BOOTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 3: BOOTING → start CONNECTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 4: CONNECTING complete → advance to CONNECTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 5: CONNECTING → start READY
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 6: READY complete → advance to READY, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::getObservedState(supervisor));

    // Run 7: READY → start LIVE
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::LIVE,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 8: LIVE complete → at target (no self-wake needed)
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::LIVE,
                      S2V2Access::getObservedState(supervisor));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::LIVE,
                      S2V2Access::getTargetState(supervisor));
}

void test_multi_step_transition_sleep_to_ready_completes_autonomously() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    // Path: SLEEP(20) → BOOTING(30) → CONNECTING(40) → READY(50)
    S2V2Access::setObservedState(supervisor, SystemState::SLEEP);
    S2V2Access::setTargetState(supervisor, SystemState::READY);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    // Run 1: SLEEP → start BOOTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 2: BOOTING complete → advance to BOOTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 3: BOOTING → start CONNECTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 4: CONNECTING complete → advance to CONNECTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 5: CONNECTING → start READY
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 6: READY complete → at target (no self-wake needed)
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::getObservedState(supervisor));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::getTargetState(supervisor));
}

void test_multi_step_transition_live_to_sleep_completes_autonomously() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    // Path: LIVE(60) → READY(50) → CONNECTING(40) → BOOTING(30) → SLEEP(20)
    S2V2Access::setObservedState(supervisor, SystemState::LIVE);
    S2V2Access::setTargetState(supervisor, SystemState::SLEEP);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    // Run 1: LIVE → start READY
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 2: READY complete → advance to READY, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::getObservedState(supervisor));

    // Run 3: READY → start CONNECTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 4: CONNECTING complete → advance to CONNECTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 5: CONNECTING → start BOOTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 6: BOOTING complete → advance to BOOTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 7: BOOTING → start SLEEP
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 8: SLEEP complete → at target
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getObservedState(supervisor));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getTargetState(supervisor));
}

void test_multi_step_transition_ready_to_sleep_completes_autonomously() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    // Path: READY(50) → CONNECTING(40) → BOOTING(30) → SLEEP(20)
    S2V2Access::setObservedState(supervisor, SystemState::READY);
    S2V2Access::setTargetState(supervisor, SystemState::SLEEP);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    // Run 1: READY → start CONNECTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 2: CONNECTING complete → advance to CONNECTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 3: CONNECTING → start BOOTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 4: BOOTING complete → advance to BOOTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 5: BOOTING → start SLEEP
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));

    completeInFlightOrchestration(supervisor);

    // Run 6: SLEEP complete → at target
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getObservedState(supervisor));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getTargetState(supervisor));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_start_orchestration_sets_active_flag);
    RUN_TEST(test_start_orchestration_writes_all_component_mailboxes);
    RUN_TEST(test_start_orchestration_posts_order_with_correct_bits);
    RUN_TEST(test_start_orchestration_excludes_degraded_from_order);
    RUN_TEST(test_start_orchestration_clears_event_group_bits);
    RUN_TEST(test_start_orchestration_sets_deadline_in_order);
    RUN_TEST(test_complete_transition_optional_failed_sets_event_bit);
    RUN_TEST(test_check_response_completed_advances_observed_state);
    RUN_TEST(test_check_response_timed_out_required_posts_error);
    RUN_TEST(test_check_response_timed_out_optional_is_degraded);
    RUN_TEST(test_check_response_returns_when_no_pending);
    RUN_TEST(test_multi_step_transition_sleep_to_live_completes_autonomously);
    RUN_TEST(test_multi_step_transition_sleep_to_ready_completes_autonomously);
    RUN_TEST(test_multi_step_transition_live_to_sleep_completes_autonomously);
    RUN_TEST(test_multi_step_transition_ready_to_sleep_completes_autonomously);
    return UNITY_END();
}
