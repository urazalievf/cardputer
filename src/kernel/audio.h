// Microphone capture and speaker tones. The Cardputer's mic and speaker share
// one I2S peripheral, so exactly one of them can be live at a time.
#pragma once
#include "os.h"
#include <functional>

namespace audio {

// 16 kHz is what Whisper wants. 8 kHz halves the data rate and therefore
// doubles how long you can record, at some cost in transcription accuracy --
// on a board with no PSRAM that trade is often worth making.
uint32_t sampleRate();
void     setSampleRate(uint32_t hz);

// Bytes to keep free for whatever happens after the recording. A TLS handshake
// wants ~45KB; the Mac daemon over plain HTTP wants almost nothing.
void     setHeadroomBytes(size_t bytes);

// Declare that this recording streams to the card, which changes the memory
// trade completely and in both directions.
//
// Not streaming: the card is unmounted when the microphone starts, handing back
// ~28KB of driver and FATFS buffers -- nearly a second of extra recording, and
// the whole budget on a board this tight. Every byte is length.
//
// Streaming: the card must stay mounted, so that 28KB is gone; but the ring
// only has to cover how long one SD write takes, and it is freed before the
// upload's TLS handshake, so it needs neither the size nor the headroom.
void     setStreaming(bool on);
bool     streaming();

// Memory the mounted SD driver is holding that recording will free anyway,
// because claiming the microphone unmounts the card (shared GPIO40).
void     setSdReclaimable(size_t bytes);

void begin();

// True when the microphone is actually live, not merely wired up. M5Unified's
// Mic.isEnabled() only reports whether a data pin is configured -- it is true
// on a Cardputer from boot onwards even if the I2S port never came up, which
// makes a dead microphone look healthy right up until record() returns false
// and the memo comes back empty. This asks the driver whether the capture task
// is running, and re-probes rather than trusting a snapshot taken at boot.
bool micReady();

// Bring the microphone up, reporting whether it worked. Retries once with a
// full teardown, because a half-uninstalled I2S channel fails the first begin()
// and succeeds the second.
bool micOn();

// Empty while the last recordStart() is healthy; otherwise why it would not
// start, in words fit for a toast.
const char* startError();

// Why the last capture ended. "Full" is the ordinary end of a long memo;
// anything else is a fault worth showing the user.
enum class Stop : uint8_t {
    None,        // still running, or never started
    Full,        // the buffer filled -- normal
    MicFailed,   // I2S stopped delivering part-way through
    Stopped,     // the user stopped it
};
Stop  stopReason();
const char* stopText();

// Loudest sample seen during the last capture, 0.0-1.0. Distinguishes "the
// room was quiet" from "the microphone delivered nothing at all".
float peakLevel();

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

void speakerOn();

// Drop both audio paths. The microphone (PDM: data GPIO46, clock GPIO43) and
// the speaker (I2S_NUM_1: BCK 41, WS 43, DOUT 42) share GPIO43, so only one of
// them can be live; store::sdAcquire() also calls this before touching the
// card. The SD bus itself (SCK 40, MISO 39, MOSI 14, CS 12) shares no pin with
// either, so that second eviction is belt-and-braces rather than necessary.
void releaseI2S();
bool ownsI2S();

// Chunked capture so the caller can draw a level meter and stop on key release.
// recordStart() reports success; on failure startError() says why.
bool  recordStart();
bool  recordChunk();        // grabs ~100ms; false when it ends -- see stopReason()
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

// ---- streaming ----
// The capture buffer is a ring. Nothing drains it by default, so it fills and
// stops exactly as a plain linear buffer would -- but a caller that writes each
// chunk to the card as it arrives lifts the recording clean off the RAM
// ceiling, which is what caps a memo at four seconds otherwise.
//
// pendingChunk() hands back the oldest captured-but-unreleased chunk;
// releaseChunk() says it is safely on disk and its space may be reused.
const int16_t* pendingChunk(size_t* samples);
void   releaseChunk();
size_t pendingChunks();

// Total samples captured this recording, including any already written out and
// dropped from the ring. recordedSamples() is what is still in memory.
size_t capturedSamples();
float  capturedSeconds();

// True once the ring has wrapped, i.e. pcm() no longer holds the whole
// recording and only the streamed file does.
bool   wrapped();

// 44-byte canonical PCM WAV header for `pcmBytes` of 16-bit mono audio.
size_t wavHeader(uint8_t* out, size_t pcmBytes);

// Capture one short block straight into a caller-owned buffer, with no heap
// allocation and no recording state. The mic-check screen uses it to prove the
// I2S path works before anyone commits 128KB to a memo.
bool  sampleOnce(int16_t* buf, size_t samples);

// Play a WAV off the card without loading it. A streamed recording is far
// larger than the heap, so the only way to hear one back is a block at a time:
// two buffers alternate through the driver's two-slot queue. `onFrame` is
// called between blocks with progress 0..1 and returns false to stop early.
// Puts the microphone back when it finishes.
bool playWavFile(const String& path, std::function<bool(float)> onFrame);

void beep(uint16_t freq = 880, uint32_t ms = 60);
void chirpOk();
void chirpErr();

}  // namespace audio
