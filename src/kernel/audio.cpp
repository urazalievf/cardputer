#include "audio.h"
#include "store.h"
#include <esp_heap_caps.h>
#include <string.h>

namespace audio {

static int16_t* s_buf = nullptr;
static size_t   s_cap = 0;        // samples
static size_t   s_used = 0;
static bool     s_micReady = false;
static bool     s_recording = false;
static float    s_level = 0.0f;
static bool     s_micOwnsI2S = false;

static uint32_t s_rate = 16000;
static size_t   s_headroom = 72 * 1024;
static size_t   s_sdReclaim = 0;
static float    s_peak = 0.0f;
static Stop     s_stop = Stop::None;
static const char* s_startErr = "";

uint32_t sampleRate() { return s_rate; }
void setSampleRate(uint32_t hz) { s_rate = (hz == 8000 || hz == 16000) ? hz : 16000; }
void setHeadroomBytes(size_t bytes) { s_headroom = bytes; }
void setSdReclaimable(size_t bytes) { s_sdReclaim = bytes; }

// 100ms of audio, whatever the rate.
static size_t chunkSamples() { return s_rate / 10; }
static size_t s_reclaimable = 0;

// Ring buffer of envelope points. Cheap: 240 floats, written once per sub-block.
static float s_wave[WAVE_POINTS] = {0};
static int   s_waveHead = 0;
static int   s_waveFill = 0;
static const int SUBS = 8;            // envelope points per 100ms chunk

static void wavePush(float v) {
    s_wave[s_waveHead] = v;
    s_waveHead = (s_waveHead + 1) % WAVE_POINTS;
    if (s_waveFill < WAVE_POINTS) s_waveFill++;
}

void setReclaimableBytes(size_t bytes) { s_reclaimable = bytes; }

// How much we could capture if we asked right now. Reported before any
// allocation so the UI can promise a realistic ceiling.
static size_t plannedSamples() {
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t want = s_rate * 30;
    if (psram >= want * sizeof(int16_t)) return want;
    // Leave enough for a TLS handshake (~45KB) plus slack; the caller releases
    // the canvas first, so count that back in.
    size_t avail = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) + s_reclaimable + s_sdReclaim;
    size_t bytes = avail > s_headroom ? avail - s_headroom : 0;
    if (bytes > 200 * 1024) bytes = 200 * 1024;
    return bytes / sizeof(int16_t);
}

bool allocBuffer() {
    if (s_buf) return true;
    size_t want = s_rate * 30;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= want * sizeof(int16_t)) {
        s_buf = (int16_t*)heap_caps_malloc(want * sizeof(int16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_buf) { s_cap = want; return true; }
    }

    // plannedSamples() is the advertised ceiling and counts memory the caller
    // is *expected* to hand back (the UI canvas). Allocation has to work from
    // what is actually free right now, and from the largest contiguous block
    // rather than the total, or a fragmented heap fails a request that "fits".
    size_t total = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t budget = total > s_headroom ? total - s_headroom : 0;
    size_t bytes = largest < budget ? largest : budget;
    if (bytes > 200 * 1024) bytes = 200 * 1024;

    // Back off rather than give up: a shorter memo beats no memo.
    while (bytes >= s_rate * sizeof(int16_t)) {
        s_buf = (int16_t*)malloc(bytes);
        if (s_buf) { s_cap = bytes / sizeof(int16_t); return true; }
        bytes = bytes / 4 * 3;
    }
    s_cap = 0;
    return false;
}

void freeBuffer() {
    if (!s_buf) return;
    free(s_buf);
    s_buf = nullptr;
    s_cap = 0;
    s_used = 0;
}

bool bufferHeld() { return s_buf != nullptr; }

void begin() {
    bool ready = micOn();
    // Idle state favours the SD card: most apps read files, few record audio.
    // The verdict outlives the release -- the hardware works, it is just not
    // claimed right now, which is what micReady() means.
    releaseI2S();
    s_micReady = ready;
    os::logf("audio: microphone %s", ready ? "ready" : "UNAVAILABLE");
}

// Mic.isEnabled() only says a data pin is configured, which is true on a
// Cardputer from M5.begin() onward whether or not the I2S port ever came up.
// Mic.isRunning() is the honest test, and s_micReady caches the last real
// begin() result for the case where the port is deliberately released.
bool micReady() { return M5Cardputer.Mic.isRunning() || s_micReady; }

Stop stopReason() { return s_stop; }
float peakLevel() { return s_peak; }
const char* startError() { return s_startErr; }

const char* stopText() {
    switch (s_stop) {
        case Stop::Full:      return "buffer full";
        case Stop::MicFailed: return "the microphone stopped delivering";
        case Stop::Stopped:   return "stopped";
        default:              return "";
    }
}
size_t capacitySamples() { return s_buf ? s_cap : plannedSamples(); }
uint32_t capacitySeconds() { return capacitySamples() / s_rate; }

// GPIO40 is the I2S bit clock AND the SD clock. Any audio path has to evict
// the SD card first, and store::sdAcquire() evicts audio in the other
// direction. s_haveI2S tracks whether we currently hold the pin at all.
static bool s_haveI2S = false;

bool ownsI2S() { return s_haveI2S; }

void releaseI2S() {
    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.end();
    delay(40);
    s_haveI2S = false;
    s_micOwnsI2S = false;
}

bool micOn() {
    // Believing our own bookkeeping is how one failed begin() becomes a
    // permanently dead microphone: the flags say "on", every later call
    // early-returns, and record() quietly returns false forever. Ask the driver.
    if (s_haveI2S && s_micOwnsI2S && M5Cardputer.Mic.isRunning()) return true;

    store::sdRelease();
    // tone() is asynchronous; ending the port underneath a playing buffer is
    // what leaves the channel half-torn-down for the next begin().
    if (M5Cardputer.Speaker.isPlaying()) {
        M5Cardputer.Speaker.stop();
        for (int i = 0; i < 20 && M5Cardputer.Speaker.isPlaying(); i++) delay(5);
    }
    M5Cardputer.Speaker.end();
    delay(60);

    bool ok = M5Cardputer.Mic.begin();
    delay(60);
    if (!ok) {
        // A channel left installed by an interrupted teardown fails the first
        // begin() and takes the second. 150ms is cheap next to a lost memo.
        os::logf("audio: Mic.begin() failed, retrying after a full teardown");
        M5Cardputer.Mic.end();
        M5Cardputer.Speaker.end();
        delay(90);
        ok = M5Cardputer.Mic.begin();
        delay(60);
    }

    s_micOwnsI2S = ok;
    s_haveI2S = ok;
    s_micReady = ok;
    if (!ok) os::logf("audio: microphone unavailable - I2S0 PDM did not start");
    return ok;
}

void speakerOn() {
    if (s_haveI2S && !s_micOwnsI2S && M5Cardputer.Speaker.isRunning()) return;
    store::sdRelease();
    M5Cardputer.Mic.end();
    delay(60);
    M5Cardputer.Speaker.begin();
    delay(60);
    s_micOwnsI2S = false;
    s_haveI2S = true;
}

void clear() { s_used = 0; s_level = 0.0f; s_peak = 0.0f; waveClear(); }

int waveCount() { return s_waveFill; }

float waveAt(int i) {
    if (i < 0 || i >= s_waveFill) return 0.0f;
    int start = (s_waveHead - s_waveFill + WAVE_POINTS) % WAVE_POINTS;
    return s_wave[(start + i) % WAVE_POINTS];
}

void waveClear() {
    s_waveHead = 0;
    s_waveFill = 0;
    for (int i = 0; i < WAVE_POINTS; i++) s_wave[i] = 0.0f;
}

bool recordStart() {
    waveClear();
    s_startErr = "";
    s_stop = Stop::None;
    s_peak = 0.0f;

    // micOn() evicts the SD card, which hands ~28KB of driver and FATFS
    // buffers back. Allocating before that throws away a second of recording.
    if (!micOn()) {
        s_startErr = "microphone did not start";
        s_recording = false;
        return false;
    }
    if (!allocBuffer()) {
        s_startErr = "not enough memory to record";
        s_recording = false;
        return false;
    }
    s_used = 0;
    s_level = 0.0f;
    // Not s_micReady: that is a cached verdict. This has to be true right now.
    s_recording = M5Cardputer.Mic.isRunning();
    if (!s_recording) {
        s_startErr = "microphone stopped before the first chunk";
        freeBuffer();
        return false;
    }
    return true;
}

bool recordChunk() {
    if (!s_recording || !s_buf) return false;
    size_t room = s_cap - s_used;
    if (room < chunkSamples()) { s_recording = false; s_stop = Stop::Full; return false; }

    // record() only returns false when its lazy begin() fails, i.e. the I2S
    // port went away underneath us. Say so rather than letting the caller
    // report an empty recording as "too short".
    if (!M5Cardputer.Mic.record(s_buf + s_used, chunkSamples(), s_rate)) {
        os::logf("audio: Mic.record() failed at %.1fs", (double)s_used / s_rate);
        s_recording = false;
        s_stop = Stop::MicFailed;
        return false;
    }
    // The mic task fills the slot asynchronously; bail out rather than spin
    // forever if it never does.
    uint32_t deadline = millis() + 500;
    while (M5Cardputer.Mic.isRecording()) {
        if (millis() > deadline) {
            os::logf("audio: mic task stalled at %.1fs", (double)s_used / s_rate);
            s_recording = false;
            s_stop = Stop::MicFailed;
            return false;
        }
        delay(2);
    }

    // Envelope in SUBS slices so the waveform scrolls smoothly, plus the
    // whole-chunk RMS for the coarse level readout.
    const size_t sub = chunkSamples() / SUBS;
    uint64_t total = 0;
    for (int s = 0; s < SUBS; s++) {
        uint64_t sum = 0;
        for (size_t i = 0; i < sub; i++) {
            int32_t v = s_buf[s_used + s * sub + i];
            sum += (uint64_t)(v * v);
        }
        total += sum;
        float subRms = sqrtf((float)sum / sub) / 6000.0f;
        wavePush(subRms > 1.0f ? 1.0f : subRms);
    }
    float rms = sqrtf((float)total / (sub * SUBS));
    s_level = rms / 6000.0f;
    if (s_level > 1.0f) s_level = 1.0f;

    // Peak, not RMS: it is the one number that separates "the room was quiet"
    // from "the microphone handed us a buffer of zeroes".
    for (size_t i = 0; i < chunkSamples(); i++) {
        int32_t v = s_buf[s_used + i];
        if (v < 0) v = -v;
        float f = (float)v / 32767.0f;
        if (f > s_peak) s_peak = f;
    }

    s_used += chunkSamples();
    return true;
}

// One block, no heap, no recording state -- enough to prove the I2S path is
// alive without committing the memory a real memo needs.
bool sampleOnce(int16_t* buf, size_t samples) {
    if (!buf || !samples) return false;
    if (!micOn()) return false;
    if (!M5Cardputer.Mic.record(buf, samples, s_rate)) return false;
    uint32_t deadline = millis() + 500;
    while (M5Cardputer.Mic.isRecording()) {
        if (millis() > deadline) return false;
        delay(2);
    }
    return true;
}

void recordStop() { if (s_recording) s_stop = Stop::Stopped; s_recording = false; }
bool recording() { return s_recording; }
size_t recordedSamples() { return s_used; }
float recordedSeconds() { return (float)s_used / s_rate; }
float level() { return s_level; }
const int16_t* pcm() { return s_buf; }

static void le32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void le16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

size_t wavHeader(uint8_t* h, size_t pcmBytes) {
    memcpy(h, "RIFF", 4);          le32(h + 4, 36 + pcmBytes);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);     le32(h + 16, 16);
    le16(h + 20, 1);               // PCM
    le16(h + 22, 1);               // mono
    le32(h + 24, s_rate);
    le32(h + 28, s_rate * 2); // byte rate
    le16(h + 32, 2);               // block align
    le16(h + 34, 16);              // bits per sample
    memcpy(h + 36, "data", 4);     le32(h + 40, pcmBytes);
    return 44;
}

void beep(uint16_t freq, uint32_t ms) {
    speakerOn();
    M5Cardputer.Speaker.tone(freq, ms);
    delay(ms + 20);
    micOn();
}

void chirpOk()  { beep(1200, 40); }
void chirpErr() { beep(300, 120); }

}  // namespace audio
