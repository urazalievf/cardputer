// Microphone capture and speaker tones. The Cardputer's mic and speaker share
// one I2S peripheral, so exactly one of them can be live at a time.
#pragma once
#include "os.h"

namespace audio {

// 16 kHz is what Whisper wants. 8 kHz halves the data rate and therefore
// doubles how long you can record, at some cost in transcription accuracy --
// on a board with no PSRAM that trade is often worth making.
uint32_t sampleRate();
void     setSampleRate(uint32_t hz);

// Bytes to keep free for whatever happens after the recording. A TLS handshake
// wants ~45KB; the Mac daemon over plain HTTP wants almost nothing.
void     setHeadroomBytes(size_t bytes);

void begin();
bool micReady();

// The capture buffer is ~128KB, far too much to hold at rest on a board with
// no PSRAM. It is allocated when recording starts and freed once the samples
// have been consumed, which is what makes room for the UI canvas and BLE.
bool allocBuffer();
void freeBuffer();
bool bufferHeld();

// Memory another subsystem will free before we record (the UI canvas). Counted
// into the capacity estimate so the app can promise a realistic length.
void setReclaimableBytes(size_t bytes);
size_t capacitySamples();
uint32_t capacitySeconds();

void micOn();
void speakerOn();

// Hand GPIO40 back to the SD card. The Cardputer wires the I2S bit clock and
// the SD clock to the same pin, so only one of them can be live.
void releaseI2S();
bool ownsI2S();

// Chunked capture so the caller can draw a level meter and stop on key release.
void  recordStart();
bool  recordChunk();        // grabs ~100ms; false when the buffer is full
void  recordStop();
bool  recording();
size_t recordedSamples();
float recordedSeconds();
float level();              // 0.0 - 1.0, RMS of the last chunk

// A scrolling envelope of the last few seconds, for drawing a live waveform.
// Each chunk contributes SUBS points, so the trace moves smoothly rather than
// stepping once per 100ms.
static const int WAVE_POINTS = 240;
int   waveCount();
float waveAt(int i);        // 0 = oldest visible, waveCount()-1 = newest
void  waveClear();

const int16_t* pcm();
void clear();

// 44-byte canonical PCM WAV header for `pcmBytes` of 16-bit mono audio.
size_t wavHeader(uint8_t* out, size_t pcmBytes);

void beep(uint16_t freq = 880, uint32_t ms = 60);
void chirpOk();
void chirpErr();

}  // namespace audio
