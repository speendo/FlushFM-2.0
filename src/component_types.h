#pragma once

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

// Forward declaration — full definition is in supervisor_v2.h
enum class SystemState : uint8_t;

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

#define COMPONENT_TYPES_LIFECYCLE_STATUS_X(V) \
    V(Unknown) \
    V(Ready) \
    V(Failed) \
    V(Disabled)

#define COMPONENT_TYPES_LIFECYCLE_STATUS_ENUM(name) name,
enum class ComponentLifecycleStatus : uint8_t {
    COMPONENT_TYPES_LIFECYCLE_STATUS_X(COMPONENT_TYPES_LIFECYCLE_STATUS_ENUM)
};

inline const char* toString(ComponentLifecycleStatus status) {
    switch (status) {
#define COMPONENT_TYPES_LIFECYCLE_STATUS_STRING(name) case ComponentLifecycleStatus::name: return #name;
        COMPONENT_TYPES_LIFECYCLE_STATUS_X(COMPONENT_TYPES_LIFECYCLE_STATUS_STRING)
#undef COMPONENT_TYPES_LIFECYCLE_STATUS_STRING
    }
    return "UNKNOWN";
}

#undef COMPONENT_TYPES_LIFECYCLE_STATUS_ENUM
#undef COMPONENT_TYPES_LIFECYCLE_STATUS_X

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

struct ComponentRegistryEntry {
    ComponentLifecycleStatus lifeCycleStatus = ComponentLifecycleStatus::Unknown;
    bool isRequired = false;
    bool isDisabled = false;
    bool isRegistered = false;
    const char* lastFailureReason = nullptr;
};

struct ComponentStateMatrix {
    uint32_t minState;
    uint32_t maxState;
    uint32_t forwardTimeoutMs;
    uint32_t backwardTimeoutMs;
};

constexpr uint32_t TARGET_MODE = 0xFF;
