#pragma once

#include "IAudioPlayer.h"

class ISystemComponent {
public:
    virtual ~ISystemComponent() = default;
    virtual const char* name() const = 0;
    virtual bool setup() = 0;
    virtual void loop() {}
};

class BoardInfoComponent final : public ISystemComponent {
public:
    const char* name() const override;
    bool setup() override;
};

class WiFiComponent final : public ISystemComponent {
public:
    const char* name() const override;
    bool setup() override;
};

class AudioRuntimeComponent final : public ISystemComponent {
public:
    explicit AudioRuntimeComponent(IAudioPlayer& audio);

    const char* name() const override;
    bool setup() override;

private:
    IAudioPlayer& audio_;
};

class CliComponent final : public ISystemComponent {
public:
    explicit CliComponent(IAudioPlayer& audio);

    const char* name() const override;
    bool setup() override;
    void loop() override;

private:
    IAudioPlayer& audio_;
};
