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

    void setMute(bool mute) override {
        ++setMuteCalls;
        currentMute = mute;
    }

    bool getMute() override {
        return currentMute;
    }

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
        return currentRuntimeState;
    }

    int connectCalls = 0;
    int stopCalls = 0;
    int setMuteCalls = 0;
    int setVolumeCalls = 0;
    int setBalanceCalls = 0;
    const char* lastUrl = nullptr;
    uint8_t currentVolume = 7;
    int8_t currentBalance = 0;
    bool currentMute = false;
    RuntimeState currentRuntimeState = RuntimeState::IDLE;
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

    void saveStation(const char* stationUrl) override {
        ++saveStationCalls;
        lastStation = stationUrl;
    }

    const char* loadStation() override {
        return persistedStation ? persistedStation : "";
    }

    void forgetSettings() override {
        ++forgetSettingsCalls;
    }

    void resetSession() override {
        ++resetSessionCalls;
    }

    cli_command_logic::WiFiConnectivity wifiConnectivity() const override {
        return connectivity;
    }

    cli_command_logic::AudioState audioState() const override {
        return currentAudioState;
    }

    const char* getPersistedStation() const override {
        return persistedStation ? persistedStation : "";
    }

    cli_command_logic::WiFiConnectivity connectivity = cli_command_logic::WiFiConnectivity::DISCONNECTED;
    cli_command_logic::AudioState currentAudioState = cli_command_logic::AudioState::IDLE;
    int setSsidCalls = 0;
    int setPassCalls = 0;
    int connectWiFiCalls = 0;
    int saveStationCalls = 0;
    int forgetSettingsCalls = 0;
    int resetSessionCalls = 0;
    const char* lastSsid = nullptr;
    const char* lastPass = nullptr;
    const char* lastStation = nullptr;
    const char* persistedStation = nullptr;
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
    TEST_ASSERT_EQUAL(0, env.saveStationCalls);
}

void test_play_command_with_wifi_requests_transition_and_persists_station() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    env.connectivity = cli_command_logic::WiFiConnectivity::CONNECTED;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "play",
        "http://example.com/play.mp3",
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::CONNECTING_STREAM), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.connectCalls);
    TEST_ASSERT_EQUAL(1, env.saveStationCalls);
    TEST_ASSERT_EQUAL_STRING("http://example.com/play.mp3", env.lastStation);
}

void test_switch_command_is_not_supported_anymore() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    env.connectivity = cli_command_logic::WiFiConnectivity::CONNECTED;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "switch",
        "http://example.com/switch.mp3",
        audio,
        env,
        255);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::NONE), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.stopCalls);
    TEST_ASSERT_EQUAL(0, audio.connectCalls);
    TEST_ASSERT_EQUAL(0, env.saveStationCalls);
}

void test_forget_command_clears_persisted_settings() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "forget",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::SETTINGS_FORGOTTEN), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(1, env.forgetSettingsCalls);
}

void test_reset_command_resets_runtime_session_without_direct_stop_call() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "reset",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::SESSION_RESET), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.stopCalls);
    TEST_ASSERT_EQUAL(1, env.resetSessionCalls);
}

void test_play_without_url_loads_persisted_station() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    env.connectivity = cli_command_logic::WiFiConnectivity::CONNECTED;
    env.persistedStation = "http://persisted.stream/music";

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "play",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::CONNECTING_STREAM), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.connectCalls);
    TEST_ASSERT_EQUAL(1, env.saveStationCalls);
}

void test_play_without_url_fails_if_no_persisted_station() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    env.connectivity = cli_command_logic::WiFiConnectivity::CONNECTED;
    env.persistedStation = nullptr;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "play",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::USAGE_PLAY), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.connectCalls);
}

void test_mute_without_arg_returns_current_state() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    audio.currentMute = true;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "mute",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::MUTE_CURRENT), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(1, result.value);
    TEST_ASSERT_EQUAL(0, audio.setMuteCalls);
}

void test_mute_on_sets_muted_state() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "mute",
        "on",
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::MUTE_SET), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(1, audio.setMuteCalls);
    TEST_ASSERT_TRUE(audio.currentMute);
}

void test_mute_off_sets_unmuted_state() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    audio.currentMute = true;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "mute",
        "off",
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::MUTE_SET), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(1, audio.setMuteCalls);
    TEST_ASSERT_FALSE(audio.currentMute);
}

void test_mute_invalid_arg_returns_usage() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "mute",
        "toggle",
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::USAGE_MUTE), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.setMuteCalls);
}

void test_status_shows_connected_and_streaming_state() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    env.connectivity = cli_command_logic::WiFiConnectivity::CONNECTED;
    env.currentAudioState = cli_command_logic::AudioState::STREAMING;
    env.persistedStation = "http://example.com/stream.mp3";

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "status",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::STATUS), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(1, result.aux & 0x01);  // WiFi connected (bit 0)
    TEST_ASSERT_EQUAL(2, (result.aux >> 1) & 0x03);  // Audio STREAMING (bits 1-2 = 2)
    TEST_ASSERT_EQUAL_STRING("http://example.com/stream.mp3", result.text);
}

void test_status_shows_disconnected_and_idle_state() {
    FakeAudioPlayer audio;
    FakeEnvironment env;
    env.connectivity = cli_command_logic::WiFiConnectivity::DISCONNECTED;
    env.currentAudioState = cli_command_logic::AudioState::IDLE;
    env.persistedStation = nullptr;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "status",
        nullptr,
        audio,
        env,
        21);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::STATUS), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, result.aux & 0x01);  // WiFi disconnected
    TEST_ASSERT_EQUAL(0, (result.aux >> 1) & 0x03);  // Audio IDLE
    TEST_ASSERT_EQUAL_STRING("", result.text);
}

void test_volume_command_accepts_high_values_with_max_255() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "volume",
        "255",
        audio,
        env,
        255);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::VOLUME_SET), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(1, audio.setVolumeCalls);
    TEST_ASSERT_EQUAL(255, audio.currentVolume);
}

void test_volume_command_rejects_values_above_max() {
    FakeAudioPlayer audio;
    FakeEnvironment env;

    const cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        "volume",
        "256",
        audio,
        env,
        255);

    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::VOLUME_OUT_OF_RANGE), static_cast<int>(result.key));
    TEST_ASSERT_EQUAL(0, audio.setVolumeCalls);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_play_command_requires_wifi_and_does_not_start_stream);
    RUN_TEST(test_play_command_with_wifi_requests_transition_and_persists_station);
    RUN_TEST(test_switch_command_is_not_supported_anymore);
    RUN_TEST(test_forget_command_clears_persisted_settings);
    RUN_TEST(test_reset_command_resets_runtime_session_without_direct_stop_call);
    RUN_TEST(test_play_without_url_loads_persisted_station);
    RUN_TEST(test_play_without_url_fails_if_no_persisted_station);
    RUN_TEST(test_mute_without_arg_returns_current_state);
    RUN_TEST(test_mute_on_sets_muted_state);
    RUN_TEST(test_mute_off_sets_unmuted_state);
    RUN_TEST(test_mute_invalid_arg_returns_usage);
    RUN_TEST(test_status_shows_connected_and_streaming_state);
    RUN_TEST(test_status_shows_disconnected_and_idle_state);
    RUN_TEST(test_volume_command_accepts_high_values_with_max_255);
    RUN_TEST(test_volume_command_rejects_values_above_max);
    return UNITY_END();
}
