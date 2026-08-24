// Microphone capture and speaker tones. The Cardputer's mic and speaker share
// one I2S peripheral, so exactly one of them can be live at a time.
#pragma once
#include "os.h"

namespace audio {

static const uint32_t SAMPLE_RATE = 16000;   // what Whisper wants

void begin();
bool micReady();
size_t capacitySamples();
uint32_t capacitySeconds();

void micOn();
void speakerOn();

// Chunked capture so the caller can draw a level meter and stop on key release.
void  recordStart();
bool  recordChunk();        // grabs ~100ms; false when the buffer is full
void  recordStop();
bool  recording();
size_t recordedSamples();
float recordedSeconds();
float level();              // 0.0 - 1.0, RMS of the last chunk

const int16_t* pcm();
void clear();

// 44-byte canonical PCM WAV header for `pcmBytes` of 16-bit mono audio.
size_t wavHeader(uint8_t* out, size_t pcmBytes);

void beep(uint16_t freq = 880, uint32_t ms = 60);
void chirpOk();
void chirpErr();

}  // namespace audio
