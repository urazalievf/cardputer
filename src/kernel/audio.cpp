#include "audio.h"
#include "store.h"
#include <esp_heap_caps.h>
#include <string.h>
#include <SD.h>

namespace audio {

static int16_t* s_buf = nullptr;
static size_t   s_cap = 0;        // samples
static size_t   s_used = 0;
static bool     s_micReady = false;
static bool     s_recording = false;
static size_t   s_submitted = 0;   // samples handed to the driver
static size_t   s_released = 0;    // samples written out and free to overwrite
static bool     s_wrapped = false;
static float    s_level = 0.0f;
static bool     s_micOwnsI2S = false;

static uint32_t s_rate = 16000;
static size_t   s_headroom = 72 * 1024;
static size_t   s_sdReclaim = 0;
static bool     s_streaming = false;

// Two seconds at 16kHz. An SD write of one chunk is a few milliseconds, so this
// is three orders of magnitude of slack; anything larger is RAM held for no
// reason while the card is mounted and memory is scarce.
static const size_t STREAM_RING_BYTES = 64 * 1024;
// The ring is freed before the upload, so the only thing that has to survive
// alongside it is the filesystem doing the writing.
static const size_t STREAM_HEADROOM = 24 * 1024;
static float    s_peak = 0.0f;
static Stop     s_stop = Stop::None;
static const char* s_startErr = "";

uint32_t sampleRate() { return s_rate; }
void setSampleRate(uint32_t hz) { s_rate = (hz == 8000 || hz == 16000) ? hz : 16000; }
void setHeadroomBytes(size_t bytes) { s_headroom = bytes; }
void setStreaming(bool on) { s_streaming = on; }
bool streaming() { return s_streaming; }
static size_t headroom() { return s_streaming ? STREAM_HEADROOM : s_headroom; }
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
    // Streaming keeps the card, so its buffers are not ours to count.
    size_t avail = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) + s_reclaimable +
                   (s_streaming ? 0 : s_sdReclaim);
    size_t bytes = avail > headroom() ? avail - headroom() : 0;
    size_t cap = s_streaming ? STREAM_RING_BYTES : 200 * 1024;
    if (bytes > cap) bytes = cap;
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
    size_t budget = total > headroom() ? total - headroom() : 0;
    size_t bytes = largest < budget ? largest : budget;
    size_t cap = s_streaming ? STREAM_RING_BYTES : 200 * 1024;
    if (bytes > cap) bytes = cap;

    // Back off rather than give up: a shorter memo beats no memo. A streaming
    // ring only has to outlast one card write, so it may go far smaller.
    size_t floorBytes = s_streaming ? chunkSamples() * 4 * sizeof(int16_t)
                                    : s_rate * sizeof(int16_t);
    while (bytes >= floorBytes) {
        s_buf = (int16_t*)malloc(bytes);
        if (s_buf) {
            // Round down to whole chunks: the ring indexes by modulo, so a
            // capacity that is not a multiple of the chunk size would let a
            // request straddle the wrap and scribble over the start.
            size_t chunk = chunkSamples();
            s_cap = (bytes / sizeof(int16_t)) / chunk * chunk;
            return s_cap > 0;
        }
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

    // Unmounting hands back ~28KB of driver and FATFS buffers, which is most of
    // a second of recording on a board with no PSRAM -- so do it unless this
    // recording is streaming to the card and actually needs the filesystem.
    if (!s_streaming || store::audioConflicts()) store::sdRelease();
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
    if (store::audioConflicts()) store::sdRelease();
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

// Hand the driver everything it will take, up to its two-slot queue. Never
// blocks: Mic.record() only waits when both slots are busy, and this checks.
static bool topUp() {
    if (!s_buf) return false;
    const size_t chunk = chunkSamples();
    // Only submit into space that has been written out (or was never claimed).
    // With nothing draining, s_released stays 0 and this stops at s_cap, which
    // is exactly how a plain linear buffer behaves.
    while (M5Cardputer.Mic.isRecording() < 2 &&
           s_submitted + chunk <= s_released + s_cap) {
        int16_t* dst = s_buf + (s_submitted % s_cap);
        if (!M5Cardputer.Mic.record(dst, chunk, s_rate)) return false;
        s_submitted += chunk;
        if (s_submitted > s_cap) s_wrapped = true;
    }
    return true;
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
    s_submitted = 0;
    s_released = 0;
    s_wrapped = false;
    s_level = 0.0f;
    // Not s_micReady: that is a cached verdict. This has to be true right now.
    s_recording = M5Cardputer.Mic.isRunning();
    if (!s_recording) {
        s_startErr = "microphone stopped before the first chunk";
        freeBuffer();
        return false;
    }
    // Prime both slots before returning, so the very first chunk is already
    // being filled while the app paints its first frame.
    if (!topUp()) {
        s_startErr = "microphone refused the first buffer";
        s_recording = false;
        freeBuffer();
        return false;
    }
    return true;
}

// Samples the driver has actually finished writing. Requests complete in the
// order they were queued and are all the same size, so the outstanding count is
// enough to work it out.
static size_t completedSamples() {
    size_t pending = (size_t)M5Cardputer.Mic.isRecording();
    size_t outstanding = pending * chunkSamples();
    return s_submitted > outstanding ? s_submitted - outstanding : 0;
}

bool recordChunk() {
    if (!s_recording || !s_buf) return false;

    // Keep two requests outstanding at all times. mic_task blocks on
    // ulTaskNotifyTake(portMAX_DELAY) the moment both slots are empty, and every
    // sample arriving at the I2S port while it is blocked is discarded -- so the
    // 20-60ms this function's caller spends drawing a waveform used to be a hole
    // punched straight through the recording, once every 100ms. Four seconds of
    // samples stitched out of six seconds of speech is not speech any more, and
    // Whisper answers noise with its stock hallucination.
    if (!topUp()) {
        os::logf("audio: Mic.record() failed at %.1fs", (double)s_used / s_rate);
        s_recording = false;
        s_stop = Stop::MicFailed;
        return false;
    }

    // Nothing left to submit and nothing left in flight: the buffer is full.
    if (s_used >= s_submitted && M5Cardputer.Mic.isRecording() == 0) {
        s_recording = false;
        s_stop = Stop::Full;
        return false;
    }

    // Wait for the oldest outstanding request to land -- while the other one
    // keeps filling.
    uint32_t deadline = millis() + 500;
    while (completedSamples() < s_used + chunkSamples()) {
        if (millis() > deadline) {
            os::logf("audio: mic task stalled at %.1fs", (double)s_used / s_rate);
            s_recording = false;
            s_stop = Stop::MicFailed;
            return false;
        }
        delay(2);
        topUp();
    }

    // Envelope in SUBS slices so the waveform scrolls smoothly, plus the
    // whole-chunk RMS for the coarse level readout. Chunks are ring-aligned, so
    // one chunk never straddles the wrap and this base is enough.
    const int16_t* chunkAt = s_buf + (s_used % s_cap);
    const size_t sub = chunkSamples() / SUBS;
    uint64_t total = 0;
    for (int s = 0; s < SUBS; s++) {
        uint64_t sum = 0;
        for (size_t i = 0; i < sub; i++) {
            int32_t v = chunkAt[s * sub + i];
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
        int32_t v = chunkAt[i];
        if (v < 0) v = -v;
        float f = (float)v / 32767.0f;
        if (f > s_peak) s_peak = f;
    }

    s_used += chunkSamples();
    // Refill before returning: the caller's next stop is a full repaint.
    topUp();
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

// The oldest chunk that has been captured and analysed but not yet written out.
const int16_t* pendingChunk(size_t* samples) {
    if (!s_buf || s_released >= s_used) return nullptr;
    if (samples) *samples = chunkSamples();
    return s_buf + (s_released % s_cap);
}

void releaseChunk() {
    if (s_released < s_used) s_released += chunkSamples();
}

size_t pendingChunks() {
    return s_used > s_released ? (s_used - s_released) / chunkSamples() : 0;
}

size_t capturedSamples() { return s_used; }
float  capturedSeconds() { return (float)s_used / s_rate; }
bool   wrapped() { return s_wrapped; }

void recordStop() {
    if (s_recording) s_stop = Stop::Stopped;
    s_recording = false;
    // Two requests are usually still in flight. Let them land and count them,
    // rather than throwing away the last 200ms of every recording -- which is
    // often the end of the last word.
    uint32_t deadline = millis() + 400;
    while (M5Cardputer.Mic.isRecording() && millis() < deadline) delay(2);
    // s_used is a running total across the whole recording, not an index into
    // the ring, so there is nothing to clamp it against.
    size_t done = completedSamples();
    if (done > s_used) s_used = done;
}
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

static uint32_t le32at(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16at(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

bool playWavFile(const String& path, std::function<bool(float)> onFrame) {
    if (!store::sdAcquire()) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) { os::logf("play: cannot open %s", path.c_str()); return false; }

    uint8_t hdr[44];
    if (f.read(hdr, 44) != 44 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        f.close();
        os::logf("play: %s is not a WAV", path.c_str());
        return false;
    }
    // Read the rate out of the file rather than assuming the current setting:
    // the memo may have been recorded at 8kHz and the mic switched since.
    uint32_t rate = le32at(hdr + 24);
    uint16_t channels = le16at(hdr + 22);
    uint16_t bits = le16at(hdr + 34);
    uint32_t dataBytes = le32at(hdr + 40);
    if (rate < 4000 || rate > 48000 || bits != 16 || channels != 1) {
        f.close();
        os::logf("play: unsupported format (%luHz %ubit %uch)",
                 (unsigned long)rate, bits, channels);
        return false;
    }
    size_t fileData = f.size() > 44 ? f.size() - 44 : 0;
    if (dataBytes == 0 || dataBytes > fileData) dataBytes = fileData;   // header not patched
    if (dataBytes == 0) { f.close(); return false; }

    // Two buffers alternating through a two-slot queue: playRaw() keeps the
    // pointer rather than copying, so a buffer must not be refilled until the
    // slot holding it has drained.
    const size_t BLOCK = 1024;                     // samples, 2KB each
    int16_t* buf[2] = {
        (int16_t*)malloc(BLOCK * sizeof(int16_t)),
        (int16_t*)malloc(BLOCK * sizeof(int16_t)),
    };
    if (!buf[0] || !buf[1]) {
        free(buf[0]); free(buf[1]);
        f.close();
        os::logf("play: no room for the playback buffers");
        return false;
    }

    speakerOn();
    M5Cardputer.Speaker.setVolume(200);
    const int ch = 0;

    size_t sent = 0;
    int idx = 0;
    bool stopped = false;
    while (sent < dataBytes) {
        size_t want = dataBytes - sent;
        if (want > BLOCK * sizeof(int16_t)) want = BLOCK * sizeof(int16_t);
        int got = f.read((uint8_t*)buf[idx], want);
        if (got <= 0) break;

        // Wait for a free slot before overwriting the buffer this one will use.
        uint32_t deadline = millis() + 3000;
        while (M5Cardputer.Speaker.isPlaying(ch) >= 2) {
            if (millis() > deadline) { stopped = true; break; }
            delay(2);
        }
        if (stopped) break;

        M5Cardputer.Speaker.playRaw(buf[idx], got / sizeof(int16_t), rate, false, 1, ch);
        sent += got;
        idx ^= 1;

        if (onFrame && !onFrame((float)sent / dataBytes)) { stopped = true; break; }
    }

    if (stopped) M5Cardputer.Speaker.stop();
    else {
        uint32_t deadline = millis() + 4000;
        while (M5Cardputer.Speaker.isPlaying(ch) && millis() < deadline) {
            if (onFrame && !onFrame(1.0f)) { M5Cardputer.Speaker.stop(); break; }
            delay(10);
        }
    }

    f.close();
    free(buf[0]);
    free(buf[1]);
    micOn();
    return true;
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
