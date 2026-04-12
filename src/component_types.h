#pragma once

#include <cstdint>

enum class TransitionStatus : uint8_t {
    Completed,
    Failed,
};

enum class ComponentLifecycleStatus : uint8_t {
    Unknown,
    Ready,
    Failed,
    Disabled,
};

using DebugReason = const char*;

const char* toString(TransitionStatus status);
const char* toString(ComponentLifecycleStatus status);
