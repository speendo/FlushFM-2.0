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
constexpr auto& stateRoute = detail::kRoute;
constexpr size_t stateCount = detail::kCount;

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

/** @brief Result of an orchestration attempt.
 *  COMPLETED: all required bits were set before the deadline.
 *  TIMED_OUT: the deadline elapsed with bits still missing.
 */
enum class OrchestrationResult : uint8_t {
    COMPLETED,
    TIMED_OUT
};

/** @brief Order posted by the state machine to the orchestration worker.
 *  Single-slot, spinlock-guarded. Last-write-wins.
 *  The worker reads this via consume() then begins waiting on the event group.
 */
struct OrchestrationOrder {
    bool pending = false;
    EventBits_t expectedBits = 0;
    TickType_t deadlineMs = 0;
    SystemState transitionTarget;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(EventBits_t bits, TickType_t deadline, SystemState target) {
        portENTER_CRITICAL(&spinlock);
        expectedBits = bits;
        deadlineMs = deadline;
        transitionTarget = target;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    /** @brief Clear the pending flag under spinlock. Caller reads members directly after.
     *  @return true if an order was pending and was consumed.
     */
    bool consume() {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};

/** @brief Response posted by the orchestration worker to the state machine.
 *  Single-slot, spinlock-guarded. The state machine reads this on each run() tick.
 */
struct OrchestrationResponse {
    bool pending = false;
    OrchestrationResult result = OrchestrationResult::COMPLETED;
    EventBits_t timedOutComponents = 0;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(OrchestrationResult r, EventBits_t timedOut) {
        portENTER_CRITICAL(&spinlock);
        result = r;
        timedOutComponents = timedOut;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    /** @brief Clear the pending flag under spinlock. Caller reads members directly after.
     *  @return true if a response was pending and was consumed.
     */
    bool consume() {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
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

	/** @brief Check for a pending orchestration response from the worker task.
	 *  Reads responseMailbox_ (non-blocking, spinlock inside).
	 *  On COMPLETED: advances observedState_ via setObservedState().
	 *  On TIMED_OUT: marks timed-out components, posts error events.
	 */
	void checkOrchestrationResponse();

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

	std::array<ComponentMailbox*, componentCount> componentMailboxes_{};
	std::array<bool, componentCount> isRequired_{};

	bool hasActiveOrchestration_{};
	OrchestrationOrder orderMailbox_{};
	OrchestrationResponse responseMailbox_{};
	TaskHandle_t workerTaskHandle_{};
	TaskHandle_t supervisorTaskHandle_{};

	TickType_t fatalDeadlineMs_{};

	/** @brief Saved target for ERROR recovery placeholder.
	 *  Auto-snapshotted by setTargetState() when transitioning to ERROR.
	 *  TODO: remove once determineRecoveryTarget() is replaced with real logic.
	 */
	SystemState lastTargetBeforeError_;

	void setTargetState(SystemState target);

    friend void orchestrationWorker(void* param);
};

#undef SYSTEM_STATE_X