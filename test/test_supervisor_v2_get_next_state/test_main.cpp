#include <unity.h>

#include "../../src/state_machine/supervisor_v2.cpp"

void test_get_next_state_fatal_absorbent() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::SLEEP)));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::READY)));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::LIVE)));
}

void test_get_next_state_fatal_to_fatal() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::FATAL)));
}

void test_get_next_state_fatal_to_error() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::ERROR)));
}

void test_get_next_state_error_recovery_toward_live() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(getNextState(SystemState::ERROR, SystemState::LIVE)));
}

void test_get_next_state_error_to_sleep() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP),
                      static_cast<int>(getNextState(SystemState::ERROR, SystemState::SLEEP)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_next_state_fatal_absorbent);
    RUN_TEST(test_get_next_state_fatal_to_fatal);
    RUN_TEST(test_get_next_state_fatal_to_error);
    RUN_TEST(test_get_next_state_error_recovery_toward_live);
    RUN_TEST(test_get_next_state_error_to_sleep);
    return UNITY_END();
}
