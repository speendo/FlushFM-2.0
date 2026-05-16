#include <unity.h>

#include "support/s2v2_access.h"
#include "supervisor/supervisor_v2.h"

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

void test_register_component_stores_pointer() {
    SupervisorV2 supervisor;
    TestComponent comp;

    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    S2V2Access::nextState(supervisor).transitionTarget = SystemState::READY;

    S2V2Access::postNextComponentState(supervisor, ComponentID::WiFi);
    TEST_ASSERT_TRUE(comp.mailbox.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(comp.mailbox.targetState));
}

void test_post_next_component_state_null_guard() {
    SupervisorV2 supervisor;
    S2V2Access::postNextComponentState(supervisor, ComponentID::AudioRuntime);
    TEST_ASSERT_NULL(S2V2Access::getComponentMailbox(supervisor, ComponentID::AudioRuntime));
}

void test_register_component_null_mailbox_is_safe() {
    SupervisorV2 supervisor;
    supervisor.registerComponent(ComponentID::BoardInfo, nullptr, false);
    S2V2Access::postNextComponentState(supervisor, ComponentID::BoardInfo);
    TEST_ASSERT_NULL(S2V2Access::getComponentMailbox(supervisor, ComponentID::BoardInfo));
}

void test_complete_transition_completed_sets_event_bit() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);
    supervisor.setup();

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Completed);

    EventBits_t bits = xEventGroupGetBits(S2V2Access::getEventGroup(supervisor));
    TEST_ASSERT_TRUE(bits & (1 << static_cast<int>(ComponentID::WiFi)));
}

void test_complete_transition_failed_required_posts_error() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(S2V2Access::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(S2V2Access::errorEvent(supervisor).source));
}

void test_complete_transition_failed_optional_is_degraded() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::CLI, &comp.mailbox, false);

    supervisor.completeTransition(ComponentID::CLI, TransitionStatus::Failed);

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::CLI)));
    TEST_ASSERT_FALSE(S2V2Access::errorEvent(supervisor).pending);
}

void test_boot_presence_passes_when_all_required_registered() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    S2V2Access::checkComponentPresence(supervisor);

    TEST_ASSERT_FALSE(S2V2Access::errorEvent(supervisor).pending);
}

void test_boot_presence_detects_missing_required() {
    SupervisorV2 supervisor;
    TestComponent board, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, nullptr, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    S2V2Access::checkComponentPresence(supervisor);

    TEST_ASSERT_TRUE(S2V2Access::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(S2V2Access::errorEvent(supervisor).source));
}

void test_boot_presence_ignores_missing_optional() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);

    S2V2Access::checkComponentPresence(supervisor);

    TEST_ASSERT_FALSE(S2V2Access::errorEvent(supervisor).pending);
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
