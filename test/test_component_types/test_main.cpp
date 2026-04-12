#include <cstdint>
#include <cstring>
#include <unity.h>

#include "../../src/component_types.cpp"

namespace {

void test_transition_status_values_are_stable() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(TransitionStatus::Completed));
    TEST_ASSERT_EQUAL(1, static_cast<int>(TransitionStatus::Failed));
}

void test_component_lifecycle_status_values_are_stable() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(ComponentLifecycleStatus::Unknown));
    TEST_ASSERT_EQUAL(1, static_cast<int>(ComponentLifecycleStatus::Ready));
    TEST_ASSERT_EQUAL(2, static_cast<int>(ComponentLifecycleStatus::Failed));
    TEST_ASSERT_EQUAL(3, static_cast<int>(ComponentLifecycleStatus::Disabled));
}

void test_transition_status_to_string_all_values() {
    TEST_ASSERT_EQUAL_STRING("Completed", toString(TransitionStatus::Completed));
    TEST_ASSERT_EQUAL_STRING("Failed", toString(TransitionStatus::Failed));
}

void test_component_lifecycle_status_to_string_all_values() {
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(ComponentLifecycleStatus::Unknown));
    TEST_ASSERT_EQUAL_STRING("Ready", toString(ComponentLifecycleStatus::Ready));
    TEST_ASSERT_EQUAL_STRING("Failed", toString(ComponentLifecycleStatus::Failed));
    TEST_ASSERT_EQUAL_STRING("Disabled", toString(ComponentLifecycleStatus::Disabled));
}

void test_to_string_handles_invalid_values() {
    const auto invalidTransition = static_cast<TransitionStatus>(255);
    const auto invalidLifecycle = static_cast<ComponentLifecycleStatus>(255);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidTransition));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidLifecycle));
}

void test_debug_reason_alias_accepts_null_and_strings() {
    DebugReason nullReason = nullptr;
    DebugReason emptyReason = "";
    DebugReason customReason = "wifi_timeout_after_15s";
    DebugReason longReason =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    TEST_ASSERT_NULL(nullReason);
    TEST_ASSERT_EQUAL_STRING("", emptyReason);
    TEST_ASSERT_EQUAL_STRING("wifi_timeout_after_15s", customReason);
    TEST_ASSERT_EQUAL_UINT32(256, static_cast<uint32_t>(strlen(longReason)));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_transition_status_values_are_stable);
    RUN_TEST(test_component_lifecycle_status_values_are_stable);
    RUN_TEST(test_transition_status_to_string_all_values);
    RUN_TEST(test_component_lifecycle_status_to_string_all_values);
    RUN_TEST(test_to_string_handles_invalid_values);
    RUN_TEST(test_debug_reason_alias_accepts_null_and_strings);
    return UNITY_END();
}
