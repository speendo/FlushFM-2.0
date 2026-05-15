#include <unity.h>

#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/state_machine.cpp"

void test_get_index_valid_states() {
    TEST_ASSERT_EQUAL_INT(0, getIndex(SystemState::FATAL));
    TEST_ASSERT_EQUAL_INT(1, getIndex(SystemState::ERROR));
    TEST_ASSERT_EQUAL_INT(2, getIndex(SystemState::SLEEP));
    TEST_ASSERT_EQUAL_INT(3, getIndex(SystemState::BOOTING));
    TEST_ASSERT_EQUAL_INT(4, getIndex(SystemState::CONNECTING));
    TEST_ASSERT_EQUAL_INT(5, getIndex(SystemState::READY));
    TEST_ASSERT_EQUAL_INT(6, getIndex(SystemState::LIVE));
}

void test_get_index_invalid_returns_negative_one() {
    // Any uint8_t value that doesn't match a defined state
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(1)));
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(42)));
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(100)));
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(255)));
}

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
    RUN_TEST(test_get_index_valid_states);
    RUN_TEST(test_get_index_invalid_returns_negative_one);
    RUN_TEST(test_get_next_state_fatal_absorbent);
    RUN_TEST(test_get_next_state_fatal_to_fatal);
    RUN_TEST(test_get_next_state_fatal_to_error);
    RUN_TEST(test_get_next_state_error_recovery_toward_live);
    RUN_TEST(test_get_next_state_error_to_sleep);
    return UNITY_END();
}
