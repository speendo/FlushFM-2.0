#include "component_types.h"

const char* toString(TransitionStatus status) {
    switch (status) {
        case TransitionStatus::Completed:
            return "Completed";
        case TransitionStatus::Failed:
            return "Failed";
    }
    return "UNKNOWN";
}

const char* toString(ComponentLifecycleStatus status) {
    switch (status) {
        case ComponentLifecycleStatus::Unknown:
            return "Unknown";
        case ComponentLifecycleStatus::Ready:
            return "Ready";
        case ComponentLifecycleStatus::Failed:
            return "Failed";
        case ComponentLifecycleStatus::Disabled:
            return "Disabled";
    }
    return "UNKNOWN";
}
