#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unity.h>

#include "../../src/component_types.h"

namespace {

void test_transition_status_values_are_stable() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(TransitionStatus::Completed));
    TEST_ASSERT_EQUAL(1, static_cast<int>(TransitionStatus::Failed));
}

void test_transition_status_to_string_all_values() {
    TEST_ASSERT_EQUAL_STRING("Completed", toString(TransitionStatus::Completed));
    TEST_ASSERT_EQUAL_STRING("Failed", toString(TransitionStatus::Failed));
}

void test_state_machine_labels_round_trip() {
    TEST_ASSERT_EQUAL_STRING("FATAL", stateToString(SystemState::FATAL));
    TEST_ASSERT_EQUAL_STRING("ERROR", stateToString(SystemState::ERROR));
    TEST_ASSERT_EQUAL_STRING("SLEEP", stateToString(SystemState::SLEEP));
    TEST_ASSERT_EQUAL_STRING("BOOTING", stateToString(SystemState::BOOTING));
    TEST_ASSERT_EQUAL_STRING("CONNECTING", stateToString(SystemState::CONNECTING));
    TEST_ASSERT_EQUAL_STRING("READY", stateToString(SystemState::READY));
    TEST_ASSERT_EQUAL_STRING("LIVE", stateToString(SystemState::LIVE));
}

void test_state_machine_invalid_values_map_to_unknown() {
    const auto invalidState = static_cast<SystemState>(255);
    const auto invalidComponent = static_cast<ComponentID>(255);

    TEST_ASSERT_EQUAL_STRING("UNKNOWN", stateToString(invalidState));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", componentName(invalidComponent));
}

void test_component_id_names_match_expected() {
    TEST_ASSERT_EQUAL_STRING("BoardInfo", componentName(ComponentID::BoardInfo));
    TEST_ASSERT_EQUAL_STRING("WiFi", componentName(ComponentID::WiFi));
    TEST_ASSERT_EQUAL_STRING("AudioRuntime", componentName(ComponentID::AudioRuntime));
    TEST_ASSERT_EQUAL_STRING("CLI", componentName(ComponentID::CLI));
}

void test_debug_reason_alias_accepts_null_and_strings() {
    DebugReason nullReason = nullptr;
    DebugReason emptyReason = "";
    DebugReason customReason = "wifi_timeout_after_15s";

    TEST_ASSERT_NULL(nullReason);
    TEST_ASSERT_EQUAL_STRING("", emptyReason);
    TEST_ASSERT_EQUAL_STRING("wifi_timeout_after_15s", customReason);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_transition_status_values_are_stable);
    RUN_TEST(test_transition_status_to_string_all_values);
    RUN_TEST(test_state_machine_labels_round_trip);
    RUN_TEST(test_state_machine_invalid_values_map_to_unknown);
    RUN_TEST(test_component_id_names_match_expected);
    RUN_TEST(test_debug_reason_alias_accepts_null_and_strings);
    return UNITY_END();
}
