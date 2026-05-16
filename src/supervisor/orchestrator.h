#pragma once

#include <cstdint>
#include <cstddef>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#else
#include "native_stubs.h"
#endif

#include "component_types.h"

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

    /** @brief Copy all fields under spinlock and clear the pending flag.
     *  @param outBits Receives the expected bits mask.
     *  @param outDeadline Receives the deadline tick.
     *  @param outTarget Receives the transition target state.
     *  @return true if an order was pending and was consumed.
     */
    bool consume(EventBits_t& outBits, TickType_t& outDeadline, SystemState& outTarget) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outBits = expectedBits;
        outDeadline = deadlineMs;
        outTarget = transitionTarget;
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

    /** @brief Copy all fields under spinlock and clear the pending flag.
     *  @param outResult Receives the orchestration result.
     *  @param outTimedOut Receives the timed-out component bits.
     *  @return true if a response was pending and was consumed.
     */
    bool consume(OrchestrationResult& outResult, EventBits_t& outTimedOut) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outResult = result;
        outTimedOut = timedOutComponents;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};
