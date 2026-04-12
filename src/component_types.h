#pragma once

#include <cstdint>
#include <cstddef>

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

constexpr size_t kMaxComponentNameLen = 256;
constexpr size_t kMaxFailureReasonLen = 512;

struct ComponentRegistryEntry {
    ComponentLifecycleStatus lifeCycleStatus = ComponentLifecycleStatus::Unknown;
    bool isRequired = false;
    bool isDisabled = false;
    char lastFailureReason[kMaxFailureReasonLen + 1] = {};
};

const char* toString(TransitionStatus status);
const char* toString(ComponentLifecycleStatus status);
