#include "AudioPlayerESP32.h"

AudioPlayerESP32::AudioPlayerESP32(int bck, int ws, int dout)
    : _bck(bck), _ws(ws), _dout(dout) {}

bool AudioPlayerESP32::begin() {
    _audio.setPinout(_bck, _ws, _dout);
    _audio.setVolume(_volume);
    return true;  // ESP32-audioI2S setPinout has no error return value
}

void AudioPlayerESP32::loop() {
    _audio.loop();
}

bool AudioPlayerESP32::connectToHost(const char* url) {
    return _audio.connecttohost(url);
}

void AudioPlayerESP32::stop() {
    _audio.stopSong();
}

void AudioPlayerESP32::setVolume(uint8_t volume) {
    _volume = volume;
    _audio.setVolume(volume);
}

void AudioPlayerESP32::setVolumeSteps(uint8_t steps) {
    _audio.setVolumeSteps(steps);
}

uint8_t AudioPlayerESP32::getVolume() const {
    return _volume;
}

void AudioPlayerESP32::setBalance(int8_t balance) {
    _audio.setBalance(balance);
}
