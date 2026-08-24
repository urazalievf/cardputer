#include "audio.h"
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

static const size_t CHUNK = SAMPLE_RATE / 10;   // 100ms

void begin() {
    // StampS3 has no PSRAM on stock Cardputer; try anyway for ADV / modded units,
    // then fall back to a slice of internal heap.
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t want = SAMPLE_RATE * 30;             // 30s ceiling
    if (psram >= want * sizeof(int16_t)) {
        s_buf = (int16_t*)heap_caps_malloc(want * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_buf) s_cap = want;
    }
    if (!s_buf) {
        size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t bytes = internalFree / 2;
        if (bytes > 160 * 1024) bytes = 160 * 1024;   // leave room for TLS buffers
        s_cap = bytes / sizeof(int16_t);
        s_buf = (int16_t*)malloc(s_cap * sizeof(int16_t));
        if (!s_buf) s_cap = 0;
    }
    micOn();
    s_micReady = s_buf && M5Cardputer.Mic.isEnabled();
}

bool micReady() { return s_micReady; }
size_t capacitySamples() { return s_cap; }
uint32_t capacitySeconds() { return s_cap / SAMPLE_RATE; }

void micOn() {
    if (s_micOwnsI2S) return;
    M5Cardputer.Speaker.end();
    delay(60);
    M5Cardputer.Mic.begin();
    delay(60);
    s_micOwnsI2S = true;
}

void speakerOn() {
    if (!s_micOwnsI2S) return;
    M5Cardputer.Mic.end();
    delay(60);
    M5Cardputer.Speaker.begin();
    delay(60);
    s_micOwnsI2S = false;
}

void clear() { s_used = 0; s_level = 0.0f; }

void recordStart() {
    micOn();
    s_used = 0;
    s_level = 0.0f;
    s_recording = s_micReady;
}

bool recordChunk() {
    if (!s_recording || !s_buf) return false;
    size_t room = s_cap - s_used;
    if (room < CHUNK) { s_recording = false; return false; }

    if (!M5Cardputer.Mic.record(s_buf + s_used, CHUNK, SAMPLE_RATE)) return false;
    while (M5Cardputer.Mic.isRecording()) delay(2);

    // RMS of this chunk, normalised against a comfortable speaking level.
    uint64_t sum = 0;
    for (size_t i = 0; i < CHUNK; i++) {
        int32_t v = s_buf[s_used + i];
        sum += (uint64_t)(v * v);
    }
    float rms = sqrtf((float)sum / CHUNK);
    s_level = rms / 6000.0f;
    if (s_level > 1.0f) s_level = 1.0f;

    s_used += CHUNK;
    return true;
}

void recordStop() { s_recording = false; }
bool recording() { return s_recording; }
size_t recordedSamples() { return s_used; }
float recordedSeconds() { return (float)s_used / SAMPLE_RATE; }
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
    le32(h + 24, SAMPLE_RATE);
    le32(h + 28, SAMPLE_RATE * 2); // byte rate
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
