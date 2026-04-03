#include "AudioPlayerESP32.h"

AudioPlayerESP32::AudioPlayerESP32(int bck, int ws, int dout)
    : _bck(bck), _ws(ws), _dout(dout), _audio(std::make_unique<Audio>()) {}

bool AudioPlayerESP32::begin() {
    _audio->setPinout(_bck, _ws, _dout);
    _audio->setVolume(_volume);
    setMute(false);
    _runtimeState = RuntimeState::IDLE;
    return true;  // ESP32-audioI2S setPinout has no error return value
}

void AudioPlayerESP32::loop() {
    _audio->loop();
}

bool AudioPlayerESP32::connectToHost(const char* url) {
    setMute(false);
    _runtimeState = RuntimeState::CONNECTING;
    const bool ok = _audio->connecttohost(url);
    _runtimeState = ok ? RuntimeState::STREAMING : RuntimeState::ERROR;
    return ok;
}

void AudioPlayerESP32::stop() {
    setMute(true);
    _audio->stopSong();
    _runtimeState = RuntimeState::IDLE;
}

void AudioPlayerESP32::setMute(bool mute) {
    _audio->setMute(mute);
}

bool AudioPlayerESP32::getMute() {
    return _audio->getMute();
}

void AudioPlayerESP32::setVolume(uint8_t volume) {
    _volume = volume;
    _audio->setVolume(volume);
}

void AudioPlayerESP32::setVolumeSteps(uint8_t steps) {
    _audio->setVolumeSteps(steps);
}

uint8_t AudioPlayerESP32::getVolume() const {
    return _volume;
}

void AudioPlayerESP32::setBalance(int8_t balance) {
    _audio->setBalance(balance);
}

IAudioPlayer::RuntimeState AudioPlayerESP32::runtimeState() const {
    return _runtimeState;
}
