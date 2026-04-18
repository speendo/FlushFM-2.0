#include <type_traits>

#include <unity.h>

#include "../../src/state_machine/system_controller.cpp"

namespace {

static_assert(std::is_same<decltype(&SystemController::requestTransition), TransitionRequestDecision (SystemController::*)(SystemState, SystemState, uint32_t)>::value,
              "SystemController::requestTransition signature mismatch");
static_assert(std::is_same<decltype(&SystemController::finishTransition), bool (SystemController::*)(uint32_t)>::value,
              "SystemController::finishTransition signature mismatch");

void test_first_transition_starts_when_none_is_active() {
    SystemController controller;

    const TransitionRequestDecision decision = controller.requestTransition(SystemState::READY, SystemState::LIVE, 10);

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Started), static_cast<int>(decision));
    TEST_ASSERT_TRUE(controller.hasActiveTransition());
    TEST_ASSERT_EQUAL_UINT32(10, controller.activeTransitionId());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(controller.activeTransitionFrom()));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(controller.activeTransitionTarget()));
    TEST_ASSERT_FALSE(controller.hasQueuedTransition());
}

void test_reciprocal_transition_supersedes_active_transition() {
    SystemController controller;

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Started),
                      static_cast<int>(controller.requestTransition(SystemState::LIVE, SystemState::READY, 21)));

    const TransitionRequestDecision decision = controller.requestTransition(SystemState::READY, SystemState::LIVE, 22);

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Superseded), static_cast<int>(decision));
    TEST_ASSERT_TRUE(controller.hasActiveTransition());
    TEST_ASSERT_EQUAL_UINT32(22, controller.activeTransitionId());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(controller.activeTransitionFrom()));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(controller.activeTransitionTarget()));
    TEST_ASSERT_FALSE(controller.hasQueuedTransition());
}

void test_non_reciprocal_transition_is_queued() {
    SystemController controller;

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Started),
                      static_cast<int>(controller.requestTransition(SystemState::LIVE, SystemState::READY, 31)));

    const TransitionRequestDecision decision = controller.requestTransition(SystemState::READY, SystemState::ERROR, 32);

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Queued), static_cast<int>(decision));
    TEST_ASSERT_TRUE(controller.hasActiveTransition());
    TEST_ASSERT_EQUAL_UINT32(31, controller.activeTransitionId());
    TEST_ASSERT_TRUE(controller.hasQueuedTransition());
    TEST_ASSERT_EQUAL_UINT32(32, controller.queuedTransitionId());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(controller.queuedTransitionFrom()));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.queuedTransitionTarget()));
}

void test_finishing_active_transition_promotes_queued_transition() {
    SystemController controller;

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Started),
                      static_cast<int>(controller.requestTransition(SystemState::LIVE, SystemState::READY, 40)));
    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Queued),
                      static_cast<int>(controller.requestTransition(SystemState::READY, SystemState::ERROR, 41)));

    TEST_ASSERT_TRUE(controller.finishTransition(40));
    TEST_ASSERT_TRUE(controller.hasActiveTransition());
    TEST_ASSERT_EQUAL_UINT32(41, controller.activeTransitionId());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(controller.activeTransitionFrom()));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.activeTransitionTarget()));
    TEST_ASSERT_FALSE(controller.hasQueuedTransition());
}

void test_finishing_with_wrong_id_is_rejected() {
    SystemController controller;

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Started),
                      static_cast<int>(controller.requestTransition(SystemState::READY, SystemState::LIVE, 51)));
    TEST_ASSERT_FALSE(controller.finishTransition(999));
    TEST_ASSERT_TRUE(controller.hasActiveTransition());
    TEST_ASSERT_EQUAL_UINT32(51, controller.activeTransitionId());
}

void test_ignored_when_from_equals_target() {
    SystemController controller;

    const TransitionRequestDecision decision = controller.requestTransition(SystemState::READY, SystemState::READY, 60);

    TEST_ASSERT_EQUAL(static_cast<int>(TransitionRequestDecision::Ignored), static_cast<int>(decision));
    TEST_ASSERT_FALSE(controller.hasActiveTransition());
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_transition_starts_when_none_is_active);
    RUN_TEST(test_reciprocal_transition_supersedes_active_transition);
    RUN_TEST(test_non_reciprocal_transition_is_queued);
    RUN_TEST(test_finishing_active_transition_promotes_queued_transition);
    RUN_TEST(test_finishing_with_wrong_id_is_rejected);
    RUN_TEST(test_ignored_when_from_equals_target);
    return UNITY_END();
}