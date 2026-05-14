#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#else
#include <cstring>
using EventGroupHandle_t = void*;
struct StaticEventGroup_t { uint8_t data[32]; };
using TickType_t = uint32_t;
using EventBits_t = uint32_t;
inline EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t*) { return nullptr; }
inline EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t) { return 0; }
inline EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t) { return 0; }
inline EventBits_t xEventGroupGetBits(EventGroupHandle_t) { return 0; }
#endif

#include "component_types.h"

/** @defgroup supervisor_state System States
 *  Ranked system states used by the Supervisor state machine.
 *  States are ordered by rank (multiples of 10) to enable directional
 *  comparison (upward/downward transitions). Lower rank = less active.
 *  @{ */

/** X-macro generating the SystemState enum.
 *  Each entry is `V(name, rank)` where rank is a uint8_t value.
 *  Ranks are spaced by 10 to allow future insertions.
 *  Values ≤30 are considered transient/internal stepping states
 *  and cannot be targeted by external STATE_REQUESTED calls. */
#define SYSTEM_STATE_X(V) \
	V(FATAL, 0)       /* Unrecoverable, halts all processing. */ \
	V(ERROR, 10)      /* Recoverable error, target resets to SLEEP. */ \
	V(SLEEP, 20)      /* Low-power idle. */ \
	V(BOOTING, 30)    /* Initial power-on (transient, unreachable from external requests). */ \
	V(CONNECTING, 40) /* Network/audio connecting (transient, zero-dwell pass-through). */ \
	V(READY, 50)      /* Components initialized, ready for streaming. */ \
	V(LIVE, 60)       /* Active audio streaming. */

/* Generate the enum values */
#define SYSTEM_STATE_ENUM(name, value) name = value,

enum class SystemState : uint8_t {
    SYSTEM_STATE_X(SYSTEM_STATE_ENUM)
};

#undef SYSTEM_STATE_ENUM

/* Generate the array of state names */
#define SYSTEM_STATE_ARRAY(name, value) SystemState::name,

const SystemState stateRoute[] = {
	SYSTEM_STATE_X(SYSTEM_STATE_ARRAY)
};

#undef SYSTEM_STATE_ARRAY

constexpr size_t stateCount = sizeof(stateRoute) / sizeof(SystemState);

constexpr size_t componentCount = static_cast<size_t>(ComponentID::Count);

SystemState getNextState(SystemState current, SystemState target);

const char* stateToString(SystemState state);

enum class SubState {
	PENDING, // Waiting for orchestration to complete, then will step to targetMode_.
	COMMITTED, // Orchestration completed, stepped to targetMode_, and is now the observed state.
	FAILED // Orchestration failed, stepped to ERROR.
};

struct ActiveTransition {
	SystemState transitionTarget;
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

using ComponentStatusMap = std::array<ComponentStatus, componentCount>;

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

	/** @brief Check if all expected event group bits are set.
	 *  If complete, advances observedState_ via setObservedState().
	 */
	void checkOrchestrationCompletion();

	/** @brief Check whether the current orchestration has timed out.
	 *  Marks overdue components as FAILED and handles consequences.
	 */
	void checkStateTimeout();

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

	/** @brief Manage the deep sleep shutdown after FATAL.
	 *  On first call, sets fatalDeadlineMs_ = now + 60s.
	 *  On subsequent calls, triggers esp_deep_sleep_start() if the
	 *  deadline has elapsed.
	 */
	void handleFatal();

	/** @brief Check that all required components have registered.
	 *  Posts an error event for each missing required component.
	 *  Called during the first orchestration to validate boot presence.
	 */
	void checkComponentPresence();

	SystemState observedState_;
	SystemState targetState_;
	ActiveTransition nextState_;

	Mailbox stateRequestMailbox_{};
	ErrorEvent errorEvent_{};
	RetryPolicy retryPolicy_{};
	ComponentStatusMap componentStatuses_{};
	TransitionTimeoutConfig timeoutConfig_{};

	StaticEventGroup_t eventGroupBuffer_{};
	EventGroupHandle_t eventGroup_{};

	ComponentMailbox* componentMailboxes_[componentCount]{};
	bool isRequired_[componentCount]{};

	TickType_t orchestrationDeadlineMs_{};
	bool hasActiveOrchestration_{};
	EventBits_t expectedBits_{};

	TickType_t fatalDeadlineMs_{};

	/** @brief Saved target for ERROR recovery placeholder.
	 *  Auto-snapshotted by setTargetState() when transitioning to ERROR.
	 *  TODO: remove once determineRecoveryTarget() is replaced with real logic.
	 */
	SystemState lastTargetBeforeError_;

	void setTargetState(SystemState target);
};