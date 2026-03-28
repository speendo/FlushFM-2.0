#include <unity.h>

#include "IAudioPlayer.h"
#include "cli_command_logic.h"

namespace {

class FakeAudioPlayer final : public IAudioPlayer {
public:
    bool begin() override { return true; }
    void loop() override {}

    bool connectToHost(const char* url) override {
        ++connectCalls;
        lastUrl = url;
        return true;
    }

    void stop() override { ++stopCalls; }

    void setVolume(uint8_t volume) override {
        ++setVolumeCalls;
        currentVolume = volume;
    }

    void setVolumeSteps(uint8_t) override {}

    uint8_t getVolume() const override {
        return currentVolume;
    }

    void setBalance(int8_t balance) override {
        ++setBalanceCalls;
        currentBalance = balance;
    }

    RuntimeState runtimeState() const override {
        return RuntimeState::IDLE;
    }

    int connectCalls = 0;
    int stopCalls = 0;
    int setVolumeCalls = 0;
    int setBalanceCalls = 0;
    const char* lastUrl = nullptr;
    uint8_t currentVolume = 7;
    int8_t currentBalance = 0;
};

class FakeEnvironment final : public cli_command_logic::IEnvironment {
public:
    void setSsid(const char* ssid) override {
        lastSsid = ssid;
        ++setSsidCalls;
    }

    void setPass(const char* pass) override {
        lastPass = pass;
        ++setPassCalls;
    }

    void connectWiFi() override {
        ++connectWiFiCalls;
    }

    cli_command_logic::WiFiConnectivity wifiConnectivity() const override {
        return connectivity;
    }

    cli_command_logic::WiFiConnectivity connectivity = cli_command_logic::WiFiConnectivity::DISCONNECTED;
    int setSsidCalls = 0;
    int setPassCalls = 0;
    int connectWiFiCalls = 0;
    const char* lastSsid = nullptr;
    const char* lastPass = nullptr;
};

} // namespace

void test_play_command_requires_wifi_and_does_not_start_stream() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "play",
        "http://example.com/stream.mp3",
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::WIFI_REQUIRED), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.connectCalls);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_play_command_requires_wifi_and_does_not_start_stream);
    return UNITY_END();
}
