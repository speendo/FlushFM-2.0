#pragma once

#include <cstdint>
#include <cstddef>

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
