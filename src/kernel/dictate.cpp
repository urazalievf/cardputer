#include "dictate.h"
#include "audio.h"
#include "ai.h"
#include "store.h"
#include "theme.h"

namespace dictate {

// Scratch file for a dictation that is not being kept. One fixed name, reused
// and deleted, so a month of translating does not fill the card with audio
// nobody asked to keep.
static const char* SCRATCH = "/recordings/.dictate.wav";

static const uint32_t MAX_SECONDS = 600;

static bool flush() {
    size_t n = 0;
    while (const int16_t* chunk = audio::pendingChunk(&n)) {
        if (!store::wavAppend(chunk, n)) return false;
        audio::releaseChunk();
    }
    return true;
}

Result run(const String& title, ui::Icon icon) {
    Result out;

    if (!audio::micReady()) {
        out.error = "microphone unavailable - see Voice > M";
        return out;
    }

    audio::setSampleRate(store::getInt("micrate", 16000));
    audio::setHeadroomBytes(ai::preferredStt() != ai::Stt::Host ? 72 * 1024 : 40 * 1024);

    // Open the file before claiming the microphone: sdAcquire() falls back to
    // evicting audio when a mount fails, and doing this second could tear down
    // a capture that had already started.
    bool streaming = false;
    if (store::sdReady() && !store::audioConflicts()) {
        store::removeFile(SCRATCH);
        streaming = store::wavOpen(SCRATCH);
    }

    ui::releaseCanvas();
    if (theme::sounds()) audio::chirpOk();
    audio::setStreaming(streaming);

    if (!audio::recordStart()) {
        if (streaming) store::wavAbort();
        audio::setStreaming(false);
        ui::acquireCanvas();
        out.error = audio::startError();
        return out;
    }
    if (streaming && !store::sdMounted()) {
        store::wavAbort();
        streaming = false;
        audio::setStreaming(false);
    }

    // The key that started this is still down. Arm the stop only once every key
    // has been released, or the recording ends one chunk in and comes back as
    // "too short".
    bool armed = false;
    while (audio::recordChunk()) {
        if (streaming && !flush()) {
            store::wavClose();
            streaming = false;      // carry on in RAM rather than lose the words
        }

        ui::beginFrame();
        ui::centered(32, "Listening", ui::c().bad);
        ui::progress(20, 50, SCREEN_W - 40, 12, audio::level(), ui::c().good);
        float secs = audio::capturedSeconds();
        ui::centered(74, String(secs, 1) + "s  -  " +
                         (armed ? "any key stops" : "let go to arm stop"), ui::c().dim);
        ui::centered(90, streaming ? "to card" : "in memory", ui::c().dim);
        ui::statusBar(title, icon);
        ui::endFrame();

        M5Cardputer.update();
        bool down = M5Cardputer.Keyboard.isPressed();
        if (!armed && !down) { armed = true; continue; }
        if (armed && down) break;
        if (streaming && secs >= MAX_SECONDS) break;
    }
    audio::recordStop();
    if (streaming) flush();

    size_t ramSamples = audio::recordedSamples();
    size_t total = streaming ? store::wavSamples() : ramSamples;
    if (total < audio::sampleRate() / 2) {
        float secs = (float)total / audio::sampleRate();
        out.error = audio::stopReason() == audio::Stop::MicFailed
                  ? "microphone stopped after " + String(secs, 1) + "s"
                  : total == 0 ? String("captured nothing - try Voice > M")
                               : "too short - " + String(secs, 1) + "s";
        if (streaming) store::wavAbort();
        audio::freeBuffer();
        ui::acquireCanvas();
        return out;
    }

    ai::Result r;
    float secs = (float)total / audio::sampleRate();
    if (streaming) {
        store::wavClose();
        audio::freeBuffer();          // the upload streams off the card
        ui::acquireCanvas();
        ui::await("Transcribing " + String(secs, 1) + "s",
                  [&] { r = ai::transcribeFile(SCRATCH); });
    } else {
        ui::await("Transcribing " + String(secs, 1) + "s",
                  [&] { r = ai::transcribe(audio::pcm(), ramSamples); });
        audio::freeBuffer();
        ui::acquireCanvas();
    }

    // Keep the audio only if the user asked for it; a dictation is a means to a
    // transcript, not a memo. Renamed rather than rewritten -- it is already on
    // the card and may be megabytes.
    if (streaming) {
        if (r.ok && store::getInt("recsave", 1)) {
            String kept = store::newRecordingName();
            if (store::rename(SCRATCH, kept)) out.wavPath = kept;
        } else {
            store::removeFile(SCRATCH);
        }
    }

    audio::setStreaming(false);
    out.ok = r.ok;
    out.text = r.text;
    out.error = r.error;
    return out;
}

}  // namespace dictate
