#pragma once

#include "IAudioPlayer.h"
#include <Audio.h>

/// Thin wrapper around the ESP32-audioI2S Audio class.
///
/// Responsibilities:
/// - Hold the Audio instance and configure I2S pins on begin()
/// - Forward all IAudioPlayer calls to the underlying library
/// - Track volume locally (ESP32-audioI2S has no getter)
///
/// Note: ESP32-audioI2S v3.4.x uses Audio::audio_info_callback
/// (Audio::msg_t). Callback registration belongs to the application
/// wiring layer (src/audio_callbacks.cpp), not this wrapper.
class AudioPlayerESP32 : public IAudioPlayer {
public:
    /// @param bck  GPIO for I2S bit clock  (BCK)
    /// @param ws   GPIO for I2S word select (WS / LRCK)
    /// @param dout GPIO for I2S data out    (DOUT / DIN on DAC)
    AudioPlayerESP32(int bck, int ws, int dout);

    bool    begin()                        override;
    void    loop()                         override;
    bool    connectToHost(const char* url) override;
    void    stop()                         override;
    void    setVolume(uint8_t volume)      override;
    void    setVolumeSteps(uint8_t steps)  override;
    uint8_t getVolume() const              override;
    void    setBalance(int8_t balance)     override;
    RuntimeState runtimeState() const      override;

private:
    int     _bck;
    int     _ws;
    int     _dout;
    uint8_t _volume = 0;
    RuntimeState _runtimeState = RuntimeState::IDLE;
    Audio   _audio;
};
