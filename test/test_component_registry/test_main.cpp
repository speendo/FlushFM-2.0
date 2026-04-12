#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include <unity.h>

#include "../../src/state_machine/system_controller.cpp"

namespace {

static_assert(std::is_trivially_copyable<ComponentRegistryEntry>::value,
              "ComponentRegistryEntry must stay trivially copyable");

static_assert(std::is_same<decltype(&SystemController::registerComponent), bool (SystemController::*)(const char*, bool)>::value,
              "SystemController::registerComponent signature mismatch");
static_assert(std::is_same<decltype(&SystemController::getComponentStatus), ComponentLifecycleStatus (SystemController::*)(const char*) const>::value,
              "SystemController::getComponentStatus signature mismatch");
static_assert(std::is_same<decltype(&SystemController::markComponentFailed), bool (SystemController::*)(const char*, const char*)>::value,
              "SystemController::markComponentFailed signature mismatch");
static_assert(std::is_same<decltype(&SystemController::isComponentRequired), bool (SystemController::*)(const char*) const>::value,
              "SystemController::isComponentRequired signature mismatch");

void test_register_component_stores_entry_and_returns_success() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.isComponentRequired("WiFi"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus("WiFi")));
}

void test_mark_component_failed_updates_state() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("AudioRuntime", true));
    TEST_ASSERT_TRUE(controller.markComponentFailed("AudioRuntime", "init_failed"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed), static_cast<int>(controller.getComponentStatus("AudioRuntime")));
}

void test_unknown_queries_return_defaults() {
    SystemController controller;

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus("Missing")));
    TEST_ASSERT_FALSE(controller.isComponentRequired("Missing"));
}

void test_duplicate_registration_is_graceful_and_updates_required_flag() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("CLI", false));
    TEST_ASSERT_FALSE(controller.isComponentRequired("CLI"));
    TEST_ASSERT_TRUE(controller.markComponentFailed("CLI", nullptr));
    TEST_ASSERT_TRUE(controller.registerComponent("CLI", true));
    TEST_ASSERT_TRUE(controller.isComponentRequired("CLI"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed), static_cast<int>(controller.getComponentStatus("CLI")));
}

void test_registry_resets_on_new_controller_instance() {
    SystemController first;
    SystemController second;

    TEST_ASSERT_TRUE(first.registerComponent("BoardInfo", false));
    TEST_ASSERT_TRUE(first.markComponentFailed("BoardInfo", "first_boot_failure"));

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(second.getComponentStatus("BoardInfo")));
    TEST_ASSERT_FALSE(second.isComponentRequired("BoardInfo"));
}

void test_registry_handles_null_empty_and_long_names_safely() {
    SystemController controller;

    TEST_ASSERT_FALSE(controller.registerComponent(nullptr, true));
    TEST_ASSERT_FALSE(controller.registerComponent("", true));

    char longName[300] = {};
    for (size_t index = 0; index < sizeof(longName) - 1; ++index) {
        longName[index] = static_cast<char>('A' + (index % 26));
    }

    TEST_ASSERT_TRUE(controller.registerComponent(longName, true));
    TEST_ASSERT_TRUE(controller.isComponentRequired(longName));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus(longName)));
}

void test_registry_stores_name_copies_not_borrowed_pointers() {
    SystemController controller;

    char mutableName[] = "Sensor";
    TEST_ASSERT_TRUE(controller.registerComponent(mutableName, true));
    mutableName[0] = 'X';

    TEST_ASSERT_FALSE(controller.isComponentRequired(mutableName));
    TEST_ASSERT_TRUE(controller.isComponentRequired("Sensor"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus("Sensor")));
}

void test_mark_component_failed_accepts_null_reason() {
    SystemController controller;

    TEST_ASSERT_TRUE(controller.registerComponent("WiFi", true));
    TEST_ASSERT_TRUE(controller.markComponentFailed("WiFi", nullptr));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed), static_cast<int>(controller.getComponentStatus("WiFi")));
}

void test_registry_handles_twenty_components() {
    SystemController controller;

    for (uint32_t index = 0; index < 20; ++index) {
        char name[32] = {};
        std::snprintf(name, sizeof(name), "Component%02lu", static_cast<unsigned long>(index));
        TEST_ASSERT_TRUE(controller.registerComponent(name, (index % 2) == 0));
        TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Unknown), static_cast<int>(controller.getComponentStatus(name)));
        if ((index % 2) == 0) {
            TEST_ASSERT_TRUE(controller.isComponentRequired(name));
        } else {
            TEST_ASSERT_FALSE(controller.isComponentRequired(name));
        }
    }

    TEST_ASSERT_TRUE(controller.markComponentFailed("Component19", "simulated_failure"));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentLifecycleStatus::Failed), static_cast<int>(controller.getComponentStatus("Component19")));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_register_component_stores_entry_and_returns_success);
    RUN_TEST(test_mark_component_failed_updates_state);
    RUN_TEST(test_unknown_queries_return_defaults);
    RUN_TEST(test_duplicate_registration_is_graceful_and_updates_required_flag);
    RUN_TEST(test_registry_resets_on_new_controller_instance);
    RUN_TEST(test_registry_handles_null_empty_and_long_names_safely);
    RUN_TEST(test_registry_stores_name_copies_not_borrowed_pointers);
    RUN_TEST(test_mark_component_failed_accepts_null_reason);
    RUN_TEST(test_registry_handles_twenty_components);
    return UNITY_END();
}