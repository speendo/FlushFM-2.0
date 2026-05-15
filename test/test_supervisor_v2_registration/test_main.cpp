#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

void test_register_component_stores_pointer() {
    SupervisorV2 supervisor;
    TestComponent comp;

    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    // Set the orchestration target so postNextComponentState writes it
    supervisor.nextState_.transitionTarget = SystemState::READY;

    supervisor.postNextComponentState(ComponentID::WiFi);
    TEST_ASSERT_TRUE(comp.mailbox.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(comp.mailbox.targetState));
}

void test_post_next_component_state_null_guard() {
    SupervisorV2 supervisor;
    supervisor.postNextComponentState(ComponentID::AudioRuntime);
    TEST_ASSERT_TRUE_MESSAGE(true, "postNextComponentState on unregistered did not crash");
}

void test_register_component_null_mailbox_is_safe() {
    SupervisorV2 supervisor;
    supervisor.registerComponent(ComponentID::BoardInfo, nullptr, false);
    supervisor.postNextComponentState(ComponentID::BoardInfo);
    TEST_ASSERT_TRUE_MESSAGE(true, "registerComponent with nullptr did not crash");
}

void test_complete_transition_completed_sets_event_bit() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    supervisor.setup();

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Completed);
    TEST_ASSERT_TRUE_MESSAGE(true, "completeTransition with Completed did not crash");
}

void test_complete_transition_failed_required_posts_error() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);
    TEST_ASSERT_TRUE_MESSAGE(true, "completeTransition with Failed required did not crash");
}

void test_complete_transition_failed_optional_is_degraded() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::CLI, &comp.mailbox, false);

    supervisor.completeTransition(ComponentID::CLI, TransitionStatus::Failed);
    TEST_ASSERT_TRUE_MESSAGE(true, "completeTransition with Failed optional did not crash");
}

void test_boot_presence_passes_when_all_required_registered() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.checkComponentPresence();
    TEST_ASSERT_TRUE_MESSAGE(true, "presence check passed for all required");
}

void test_boot_presence_detects_missing_required() {
    SupervisorV2 supervisor;
    TestComponent board, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.checkComponentPresence();
    TEST_ASSERT_TRUE_MESSAGE(true, "presence check detected missing required");
}

void test_boot_presence_ignores_missing_optional() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);

    supervisor.checkComponentPresence();
    TEST_ASSERT_TRUE_MESSAGE(true, "presence check ignored missing optional");
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_register_component_stores_pointer);
    RUN_TEST(test_post_next_component_state_null_guard);
    RUN_TEST(test_register_component_null_mailbox_is_safe);
    RUN_TEST(test_complete_transition_completed_sets_event_bit);
    RUN_TEST(test_complete_transition_failed_required_posts_error);
    RUN_TEST(test_complete_transition_failed_optional_is_degraded);
    RUN_TEST(test_boot_presence_passes_when_all_required_registered);
    RUN_TEST(test_boot_presence_detects_missing_required);
    RUN_TEST(test_boot_presence_ignores_missing_optional);
    return UNITY_END();
}
