#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
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
};

/** @brief Error event: single-slot flag with payload.
 *  First-writer-wins: only the first error per transition is accepted.
 */
struct ErrorEvent {
	bool pending = false;
	DebugReason reason = nullptr;
	ComponentID source = ComponentID::Count;
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

/** @brief Single-slot mailbox for component state targets.
 *  Last-write-wins. Owned by each component; the supervisor writes cross-core.
 */
struct ComponentMailbox {
	bool pending = false;
	SystemState targetState;
};

/** @brief A component-owned mailbox slot with its spinlock.
 *  The supervisor holds a pointer to this and writes under spinlock.
 */
struct ComponentMailboxSlot {
	ComponentMailbox mailbox;
	portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
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

	SystemState observedState_;
	SystemState targetState_;
	ActiveTransition nextState_;

	Mailbox stateRequestMailbox_{};
	ErrorEvent errorEvent_{};
	RetryPolicy retryPolicy_{};
	ComponentStatusMap componentStatuses_{};
	TransitionTimeoutConfig timeoutConfig_{};

	void setTargetState(SystemState target);
};