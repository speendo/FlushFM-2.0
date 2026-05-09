#include <unity.h>

#include "../../src/state_machine/supervisor.cpp"

namespace {

void test_last_event_overwrites_prior_events() {
    Supervisor controller;
    controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
    controller.postEventBuffered(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP);
    controller.postEventBuffered(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
    controller.processMailbox();
    // STATE_REQUESTED(SLEEP) (the last buffered event) was processed: SLEEP→SLEEP is ignored.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));
}

void test_mailbox_slot_is_cleared_after_process() {
    Supervisor controller;
    controller.postEventBuffered(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
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
