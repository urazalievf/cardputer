#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/audio.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"
#include "../kernel/ai.h"
#include "../kernel/hw.h"

// Voice memo -> text. TAB starts and stops; capture runs chunked from tick()
// so the level meter stays live instead of freezing the UI.
class Voice : public App {
    enum Mode : uint8_t { IDLE, REC, RESULT, CHECK };
public:
    const char* name() const override { return "Voice"; }
    const char* blurb() const override { return "speak"; }
    ui::Icon icon() const override { return ui::Icon::Mic; }

    String title() const override {
        if (mode_ == REC) {
            char b[32];
            snprintf(b, sizeof(b), "REC  %.1fs / %us", audio::recordedSeconds(),
                     (unsigned)audio::capacitySeconds());
            return String(b);
        }
        if (mode_ == RESULT) return "Transcript";
        if (mode_ == CHECK)  return "Mic check";
        return "Voice";
    }

    bool onBack() override {
        if (mode_ == REC)    { finish(); return true; }
        if (mode_ == CHECK)  { mode_ = IDLE; os::invalidate(); return true; }
        if (mode_ == RESULT) {
            mode_ = IDLE;
            audio::freeBuffer();
            havePcm_ = false;
            ui::acquireCanvas();
            return true;
        }
        return false;
    }

    void onEnter() override { mode_ = IDLE; text_ = ""; scroll_ = 0; os::invalidate(); }
    void onExit() override {
        if (mode_ == REC) audio::recordStop();
        hw::ledOff();
        audio::freeBuffer();
        havePcm_ = false;
        ui::acquireCanvas();
    }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case IDLE:
                if (k.tab || k.space || k.enter) { start(); return; }
                if (k.is('m')) { mode_ = CHECK; checkPeak_ = 0; os::invalidate(); return; }
                if (k.is('q')) {
                    uint32_t next = store::getInt("micrate", 16000) == 16000 ? 8000 : 16000;
                    store::setInt("micrate", next);
                    prepareBudget();
                    os::toast(String(next / 1000) + "kHz - up to " +
                              String((unsigned)audio::capacitySeconds()) + "s", os::Tone::Good);
                    os::invalidate();
                }
                return;
            case CHECK:
                mode_ = IDLE;
                audio::releaseI2S();
                os::invalidate();
                return;
            case REC:
                // The TAB that started this recording is usually still down, and
                // the keyboard reports a change the moment the matrix settles.
                // Only a key pressed after a clean release may stop the capture,
                // or the memo ends a chunk after it began -- which is exactly
                // what "too short" used to mean.
                if (!armed_ || !k.any()) return;
                finish();
                return;
            case RESULT:
                if (k.tab)               { start(); return; }
                if (k.is('p'))           { playback(); return; }
                if (k.down || k.is('j')) { scroll_++; os::invalidate(); return; }
                if (k.up   || k.is('k')) { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
                if (k.is('s')) { saveNote(); return; }
                if (k.is('d')) { appendDaily(); return; }
                if (k.is('c')) { sendToAI(); return; }
                return;
        }
    }

    void tick() override {
        if (mode_ == CHECK) { checkTick(); return; }
        if (mode_ != REC) return;
        // Arm the stop key only once every key has been let go of.
        if (!armed_ && !M5Cardputer.Keyboard.isPressed()) armed_ = true;
        if (!audio::recordChunk()) { finish(); return; }
        os::invalidate();
    }

    void draw() override {
        switch (mode_) {
            case IDLE:   return drawIdle();
            case REC:    return drawRec();
            case RESULT: return drawResult();
            case CHECK:  return drawCheck();
        }
    }

private:
    void drawIdle() {
        if (!audio::micReady()) {
            ui::icon(SCREEN_W / 2 - 5, 30, ui::Icon::Cross, ui::c().bad);
            ui::centered(46, "Microphone unavailable", ui::c().bad);
            ui::centered(58, "I2S0 PDM did not start", ui::c().dim);
            ui::centered(74, "M retries it and shows the level", ui::c().dim);
            ui::hint("M mic check   ` back");
            return;
        }
        ui::icon(SCREEN_W / 2 - 5, 30, ui::Icon::Mic, ui::c().accent);
        ui::centered(48, "Press TAB to record", ui::c().fg);
        ui::centered(62, "up to " + String((unsigned)audio::capacitySeconds()) + " seconds  at " +
                         String(audio::sampleRate() / 1000) + "kHz", ui::c().dim);

        String engine = ai::sttLabel(ai::preferredStt());
        bool ready = ai::sttConfigured(ai::preferredStt());
        ui::centered(82, engine, ready ? ui::c().good : ui::c().warn);
        if (!ready) ui::centered(94, ai::sttSetupHint(ai::preferredStt()), ui::c().dim);
        ui::hint("TAB record   M mic check   Q quality   ` back");
    }

    // A live, scrolling waveform — mirrored around a centre line, newest on the
    // right, so it reads like an oscilloscope rather than a progress bar.
    void drawWave(int top, int height, uint16_t tint) {
        auto& g = ui::gfx();
        const int mid = top + height / 2;
        const int half = height / 2 - 1;
        g.drawFastHLine(0, mid, SCREEN_W, ui::c().border);

        int n = audio::waveCount();
        if (n <= 0) return;
        int first = n > SCREEN_W ? n - SCREEN_W : 0;
        int count = n - first;
        int xoff = SCREEN_W - count;          // newest sample hugs the right edge

        for (int i = 0; i < count; i++) {
            float v = audio::waveAt(first + i);
            int h = (int)(v * half);
            if (h < 1) h = 1;
            // Loud passages warm up; clipping goes red.
            uint16_t col = v > 0.92f ? ui::c().bad : v > 0.6f ? ui::c().warn : tint;
            g.drawFastVLine(xoff + i, mid - h, h * 2, col);
        }
    }

    void drawRec() {
        ui::centered(20, "Recording", ui::c().bad);
        drawWave(30, 46, ui::c().good);

        float frac = audio::recordedSeconds() / max(1.0f, (float)audio::capacitySeconds());
        ui::progress(12, 82, SCREEN_W - 24, 6, frac, ui::c().accent);
        String t = String(audio::recordedSeconds(), 1) + "s  /  " +
                   String((unsigned)audio::capacitySeconds()) + "s";
        ui::centered(94, t, ui::c().dim);
        ui::hint(armed_ ? "any key stops and transcribes" : "keep talking - let go of TAB to arm stop");
    }

    // Proving the microphone works costs one 512-sample block and no heap, so
    // it stays usable on a board too full to allocate a real capture buffer.
    void checkTick() {
        static int16_t buf[512];
        if (!audio::sampleOnce(buf, 512)) { checkOk_ = false; os::invalidate(); return; }
        checkOk_ = true;
        uint64_t sum = 0;
        int16_t peak = 0;
        for (size_t i = 0; i < 512; i++) {
            int32_t v = buf[i];
            sum += (uint64_t)(v * v);
            int16_t a = v < 0 ? -v : v;
            if (a > peak) peak = a;
        }
        checkLevel_ = sqrtf((float)sum / 512) / 6000.0f;
        if (checkLevel_ > 1.0f) checkLevel_ = 1.0f;
        float p = (float)peak / 32767.0f;
        if (p > checkPeak_) checkPeak_ = p;
        os::invalidate();
    }

    void drawCheck() {
        if (!checkOk_) {
            ui::icon(SCREEN_W / 2 - 5, 34, ui::Icon::Cross, ui::c().bad);
            ui::centered(52, "The microphone will not start", ui::c().bad);
            ui::centered(66, "I2S0 refused the PDM channel", ui::c().dim);
            ui::centered(80, "A reboot clears a stuck channel", ui::c().dim);
            ui::hint("any key back");
            return;
        }
        ui::centered(22, "Say something", ui::c().fg);
        ui::progress(16, 40, SCREEN_W - 32, 14, checkLevel_, ui::c().good);
        ui::centered(62, "peak " + String((int)(checkPeak_ * 100)) + "%", ui::c().dim);
        if (checkPeak_ < 0.01f) {
            ui::centered(80, "silent - the port is up but", ui::c().warn);
            ui::centered(92, "no sound is reaching it", ui::c().warn);
        } else {
            ui::centered(86, "microphone is working", ui::c().good);
        }
        ui::hint("any key back");
    }

    void drawResult() {
        int rows = theme::bodyRows();
        int y = BODY_Y;
        if (wavPath_.length()) {
            String base = wavPath_.substring(wavPath_.lastIndexOf('/') + 1);
            ui::text(3, y, "saved " + base, ui::c().good);
            y += theme::rowHeight();
            rows--;
        }
        ui::pager(text_, scroll_, ui::c().fg, rows, y);
        ui::hint(String(havePcm_ ? "P play  " : "") + "S note  D daily  C ask  TAB again");
    }

    // How long a recording can be is pure arithmetic on a board with no PSRAM:
    // free RAM, minus whatever the upload will need, divided by the data rate.
    // Both of those are worth tuning before every recording.
    static void prepareBudget() {
        audio::setSampleRate(store::getInt("micrate", 16000));
        bool tls = ai::preferredStt() != ai::Stt::Host;
        audio::setHeadroomBytes(tls ? 72 * 1024 : 40 * 1024);
    }

    void start() {
        havePcm_ = false;
        wavPath_ = "";
        armed_ = false;
        prepareBudget();
        // The capture buffer and the UI canvas cannot both fit in internal RAM.
        ui::releaseCanvas();
        if (theme::sounds()) audio::chirpOk();
        if (!audio::recordStart()) {
            // Every failure used to surface as "not enough memory"; say which
            // one it actually was, because the two have different fixes.
            os::toast(audio::startError(), os::Tone::Bad);
            ui::acquireCanvas();
            mode_ = IDLE;
            os::invalidate();
            return;
        }
        hw::led(70, 0, 0);            // the screen is not always facing you
        mode_ = REC;
        os::invalidate();
    }

    void finish() {
        audio::recordStop();
        hw::ledOff();
        size_t n = audio::recordedSamples();
        if (n < audio::sampleRate() / 2) {
            os::toast(shortReason(n), os::Tone::Bad);
            audio::freeBuffer();
            ui::acquireCanvas();
            mode_ = IDLE;
            os::invalidate();
            return;
        }
        mode_ = RESULT;

        // Keep the audio before spending anything on the network: a failed
        // transcription should never also cost you the recording. Writing it
        // takes GPIO40 back from the microphone, which is fine now that the
        // capture is over.
        saveWav(n);

        ai::Result r;
        ui::await("Transcribing " + String(audio::recordedSeconds(), 1) + "s",
                  [&] { r = ai::transcribe(audio::pcm(), n); });
        // Hold the samples so P can play them back; the canvas comes back only
        // if there is room for both, otherwise the buffer wins until you leave.
        havePcm_ = true;
        ui::acquireCanvas();

        if (r.ok && r.text.length()) {
            text_ = r.text;
            os::toast(String("heard it - ") + r.usedLabel(), os::Tone::Good);
        } else {
            text_ = transcribeFailure(r);
            if (theme::sounds()) audio::chirpErr();
            os::toast(r.error.length() ? r.error : String("no transcript"), os::Tone::Bad);
        }
        scroll_ = 0;
        os::invalidate();
    }

    // "too short" was the same message whether the microphone died, the buffer
    // filled, or you really did only speak for a moment. They are different
    // problems and only one of them is yours to fix.
    String shortReason(size_t samples) const {
        float secs = (float)samples / audio::sampleRate();
        if (audio::stopReason() == audio::Stop::MicFailed)
            return "microphone stopped after " + String(secs, 1) + "s";
        if (samples == 0)
            return "captured nothing - try M for a mic check";
        if (audio::peakLevel() < 0.01f)
            return "captured " + String(secs, 1) + "s of silence";
        return "too short - " + String(secs, 1) + "s";
    }

    String transcribeFailure(const ai::Result& r) const {
        String why = r.error.length() ? r.error : String("empty transcript");
        String body = "[" + why + "]";
        if (!ai::sttConfigured(ai::preferredStt()))
            body += "\n\n" + String(ai::sttSetupHint(ai::preferredStt()));
        else if (audio::peakLevel() < 0.02f)
            body += "\n\nThe recording was almost silent, so there may have been "
                    "nothing to transcribe.";
        if (wavPath_.length())
            body += "\n\nThe audio is safe at " + wavPath_ + " - you can retry later.";
        return body;
    }

    // The card is unmounted while the microphone holds the bus, so this is the
    // first chance to write anything. Documented behaviour since v0.2 that was
    // never actually wired up.
    void saveWav(size_t samples) {
        // Settings has offered "Keep audio" since v0.2; nothing read it, and
        // nothing wrote the file either.
        if (!store::getInt("recsave", 1)) return;
        if (!store::sdReady()) return;
        ui::busy("Saving audio");
        String path = store::newRecordingName();
        if (store::writeWav(path, audio::pcm(), samples)) wavPath_ = path;
        else os::logf("voice: WAV save failed (%s)", path.c_str());
    }

    // Hear it back. The samples are gone once the buffer is freed, so this only
    // offers itself while they are still around.
    void playback() {
        if (!havePcm_ || !audio::bufferHeld()) { os::toast("samples already freed"); return; }
        audio::speakerOn();
        M5Cardputer.Speaker.setVolume(200);
        M5Cardputer.Speaker.playRaw(audio::pcm(), audio::recordedSamples(),
                                    audio::sampleRate(), false);
        uint32_t total = audio::recordedSamples() * 1000UL / audio::sampleRate();
        uint32_t start = millis();
        while (M5Cardputer.Speaker.isPlaying() && millis() - start < total + 500) {
            ui::beginFrame();
            ui::centered(20, "Playing back", ui::c().accent2);
            drawWave(30, 46, ui::c().accent2);
            float frac = (float)(millis() - start) / max(1UL, total);
            ui::progress(12, 82, SCREEN_W - 24, 6, frac, ui::c().accent2);
            ui::statusBar("Playback", ui::Icon::Mic);
            ui::endFrame();
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                M5Cardputer.Speaker.stop();
                break;
            }
            delay(30);
        }
        audio::micOn();
        os::invalidate();
    }

    void saveNote() {
        String file = store::newNoteName(ui::firstLine(text_, 30));
        String body = "# " + ui::firstLine(text_, 40) + "\n\n" + text_ + "\n";
        // Point the note at the audio it came from, so the two stay connected
        // once they are sitting in an Obsidian vault.
        if (wavPath_.length()) body += "\n[audio](" + wavPath_ + ")\n";
        bool ok = store::writeNote(file, body);
        os::toast(ok ? "saved to Notes" : "save failed", ok ? os::Tone::Good : os::Tone::Bad);
    }

    void appendDaily() {
        cloud::Result r;
        ui::await("Appending to daily note",
                  [&] { r = cloud::vaultDailyAppend("- " + text_ + "\n"); });
        os::toast(r.ok ? "added to today's note" : r.error,
                  r.ok ? os::Tone::Good : os::Tone::Bad);
        os::invalidate();
    }

    void sendToAI() {
        ai::Result r;
        ui::await(String("Asking ") + ai::spec(ai::preferred()).label, [&] {
            r = ai::ask(text_,
                "You are a pocket assistant on a 240x135 handheld. Plain text only, "
                "no markdown, under 400 characters.", 300);
        });
        text_ = r.ok ? r.text : ("[" + r.error + "]");
        os::toast(r.ok ? String("via ") + r.usedLabel() : r.error,
                  r.ok ? os::Tone::Good : os::Tone::Bad);
        scroll_ = 0;
        os::invalidate();
    }

    Mode mode_ = IDLE;
    String text_, wavPath_;
    int scroll_ = 0;
    bool havePcm_ = false;
    bool armed_ = false;            // a key may stop the capture
    bool checkOk_ = true;
    float checkLevel_ = 0, checkPeak_ = 0;
};

App* voiceApp() { static Voice a; return &a; }
