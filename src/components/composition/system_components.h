#pragma once

#include <cstddef>
#include <cstdint>

#include "component_types.h"

class SupervisorV2;
class IAudioPlayer;

namespace audio_runtime {
enum class Signal;
}

/** @brief Abstract base class for all system components.
 *
 *  Owns the ComponentMailbox through which the supervisor pushes state targets.
 *  Provides a non-virtual loop() that reads the mailbox, dispatches to the
 *  matching handleX() method, then calls poll() for async work.
 *
 *  Every component must override all nine pure virtual methods -- the seven
 *  handleX() state handlers, poll(), and onTransitionTimeout().
 */
class ISystemComponent {
public:
    /** @param componentId   Unique component identifier from ComponentID enum.
     *  @param componentName  Human-readable name for logging.
     *  @param isRequired     true if the supervisor should escalate to ERROR
     *                        when this component fails a transition. */
    explicit ISystemComponent(ComponentID componentId, const char* componentName, bool isRequired)
        : id_(componentId), name_(componentName), isRequired_(isRequired) {}
    virtual ~ISystemComponent() = default;

    /** @return The component's unique identifier. */
    ComponentID id() const { return id_; }

    /** @return The component's human-readable name. */
    const char* name() const { return name_; }

    /** @return true if this component is required for system quorum. */
    bool isRequired() const { return isRequired_; }

    /** @return Reference to the mailbox the supervisor writes state targets into. */
    ComponentMailbox& mailbox() { return mailbox_; }

    /** @brief One-time boot initialisation. Called once before loop().
     *  @return true on success, false on failure. */
    virtual bool setup() = 0;

    /** @brief Non-virtual loop tick. Reads the mailbox, dispatches to the
     *  matching handleX(), then calls poll(). Components must not override. */
    void loop();

protected:
    /** @name State handlers
     *  Called by dispatch() when the supervisor posts a corresponding state.
     *  Each must eventually call completeTransition(). Implementations must
     *  not block -- async work is checked in poll(). */
    ///@{
    virtual void handleBOOTING() = 0;
    virtual void handleSLEEP() = 0;
    virtual void handleCONNECTING() = 0;
    virtual void handleREADY() = 0;
    virtual void handleLIVE() = 0;
    virtual void handleERROR() = 0;
    virtual void handleFATAL() = 0;
    ///@}

    /** @brief Async work hook. Called every loop tick after dispatch.
     *  Override to check progress on async transitions (WiFi connect,
     *  audio streaming). Empty implementation for sync components. */
    virtual void poll() = 0;

    /** @brief Called by the supervisor when this component's transition times out.
     *  @param transitionId  The transition that timed out. */
    virtual void onTransitionTimeout(uint32_t transitionId) = 0;

    /** @brief Register this component with the supervisor.
     *  Must be called from setup() before any state transitions begin.
     *  @param supervisor  Reference to the global SupervisorV2 instance. */
    void registerWithSupervisor(SupervisorV2& supervisor);

    /** @brief Signal transition completion to the supervisor.
     *  Safe to call from handleX() (sync) or from poll() / callbacks (async). */
    void completeTransition(TransitionStatus status);

private:
    /** @brief Switch on target state and call the matching handleX(). */
    void dispatch(SystemState target);

    const ComponentID id_;
    const char* name_;
    const bool isRequired_;
    ComponentMailbox mailbox_;
};

// ---------------------------------------------------------------------------
// Concrete components
// ---------------------------------------------------------------------------

/** @brief Synchronous component that prints board info on startup.
 *  All handleX() complete immediately. None are required for quorum. */
class BoardInfoComponent final : public ISystemComponent {
public:
    BoardInfoComponent();
    bool setup() override;
    void handleBOOTING() override;
    void handleSLEEP() override;
    void handleCONNECTING() override;
    void handleREADY() override;
    void handleLIVE() override;
    void handleERROR() override;
    void handleFATAL() override;
    void poll() override;
    void onTransitionTimeout(uint32_t) override {}
};

/** @brief Handles WiFi connectivity. Required for quorum.
 *  handleLIVE() and handleCONNECTING() are async -- they initiate a
 *  connection and poll() / callbacks report completion. */
class WiFiComponent final : public ISystemComponent {
public:
    WiFiComponent();
    bool setup() override;
    void handleBOOTING() override;
    void handleSLEEP() override;
    void handleCONNECTING() override;
    void handleREADY() override;
    void handleLIVE() override;
    void handleERROR() override;
    void handleFATAL() override;
    void poll() override;
    void onTransitionTimeout(uint32_t transitionId) override;

private:
    /** @brief Ensure wifi is connecting.
     *  Opportunistic when called from setup() (no flags set).
     *  Mandatory when called from handleCONNECTING()/handleLIVE()
     *  (transitionPending_ already set -- poll() tracks result). */
    void ensureConnected();

    static void onConnected(void* context);
    static void onDisconnected(void* context);
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
};

/** @brief Manages audio streaming via IAudioPlayer. Required for quorum.
 *  handleLIVE() and handleCONNECTING() are async -- they initiate a
 *  stream and poll() / audio signal callbacks report completion. */
class AudioRuntimeComponent final : public ISystemComponent {
public:
    explicit AudioRuntimeComponent(IAudioPlayer* audio);
    bool setup() override;
    void handleBOOTING() override;
    void handleSLEEP() override;
    void handleCONNECTING() override;
    void handleREADY() override;
    void handleLIVE() override;
    void handleERROR() override;
    void handleFATAL() override;
    void poll() override;
    void onTransitionTimeout(uint32_t transitionId) override;

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
    bool pendingErrorTarget_ = false;
    IAudioPlayer* audio_;
};

/** @brief Serial CLI interface. Optional component -- not required for quorum.
 *  All handleX() complete immediately. poll() reads serial input for
 *  cli::readLine() / cli::process(). */
class CliComponent final : public ISystemComponent {
public:
    explicit CliComponent(IAudioPlayer* audio);
    bool setup() override;
    void handleBOOTING() override;
    void handleSLEEP() override;
    void handleCONNECTING() override;
    void handleREADY() override;
    void handleLIVE() override;
    void handleERROR() override;
    void handleFATAL() override;
    void poll() override;
    void onTransitionTimeout(uint32_t) override {}

private:
    IAudioPlayer* audio_;
};
