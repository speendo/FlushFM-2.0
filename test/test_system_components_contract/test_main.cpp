#include <cstdint>
#include <limits>
#include <type_traits>
#include <unity.h>

#include "../../src/components/composition/system_components.h"

namespace {

class ContractProbeComponent final : public ISystemComponent {
public:
    ContractProbeComponent(uint32_t offTimeout,
                           uint32_t idleTimeout,
                           uint32_t streamingTimeout,
                           uint32_t errorTimeout)
        : ISystemComponent("ContractProbe"),
          offTimeout_(offTimeout),
          idleTimeout_(idleTimeout),
          streamingTimeout_(streamingTimeout),
          errorTimeout_(errorTimeout) {}

    bool setup() override { return true; }

    uint32_t setOFF(uint32_t transitionId) override {
        lastTransitionId_ = transitionId;
        return offTimeout_;
    }

    uint32_t setIDLE(uint32_t transitionId) override {
        lastTransitionId_ = transitionId;
        return idleTimeout_;
    }

    uint32_t setSTREAMING(uint32_t transitionId) override {
        lastTransitionId_ = transitionId;
        return streamingTimeout_;
    }

    uint32_t setERROR(uint32_t transitionId) override {
        lastTransitionId_ = transitionId;
        return errorTimeout_;
    }

    void onTransitionTimeout(uint32_t transitionId) override {
        ++timeoutCallCount_;
        lastTransitionId_ = transitionId;
    }

    uint32_t lastTransitionId() const { return lastTransitionId_; }
    uint32_t timeoutCallCount() const { return timeoutCallCount_; }

private:
    uint32_t offTimeout_;
    uint32_t idleTimeout_;
    uint32_t streamingTimeout_;
    uint32_t errorTimeout_;
    uint32_t lastTransitionId_ = 0;
    uint32_t timeoutCallCount_ = 0;
};

static_assert(std::is_same<decltype(&BoardInfoComponent::setOFF), uint32_t (BoardInfoComponent::*)(uint32_t)>::value,
              "BoardInfoComponent::setOFF signature mismatch");
static_assert(std::is_same<decltype(&BoardInfoComponent::setIDLE), uint32_t (BoardInfoComponent::*)(uint32_t)>::value,
              "BoardInfoComponent::setIDLE signature mismatch");
static_assert(std::is_same<decltype(&BoardInfoComponent::setSTREAMING), uint32_t (BoardInfoComponent::*)(uint32_t)>::value,
              "BoardInfoComponent::setSTREAMING signature mismatch");
static_assert(std::is_same<decltype(&BoardInfoComponent::setERROR), uint32_t (BoardInfoComponent::*)(uint32_t)>::value,
              "BoardInfoComponent::setERROR signature mismatch");
static_assert(std::is_same<decltype(&BoardInfoComponent::onTransitionTimeout), void (BoardInfoComponent::*)(uint32_t)>::value,
              "BoardInfoComponent::onTransitionTimeout signature mismatch");

static_assert(std::is_same<decltype(&WiFiComponent::setOFF), uint32_t (WiFiComponent::*)(uint32_t)>::value,
              "WiFiComponent::setOFF signature mismatch");
static_assert(std::is_same<decltype(&WiFiComponent::setIDLE), uint32_t (WiFiComponent::*)(uint32_t)>::value,
              "WiFiComponent::setIDLE signature mismatch");
static_assert(std::is_same<decltype(&WiFiComponent::setSTREAMING), uint32_t (WiFiComponent::*)(uint32_t)>::value,
              "WiFiComponent::setSTREAMING signature mismatch");
static_assert(std::is_same<decltype(&WiFiComponent::setERROR), uint32_t (WiFiComponent::*)(uint32_t)>::value,
              "WiFiComponent::setERROR signature mismatch");
static_assert(std::is_same<decltype(&WiFiComponent::onTransitionTimeout), void (WiFiComponent::*)(uint32_t)>::value,
              "WiFiComponent::onTransitionTimeout signature mismatch");

static_assert(std::is_same<decltype(&AudioRuntimeComponent::setOFF), uint32_t (AudioRuntimeComponent::*)(uint32_t)>::value,
              "AudioRuntimeComponent::setOFF signature mismatch");
static_assert(std::is_same<decltype(&AudioRuntimeComponent::setIDLE), uint32_t (AudioRuntimeComponent::*)(uint32_t)>::value,
              "AudioRuntimeComponent::setIDLE signature mismatch");
static_assert(std::is_same<decltype(&AudioRuntimeComponent::setSTREAMING), uint32_t (AudioRuntimeComponent::*)(uint32_t)>::value,
              "AudioRuntimeComponent::setSTREAMING signature mismatch");
static_assert(std::is_same<decltype(&AudioRuntimeComponent::setERROR), uint32_t (AudioRuntimeComponent::*)(uint32_t)>::value,
              "AudioRuntimeComponent::setERROR signature mismatch");
static_assert(std::is_same<decltype(&AudioRuntimeComponent::onTransitionTimeout), void (AudioRuntimeComponent::*)(uint32_t)>::value,
              "AudioRuntimeComponent::onTransitionTimeout signature mismatch");

static_assert(std::is_same<decltype(&CliComponent::setOFF), uint32_t (CliComponent::*)(uint32_t)>::value,
              "CliComponent::setOFF signature mismatch");
static_assert(std::is_same<decltype(&CliComponent::setIDLE), uint32_t (CliComponent::*)(uint32_t)>::value,
              "CliComponent::setIDLE signature mismatch");
static_assert(std::is_same<decltype(&CliComponent::setSTREAMING), uint32_t (CliComponent::*)(uint32_t)>::value,
              "CliComponent::setSTREAMING signature mismatch");
static_assert(std::is_same<decltype(&CliComponent::setERROR), uint32_t (CliComponent::*)(uint32_t)>::value,
              "CliComponent::setERROR signature mismatch");
static_assert(std::is_same<decltype(&CliComponent::onTransitionTimeout), void (CliComponent::*)(uint32_t)>::value,
              "CliComponent::onTransitionTimeout signature mismatch");

static_assert(!std::is_abstract<BoardInfoComponent>::value, "BoardInfoComponent must be concrete");
static_assert(!std::is_abstract<WiFiComponent>::value, "WiFiComponent must be concrete");
static_assert(!std::is_abstract<AudioRuntimeComponent>::value, "AudioRuntimeComponent must be concrete");
static_assert(!std::is_abstract<CliComponent>::value, "CliComponent must be concrete");

void test_set_methods_return_configured_timeout_values() {
    ContractProbeComponent component(11, 22, 33, 44);

    TEST_ASSERT_EQUAL_UINT32(11, component.setOFF(1));
    TEST_ASSERT_EQUAL_UINT32(22, component.setIDLE(2));
    TEST_ASSERT_EQUAL_UINT32(33, component.setSTREAMING(3));
    TEST_ASSERT_EQUAL_UINT32(44, component.setERROR(4));
}

void test_transition_id_edge_values_are_accepted() {
    ContractProbeComponent component(1, 1, 1, 1);

    (void)component.setOFF(0);
    TEST_ASSERT_EQUAL_UINT32(0, component.lastTransitionId());

    const uint32_t maxId = std::numeric_limits<uint32_t>::max();
    (void)component.setSTREAMING(maxId);
    TEST_ASSERT_EQUAL_UINT32(maxId, component.lastTransitionId());
}

void test_timeout_value_edge_cases_are_accepted() {
    const uint32_t maxTimeout = std::numeric_limits<uint32_t>::max();
    ContractProbeComponent component(0, maxTimeout, 0, maxTimeout);

    TEST_ASSERT_EQUAL_UINT32(0, component.setOFF(10));
    TEST_ASSERT_EQUAL_UINT32(maxTimeout, component.setIDLE(11));
    TEST_ASSERT_EQUAL_UINT32(0, component.setSTREAMING(12));
    TEST_ASSERT_EQUAL_UINT32(maxTimeout, component.setERROR(13));
}

void test_on_transition_timeout_is_repeatable_for_same_id() {
    ContractProbeComponent component(1, 2, 3, 4);

    component.onTransitionTimeout(777);
    component.onTransitionTimeout(777);

    TEST_ASSERT_EQUAL_UINT32(2, component.timeoutCallCount());
    TEST_ASSERT_EQUAL_UINT32(777, component.lastTransitionId());
}

void test_components_are_state_isolated() {
    ContractProbeComponent componentA(100, 200, 300, 400);
    ContractProbeComponent componentB(1, 2, 3, 4);

    (void)componentA.setIDLE(42);
    (void)componentB.setERROR(7);

    TEST_ASSERT_EQUAL_UINT32(42, componentA.lastTransitionId());
    TEST_ASSERT_EQUAL_UINT32(7, componentB.lastTransitionId());
    TEST_ASSERT_EQUAL_UINT32(200, componentA.setIDLE(99));
    TEST_ASSERT_EQUAL_UINT32(4, componentB.setERROR(88));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_methods_return_configured_timeout_values);
    RUN_TEST(test_transition_id_edge_values_are_accepted);
    RUN_TEST(test_timeout_value_edge_cases_are_accepted);
    RUN_TEST(test_on_transition_timeout_is_repeatable_for_same_id);
    RUN_TEST(test_components_are_state_isolated);
    return UNITY_END();
}
