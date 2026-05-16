#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#else
#include "native_stubs.h"
#endif

#include "../component_types.h"
#include "orchestrator.h"

namespace detail {

    /* Rank values in declaration order -- extracted at compile time */
    constexpr uint8_t kValues[] = {
    #define X(name, value) value,
        SYSTEM_STATE_X(X)
    #undef X
    };

    /* State enum values in declaration order -- replaces old stateRoute[] */
    constexpr SystemState kRoute[] = {
    #define X(name, value) SystemState::name,
        SYSTEM_STATE_X(X)
    #undef X
    };

    constexpr uint8_t kCount = sizeof(kValues) / sizeof(kValues[0]);

    /* Maximum rank value, determines lookup table size */
    constexpr uint8_t kMaxValue = [] {
        uint8_t m = 0;
        for (auto v : kValues) if (v > m) m = v;
        return m;
    }();

    /* Build a table: index by uint8_t rank value -> positional index (or -1) */
    constexpr auto buildRankTable() {
        std::array<int, kMaxValue + 1> t{};
        for (auto& v : t) v = -1;
        for (uint8_t i = 0; i < kCount; ++i)
            t[kValues[i]] = static_cast<int>(i);
        return t;
    }

    constexpr auto kRankTable = buildRankTable();

}  // namespace detail


/* Backward-compatible aliases -- .cpp and existing tests use these unchanged */
inline constexpr auto& stateRoute = detail::kRoute;
inline constexpr size_t stateCount = detail::kCount;

/** @brief O(1) lookup: state rank value -> positional index.
 *  @param state The system state to look up.
 *  @return Index (0, 1, 2...) or -1 if the rank value has no mapping.
 */
inline constexpr int getIndex(SystemState state) {
    const uint8_t raw = static_cast<uint8_t>(state);
    if (raw > detail::kMaxValue) return -1;
    return detail::kRankTable[raw];
}

constexpr size_t componentCount = static_cast<size_t>(ComponentID::Count);

static_assert(componentCount < 32, "componentCount must be < 32 for event-group bitmask safety");

/** @brief Bitmask covering all component event-group bits. Auto-scales with componentCount. */
constexpr EventBits_t kAllComponentBits = (1U << componentCount) - 1;

SystemState getNextState(SystemState current, SystemState target);

const char* stateToString(SystemState state);

void orchestrationWorker(void* param);

enum class SubState {
	PENDING, // Waiting for orchestration to complete, then will step to targetMode_.
	COMMITTED, // Orchestration completed, stepped to targetMode_, and is now the observed state.
	FAILED // Orchestration failed, stepped to ERROR.
};

struct ActiveTransition {
	SystemState transitionTarget{SystemState::BOOTING};
	SubState subState = SubState::PENDING;
};

struct Mailbox {
	bool pending = false;
	SystemState requestedTarget;
	portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
};

/** @brief Error event: single-slot flag with payload.
 *  First-writer-wins: only the first error per transition is accepted.
 */
struct ErrorEvent {
	bool pending = false;
	DebugReason reason = nullptr;
	ComponentID source = ComponentID::Count;
	portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
};

/** @brief Recovery attempt tracking.
 *  Holds the maximum allowed recovery attempts and the current attempt
 *  counter. Self-contained so callers need only call isExhausted().
 */
struct RetryPolicy {
	int maxRecoveries = 3;
	int recoveryCounter = 0;

	/** @brief Check whether the recovery budget is exhausted.
	 *  @return true if recoveryCounter >= maxRecoveries.
	 */
	bool isExhausted() const {
		return recoveryCounter >= maxRecoveries;
	}
};

enum class ComponentStatus : uint8_t {
	COMMITTED,
	FAILED,
	DEGRADED
};

struct TransitionTimeoutConfig {
	std::array<uint32_t, stateCount> forwardTimeouts{};   // Per-state timeout (ms) for forward transitions
	std::array<uint32_t, stateCount> backwardTimeouts{};  // Per-state timeout (ms) for backward transitions
};

// Temporary defaults until timeout matrices are loaded from NVS.
constexpr std::array<uint32_t, stateCount> makeUniformTimeouts(uint32_t value) {
	std::array<uint32_t, stateCount> timeouts{};
	for (size_t i = 0; i < stateCount; ++i) {
		timeouts[i] = value;
	}
	return timeouts;
}

constexpr auto kDefaultForwardTimeouts = makeUniformTimeouts(5000);
constexpr auto kDefaultBackwardTimeouts = makeUniformTimeouts(5000);

class SupervisorV2 {
public:
	SupervisorV2();

	/** @brief Initialise the supervisor.
	 *  Creates the FreeRTOS event group via xEventGroupCreateStatic(),
	 *  loads the transition timeout config from defaults (NVS in future).
	 */
	void setup();

	/** @brief Get the currently observed system state.
	 *  @return The observed system state.
	 */
	SystemState getObservedState() const;

	/** @brief Get the target system state.
	 *  @return The target system state.
	 */
	SystemState getTargetState() const;

	/** @brief Post a state transition request.
	 *  Last-write-wins: overwrites any pending request.
	 *  @param target The desired system state.
	 */
	void postStateRequest(SystemState target);

	/** @brief Post an asynchronous error event.
	 *  First-writer-wins: ignored if error already pending.
	 *  @param reason Human-readable failure description.
	 *  @param source The component that generated the error.
	 */
	void postErrorEvent(DebugReason reason, ComponentID source);

	/** @brief Run one tick of the state machine.
	 *  Drains mailboxes, steps toward target, checks orchestration completion
	 *  and timeouts. Called from the FreeRTOS task loop.
	 */
	void run();

	/** @brief Signal component transition completion.
	 *  Safe to call from any core. Sets the component's event group bit
	 *  on success or handles failure (required -> ERROR, optional -> DEGRADED).
	 *  @param id The component reporting completion.
	 *  @param status Completed or Failed.
	 */
	void completeTransition(ComponentID id, TransitionStatus status);

	/** @brief Register a component with the supervisor.
	 *  Called by each component at boot to signal its presence.
	 *  @param id The component to register.
	 *  @param mailbox Pointer to the component-owned ComponentMailbox.
	 *  @param isRequired Whether this component is required for quorum.
	 */
	void registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired);

	friend void fatalTask(SupervisorV2* supervisor);

#ifdef UNIT_TEST
	friend struct S2V2Access;
#endif

private:
	/** @brief Consume and clear a pending state request.
	 *  @return true if a request was pending and applied, false otherwise.
	 */
	bool consumeStateRequest();

	/** @brief Consume a pending error event and act on it.
	 *
	 *  4-step logic:
	 *  1. If no error is pending, return immediately.
	 *  2. Log the error with component source, reason and attempt counter.
	 *  3. Increment the recovery attempt counter.
	 *  4. If the recovery budget is exhausted, transition to FATAL.
	 *
	 *  After processing the error event is always cleared.
	 */
	void consumeErrorEvent();

	/** @brief Get the maximum number of recovery attempts before FATAL.
	 *  @return The current maxRecoveries setting.
	 */
	int getMaxRecoveries() const;

	/** @brief Set the maximum number of recovery attempts before FATAL.
	 *  Minimum valid value is 1. Values below 1 are silently rejected.
	 *  @param recoveries The new maximum recovery count.
	 */
	void setMaxRecoveries(int recoveries);

	/** @brief Get the transition timeout for a given system state.
	 *  @param state The system state to query.
	 *  @param isForward true for forward transitions, false for backward.
	 *  @return The timeout in milliseconds.
	 */
	uint32_t getTransitionTimeout(SystemState state, bool isForward) const;

	/** @brief Load the transition timeout matrix from persistent storage.
	 */
	void loadTransitionTimeoutConfig();

	/** @brief Reset the recovery counter when leaving ERROR/FATAL.
	 *
	 *  Called whenever observedState_ changes. If the new observed state
	 *  is neither ERROR nor FATAL, the recovery counter is reset to 0.
	 *  This ensures a fresh recovery budget on each new error cycle.
	 *
	 *  Wire-up to run() is deferred; for now this method exists to
	 *  document the contract.
	 */
	void resetRecoveryIfOutOfError();

	/** @brief Begin an orchestration toward the given target state.
	 *  Clears the event group, writes all component mailboxes with the
	 *  target state, records start time and deadline.
	 *  @param target The state to orchestrate toward.
	 */
	void startOrchestration(SystemState target);

	/** @brief Check for a pending orchestration response from the worker task.
	 *  Reads responseMailbox_ (non-blocking, spinlock inside).
	 *  On COMPLETED: advances observedState_ via setObservedState().
	 *  On TIMED_OUT: marks timed-out components, posts error events.
	 */
	void checkOrchestrationResponse();

	/** @brief Compute the next stepping state and begin orchestration.
	 *
	 *  Called by run() when targetState_ differs from observedState_ and
	 *  no orchestration is in flight. Delegates to getNextState() which
	 *  uses the state rank table (multiples of 10: FATAL=0, ERROR=10,
	 *  SLEEP=20, ... LIVE=60) to determine the single-step intermediate.
	 *
	 *  If the intermediate equals the current observed state (meaning we
	 *  are already at the target), this is a no-op.
	 *
	 *  Otherwise, calls startOrchestration() which clears the event group,
	 *  writes all component mailboxes, computes the timeout deadline, and
	 *  posts an OrchestrationOrder to the worker task on Core 0.
	 */
	void stepTowardTarget();

	/** @brief Commit a new observed state.
	 *  Logs the transition, clears active orchestration flags,
	 *  calls resetRecoveryIfOutOfError().
	 *  @param state The new observed state.
	 */
	void setObservedState(SystemState state);

	/** @brief Determine the target state to aim for after ERROR recovery.
	 *  Placeholder: returns the state saved in lastTargetBeforeError_.
	 *  @return The recovery target state.
	 */
	SystemState determineRecoveryTarget();

	/** @brief Write the current orchestration target to a component's mailbox.
	 *  Reads target from nextState_.transitionTarget.
	 *  @param id Which component to target.
	 */
	void postNextComponentState(ComponentID id);

	/** @brief Spawn the FATAL task on first entry to FATAL state.
	 *  Records the entry tick, then creates a dedicated FreeRTOS task
	 *  that handles logging, component cleanup, the 60s dwell, and
	 *  esp_deep_sleep_start(). Called at most once.
	 */
	void spawnFatalTask();

	/** @brief Check that all required components have registered.
	 *  Posts an error event for each missing required component.
	 *  Called during the first orchestration to validate boot presence.
	 */
	void checkComponentPresence();

	// State fields: written/read only from the state machine task (single thread).
	// No concurrent access from other cores. Workers read order data through
	// spinlock-guarded mailboxes (orderMailbox_, responseMailbox_), never directly
	// from these fields.
	SystemState observedState_{SystemState::BOOTING};
	SystemState targetState_{SystemState::BOOTING};
	ActiveTransition nextState_;

	Mailbox stateRequestMailbox_{};
	ErrorEvent errorEvent_{};
	RetryPolicy retryPolicy_{};
	/** @brief Per-component health tracking.
	 *  Only resets to COMMITTED on reboot (zero-initialization of global/BSS).
	 *  This is intentional — a component that failed once is not trusted again
	 *  until the next full system restart.
	 *
	 *  If revisited, there are two future options:
	 *    1 — Per-component self-healing: DEGRADED that reports Completed resets
	 *        to COMMITTED, proving health individually.
	 *    2 — Reset on leaving ERROR: clear all statuses when transitioning from
	 *        ERROR to a non-error state.
	 *  See docs/superpowers/specs/2026-05-16-code-review-fixes-design.md#11.
	 */
	std::array<ComponentStatus, componentCount> componentStatuses_{};
	TransitionTimeoutConfig timeoutConfig_{};

	StaticEventGroup_t eventGroupBuffer_{};
	EventGroupHandle_t eventGroup_{};

	std::array<ComponentMailbox*, componentCount> componentMailboxes_{};
	std::array<bool, componentCount> isRequired_{};

	bool hasActiveOrchestration_{};
	OrchestrationOrder orderMailbox_{};
	OrchestrationResponse responseMailbox_{};
	// Task handles: default-initialized to nullptr. All xTaskNotifyGive calls are
	// guarded against null (see postStateRequest, postErrorEvent, startOrchestration).
	TaskHandle_t workerTaskHandle_{};
	TaskHandle_t supervisorTaskHandle_{};

	TaskHandle_t fatalTaskHandle_{};
	bool fatalTaskSpawned_{};

	TickType_t fatalEnteredTicks_{};
	bool fatalDeadlineElapsed_{};
	bool firstOrchestration_{true};

	/** @brief Saved target for ERROR recovery placeholder.
	 *  Auto-snapshotted by setTargetState() when transitioning to ERROR.
	 *  TODO: remove once determineRecoveryTarget() is replaced with real logic.
	 */
	SystemState lastTargetBeforeError_{SystemState::BOOTING};

	void setTargetState(SystemState target);

    friend void orchestrationWorker(void* param);
};