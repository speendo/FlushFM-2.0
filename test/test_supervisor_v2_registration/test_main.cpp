#include <unity.h>

#include "support/supervisor_access.h"
#include "supervisor/supervisor.h"

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

void test_register_component_stores_pointer() {
    Supervisor supervisor;
    TestComponent comp;

    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    SupervisorAccess::nextState(supervisor).transitionTarget = SystemState::READY;

    SupervisorAccess::postNextComponentState(supervisor, ComponentID::WiFi);
    TEST_ASSERT_TRUE(comp.mailbox.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(comp.mailbox.targetState));
}

void test_post_next_component_state_null_guard() {
    Supervisor supervisor;
    SupervisorAccess::postNextComponentState(supervisor, ComponentID::AudioRuntime);
    TEST_ASSERT_NULL(SupervisorAccess::getComponentMailbox(supervisor, ComponentID::AudioRuntime));
}

void test_register_component_null_mailbox_is_safe() {
    Supervisor supervisor;
    supervisor.registerComponent(ComponentID::BoardInfo, nullptr, false);
    SupervisorAccess::postNextComponentState(supervisor, ComponentID::BoardInfo);
    TEST_ASSERT_NULL(SupervisorAccess::getComponentMailbox(supervisor, ComponentID::BoardInfo));
}

void test_complete_transition_completed_sets_event_bit() {
    Supervisor supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);
    supervisor.setup();

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Completed);

    EventBits_t bits = xEventGroupGetBits(SupervisorAccess::getEventGroup(supervisor));
    TEST_ASSERT_TRUE(bits & (1 << static_cast<int>(ComponentID::WiFi)));
}

void test_complete_transition_failed_required_posts_error() {
    Supervisor supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(SupervisorAccess::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(SupervisorAccess::errorEvent(supervisor).source));
}

void test_complete_transition_failed_optional_is_degraded() {
    Supervisor supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::CLI, &comp.mailbox, false);

    supervisor.completeTransition(ComponentID::CLI, TransitionStatus::Failed);

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(SupervisorAccess::getComponentStatus(supervisor, ComponentID::CLI)));
    TEST_ASSERT_FALSE(SupervisorAccess::errorEvent(supervisor).pending);
}

void test_boot_presence_passes_when_all_required_registered() {
    Supervisor supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    SupervisorAccess::checkComponentPresence(supervisor);

    TEST_ASSERT_FALSE(SupervisorAccess::errorEvent(supervisor).pending);
}

void test_boot_presence_detects_missing_required() {
    Supervisor supervisor;
    TestComponent board, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, nullptr, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    SupervisorAccess::checkComponentPresence(supervisor);

    TEST_ASSERT_TRUE(SupervisorAccess::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(SupervisorAccess::errorEvent(supervisor).source));
}

void test_boot_presence_ignores_missing_optional() {
    Supervisor supervisor;
    TestComponent board, wifi, audio;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);

    SupervisorAccess::checkComponentPresence(supervisor);

    TEST_ASSERT_FALSE(SupervisorAccess::errorEvent(supervisor).pending);
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
