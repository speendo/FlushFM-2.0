// component_support.cpp – minimal implementations for LightSensorComponent tests
#include "components/composition/system_components.h"
#include "../../lib/LightSensor/ILightSensor.h"
#include "supervisor/supervisor.h"

extern Supervisor s_supervisor;

// -- ISystemComponent base class --------------------------------------------

void ISystemComponent::loop() {
    SystemState target;
    if (mailbox_.consumeNextState(target)) {
        dispatch(target);
    }
    poll();
}

void ISystemComponent::dispatch(SystemState target) {
    switch (target) {
        case SystemState::BOOTING:    handleBOOTING(); break;
        case SystemState::SLEEP:      handleSLEEP(); break;
        case SystemState::CONNECTING: handleCONNECTING(); break;
        case SystemState::READY:      handleREADY(); break;
        case SystemState::LIVE:       handleLIVE(); break;
        case SystemState::ERROR:      handleERROR(); break;
        case SystemState::FATAL:      handleFATAL(); break;
    }
}

void ISystemComponent::registerWithSupervisor(Supervisor& supervisor) {
    supervisor.registerComponent(id_, &mailbox_, isRequired_);
}

void ISystemComponent::completeTransition(TransitionStatus status) {
    s_supervisor.completeTransition(id_, status);
}

// -- LightSensorComponent ---------------------------------------------------

LightSensorComponent::LightSensorComponent(ILightSensor& sensor)
    : ISystemComponent(ComponentID::LightSensor, "LightSensor", true)
    , sensor_(sensor)
{}

bool LightSensorComponent::setup() {
    sensor_.begin();
    registerWithSupervisor(s_supervisor);
    return true;
}

void LightSensorComponent::handleBOOTING()    { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleCONNECTING() { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleREADY()      { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleLIVE()       { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleERROR()      { completeTransition(TransitionStatus::Completed); }
void LightSensorComponent::handleFATAL()      { completeTransition(TransitionStatus::Completed); }

void LightSensorComponent::handleSLEEP() {
#if LIGHT_SENSOR_WAKE_ENABLED
    sensor_.configureUlpWake();
#endif
    completeTransition(TransitionStatus::Completed);
}

void LightSensorComponent::poll() {
    sensor_.poll();

    const bool current = sensor_.isLightOn();

    if (!baselineEstablished_) {
        lastLightState_ = current;
        baselineEstablished_ = true;
        return;
    }

    if (current == lastLightState_) return;

    lastLightState_ = current;

    if (current) {
        requestState(SystemState::LIVE);
    } else {
        requestState(SystemState::READY);
    }
}

void LightSensorComponent::requestState(SystemState target) {
    s_supervisor.postStateRequest(target);
}
