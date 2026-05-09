#include <unity.h>

#include "../../src/state_machine/supervisor.cpp"

namespace {

void test_last_event_overwrites_prior_events() {
    Supervisor controller;
    controller.postEvent(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST);
    controller.postEventBuffered(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP);
    controller.postEventBuffered(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST);
    controller.processMailbox();
    // If COMPONENT_SETUP_FAILED were processed, state would be ERROR.
    // ENTER_SLEEP (the last buffered event) was processed: SLEEP→SLEEP is ignored.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));
}

void test_mailbox_slot_is_cleared_after_process() {
    Supervisor controller;
    controller.postEventBuffered(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST);
    controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));
    controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_last_event_overwrites_prior_events);
    RUN_TEST(test_mailbox_slot_is_cleared_after_process);
    return UNITY_END();
}
