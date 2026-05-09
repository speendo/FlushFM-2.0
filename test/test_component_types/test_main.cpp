#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unity.h>

#include "../../src/component_types.cpp"
#include "../../src/state_machine/supervisor.h"

namespace {

template <typename Enum>
struct EnumLabelCase {
    Enum value;
    const char* label;
};

template <typename Enum, size_t N>
void assert_round_trip_and_uniqueness(const std::array<EnumLabelCase<Enum>, N>& cases) {
    for (size_t i = 0; i < N; ++i) {
        const char* label = toString(cases[i].value);
        TEST_ASSERT_NOT_NULL(label);
        TEST_ASSERT_EQUAL_STRING(cases[i].label, label);

        bool found = false;
        for (size_t j = 0; j < N; ++j) {
            if (std::strcmp(cases[j].label, label) == 0) {
                TEST_ASSERT_EQUAL(static_cast<int>(cases[i].value), static_cast<int>(cases[j].value));
                found = true;
                break;
            }
        }

        TEST_ASSERT_TRUE(found);
    }

    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            TEST_ASSERT_FALSE(std::strcmp(cases[i].label, cases[j].label) == 0);
        }
    }
}

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

void test_state_machine_labels_round_trip_and_are_unique() {
    const std::array<EnumLabelCase<SystemState>, 6> states = {{
        {SystemState::ERROR, "ERROR"},
        {SystemState::BOOTING, "BOOTING"},
        {SystemState::SLEEP, "SLEEP"},
        {SystemState::CONNECTING, "CONNECTING"},
        {SystemState::READY, "READY"},
        {SystemState::LIVE, "LIVE"},
    }};

    const std::array<EnumLabelCase<SystemEvent>, 11> events = {{
        {SystemEvent::BOOT, "BOOT"},
        {SystemEvent::COMPONENT_SETUP_FAILED, "COMPONENT_SETUP_FAILED"},
        {SystemEvent::WIFI_READY, "WIFI_READY"},
        {SystemEvent::AUDIO_INIT_OK, "AUDIO_INIT_OK"},
        {SystemEvent::AUDIO_INIT_FAILED, "AUDIO_INIT_FAILED"},
        {SystemEvent::PLAY_REQUESTED, "PLAY_REQUESTED"},
        {SystemEvent::STOP_REQUESTED, "STOP_REQUESTED"},
        {SystemEvent::WIFI_DISCONNECTED, "WIFI_DISCONNECTED"},
        {SystemEvent::STREAM_LOST, "STREAM_LOST"},
        {SystemEvent::RECOVER, "RECOVER"},
        {SystemEvent::ENTER_SLEEP, "ENTER_SLEEP"},
    }};

    const std::array<EnumLabelCase<EventPolicy>, 2> policies = {{
        {EventPolicy::BestEffort, "BestEffort"},
        {EventPolicy::Critical, "Critical"},
    }};

    const std::array<EnumLabelCase<SystemReason>, 9> reasons = {{
        {SystemReason::NONE, "NONE"},
        {SystemReason::BOOT_SEQUENCE, "BOOT_SEQUENCE"},
        {SystemReason::COMPONENT_SETUP, "COMPONENT_SETUP"},
        {SystemReason::WIFI_INITIALIZED, "WIFI_INITIALIZED"},
        {SystemReason::AUDIO_TASK_STARTED, "AUDIO_TASK_STARTED"},
        {SystemReason::AUDIO_TASK_INIT_FAILED, "AUDIO_TASK_INIT_FAILED"},
        {SystemReason::USER_REQUEST, "USER_REQUEST"},
        {SystemReason::RECOVERY, "RECOVERY"},
        {SystemReason::POWER_POLICY, "POWER_POLICY"},
    }};

    const std::array<EnumLabelCase<TransitionRequestDecision>, 4> decisions = {{
        {TransitionRequestDecision::Ignored, "Ignored"},
        {TransitionRequestDecision::Started, "Started"},
        {TransitionRequestDecision::Superseded, "Superseded"},
        {TransitionRequestDecision::Queued, "Queued"},
    }};

    assert_round_trip_and_uniqueness(states);
    assert_round_trip_and_uniqueness(events);
    assert_round_trip_and_uniqueness(policies);
    assert_round_trip_and_uniqueness(reasons);
    assert_round_trip_and_uniqueness(decisions);
}

void test_state_machine_invalid_values_map_to_unknown() {
    const auto invalidState = static_cast<SystemState>(255);
    const auto invalidEvent = static_cast<SystemEvent>(255);
    const auto invalidPolicy = static_cast<EventPolicy>(255);
    const auto invalidReason = static_cast<SystemReason>(255);
    const auto invalidDecision = static_cast<TransitionRequestDecision>(255);

    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidState));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidEvent));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidPolicy));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidReason));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidDecision));
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
    RUN_TEST(test_state_machine_labels_round_trip_and_are_unique);
    RUN_TEST(test_state_machine_invalid_values_map_to_unknown);
    RUN_TEST(test_to_string_handles_invalid_values);
    RUN_TEST(test_debug_reason_alias_accepts_null_and_strings);
    return UNITY_END();
}
