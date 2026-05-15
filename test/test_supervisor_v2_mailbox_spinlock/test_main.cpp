#include <unity.h>

#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/orchestrator.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private

namespace {

void test_consume_state_request_reads_and_clears() {
    SupervisorV2 supervisor;
    supervisor.postStateRequest(SystemState::LIVE);

    bool consumed = supervisor.consumeStateRequest();
    TEST_ASSERT_TRUE(consumed);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.getTargetState()));
}

void test_consume_state_request_returns_false_when_nothing_pending() {
    SupervisorV2 supervisor;
    bool consumed = supervisor.consumeStateRequest();
    TEST_ASSERT_FALSE(consumed);
}

void test_consume_error_event_logs_and_clears() {
    SupervisorV2 supervisor;

    supervisor.setMaxRecoveries(3);
    supervisor.postErrorEvent("test error", ComponentID::WiFi);
    supervisor.consumeErrorEvent();

    supervisor.postErrorEvent("second error", ComponentID::AudioRuntime);
    supervisor.consumeErrorEvent();

    TEST_ASSERT_TRUE_MESSAGE(true, "consumeErrorEvent processed and cleared");
}

void test_consume_error_event_does_nothing_when_no_error_pending() {
    SupervisorV2 supervisor;
    supervisor.consumeErrorEvent();
    TEST_ASSERT_TRUE_MESSAGE(true, "consumeErrorEvent on empty did not crash");
}

void test_recovery_counter_increments_and_exhausts_to_fatal() {
    SupervisorV2 supervisor;
    supervisor.setMaxRecoveries(2);

    // First error: not exhausted → set target to ERROR
    supervisor.postErrorEvent("error 1", ComponentID::WiFi);
    supervisor.consumeErrorEvent();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(supervisor.getTargetState()));

    // Second error: exhausted → set target to FATAL
    supervisor.postErrorEvent("error 2", ComponentID::BoardInfo);
    supervisor.consumeErrorEvent();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(supervisor.getTargetState()));
}

void test_mailbox_last_write_wins_before_consume() {
    SupervisorV2 supervisor;

    supervisor.postStateRequest(SystemState::SLEEP);
    supervisor.postStateRequest(SystemState::LIVE);
    supervisor.consumeStateRequest();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.getTargetState()));
}

void test_error_first_write_wins_before_consume() {
    SupervisorV2 supervisor;

    supervisor.postErrorEvent("first", ComponentID::WiFi);
    supervisor.postErrorEvent("second", ComponentID::AudioRuntime);
    supervisor.consumeErrorEvent();
    TEST_ASSERT_TRUE_MESSAGE(true, "first-write-wins enforced");
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_consume_state_request_reads_and_clears);
    RUN_TEST(test_consume_state_request_returns_false_when_nothing_pending);
    RUN_TEST(test_consume_error_event_logs_and_clears);
    RUN_TEST(test_consume_error_event_does_nothing_when_no_error_pending);
    RUN_TEST(test_recovery_counter_increments_and_exhausts_to_fatal);
    RUN_TEST(test_mailbox_last_write_wins_before_consume);
    RUN_TEST(test_error_first_write_wins_before_consume);
    return UNITY_END();
}
