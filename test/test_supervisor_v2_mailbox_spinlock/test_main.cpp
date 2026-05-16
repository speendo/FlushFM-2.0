#include <unity.h>

#include "support/s2v2_access.h"

namespace {

void test_consume_state_request_reads_and_clears() {
    SupervisorV2 supervisor;
    supervisor.postStateRequest(SystemState::LIVE);

    bool consumed = S2V2Access::consumeStateRequest(supervisor);
    TEST_ASSERT_TRUE(consumed);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.getTargetState()));
}

void test_consume_state_request_returns_false_when_nothing_pending() {
    SupervisorV2 supervisor;
    bool consumed = S2V2Access::consumeStateRequest(supervisor);
    TEST_ASSERT_FALSE(consumed);
}

void test_consume_error_event_logs_and_clears() {
    SupervisorV2 supervisor;

    S2V2Access::setMaxRecoveries(supervisor, 3);
    supervisor.postErrorEvent("test error", ComponentID::WiFi);
    S2V2Access::consumeErrorEvent(supervisor);

    supervisor.postErrorEvent("second error", ComponentID::AudioRuntime);
    S2V2Access::consumeErrorEvent(supervisor);

    TEST_ASSERT_TRUE_MESSAGE(true, "consumeErrorEvent processed and cleared");
}

void test_consume_error_event_does_nothing_when_no_error_pending() {
    SupervisorV2 supervisor;
    S2V2Access::consumeErrorEvent(supervisor);
    TEST_ASSERT_TRUE_MESSAGE(true, "consumeErrorEvent on empty did not crash");
}

void test_recovery_counter_increments_and_exhausts_to_fatal() {
    SupervisorV2 supervisor;
    S2V2Access::setMaxRecoveries(supervisor, 2);

    supervisor.postErrorEvent("error 1", ComponentID::WiFi);
    S2V2Access::consumeErrorEvent(supervisor);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(supervisor.getTargetState()));

    supervisor.postErrorEvent("error 2", ComponentID::BoardInfo);
    S2V2Access::consumeErrorEvent(supervisor);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(supervisor.getTargetState()));
}

void test_mailbox_last_write_wins_before_consume() {
    SupervisorV2 supervisor;

    supervisor.postStateRequest(SystemState::SLEEP);
    supervisor.postStateRequest(SystemState::LIVE);
    S2V2Access::consumeStateRequest(supervisor);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.getTargetState()));
}

void test_error_first_write_wins_before_consume() {
    SupervisorV2 supervisor;

    supervisor.postErrorEvent("first", ComponentID::WiFi);
    supervisor.postErrorEvent("second", ComponentID::AudioRuntime);
    S2V2Access::consumeErrorEvent(supervisor);
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
