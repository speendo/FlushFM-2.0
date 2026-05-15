#ifndef COMPONENT_TYPES_H_
#define COMPONENT_TYPES_H_

#include <cstdint>
#include <cstddef>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#else
using portMUX_TYPE = uint32_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#endif

/** @brief Ranked system state enum for the Supervisor state machine.
 *  X-macro: V(name, rank) where rank is a uint8_t.
 *  Ranks are spaced by 10 to allow future insertions.
 *  FATAL is absorbent (no transitions out), ERROR triggers recovery. */
#define SYSTEM_STATE_X(V) \
    V(FATAL, 0) \
    V(ERROR, 10) \
    V(SLEEP, 20) \
    V(BOOTING, 30) \
    V(CONNECTING, 40) \
    V(READY, 50) \
    V(LIVE, 60)

#define SYSTEM_STATE_ENUM(name, value) name = value,

enum class SystemState : uint8_t {
    SYSTEM_STATE_X(SYSTEM_STATE_ENUM)
};

#undef SYSTEM_STATE_ENUM

inline bool isErrorState(SystemState state) {
    return state == SystemState::ERROR || state == SystemState::FATAL;
}

inline const char* stateToString(SystemState state) {
    switch (state) {
#define SYSTEM_STATE_STRING(name, value) case SystemState::name: return #name;
        SYSTEM_STATE_X(SYSTEM_STATE_STRING)
#undef SYSTEM_STATE_STRING
        default: return "UNKNOWN";
    }
}

/** @brief Single-slot mailbox for component state targets.
 *  Last-write-wins. Owned by each component; the supervisor writes cross-core
 *  under the embedded spinlock.
 */
struct ComponentMailbox {
	bool pending = false;
	SystemState targetState;
	portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

	/** @brief Read and clear a pending state target under spinlock.
	 *  Safe to call from any core. Blocks briefly if the supervisor is
	 *  concurrently writing to this mailbox.
	 *  @param outTarget Receives the target state if one was pending.
	 *  @return true if a target was consumed, false otherwise.
	 */
	bool consumeNextState(SystemState& outTarget) {
		portENTER_CRITICAL(&spinlock);
		if (!pending) {
			portEXIT_CRITICAL(&spinlock);
			return false;
		}
		outTarget = targetState;
		pending = false;
		portEXIT_CRITICAL(&spinlock);
		return true;
	}
};

#define COMPONENT_TYPES_TRANSITION_STATUS_X(V) \
    V(Completed) \
    V(Failed)

#define COMPONENT_TYPES_TRANSITION_STATUS_ENUM(name) name,
enum class TransitionStatus : uint8_t {
    COMPONENT_TYPES_TRANSITION_STATUS_X(COMPONENT_TYPES_TRANSITION_STATUS_ENUM)
};

inline const char* toString(TransitionStatus status) {
    switch (status) {
#define COMPONENT_TYPES_TRANSITION_STATUS_STRING(name) case TransitionStatus::name: return #name;
        COMPONENT_TYPES_TRANSITION_STATUS_X(COMPONENT_TYPES_TRANSITION_STATUS_STRING)
#undef COMPONENT_TYPES_TRANSITION_STATUS_STRING
    }
    return "UNKNOWN";
}

#undef COMPONENT_TYPES_TRANSITION_STATUS_ENUM
#undef COMPONENT_TYPES_TRANSITION_STATUS_X

#define COMPONENT_ID_X(V) \
    V(BoardInfo) \
    V(WiFi) \
    V(AudioRuntime) \
    V(CLI)

#define COMPONENT_ID_ENUM(name) name,
enum class ComponentID : uint8_t {
    COMPONENT_ID_X(COMPONENT_ID_ENUM)
    Count
};

inline const char* componentName(ComponentID id) {
    switch (id) {
#define COMPONENT_ID_STRING(name) case ComponentID::name: return #name;
        COMPONENT_ID_X(COMPONENT_ID_STRING)
#undef COMPONENT_ID_STRING
        default: return "UNKNOWN";
    }
}

#undef COMPONENT_ID_ENUM
#undef COMPONENT_ID_X

using DebugReason = const char*;

#endif  // COMPONENT_TYPES_H_
