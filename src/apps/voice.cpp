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
    enum Mode : uint8_t { IDLE, REC, RESULT };
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
        return "Voice";
    }

    bool onBack() override {
        if (mode_ == REC)    { stopAndTranscribe(); return true; }
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
                if (k.is('q')) {
                    uint32_t next = store::getInt("micrate", 16000) == 16000 ? 8000 : 16000;
                    store::setInt("micrate", next);
                    prepareBudget();
                    os::toast(String(next / 1000) + "kHz - up to " +
                              String((unsigned)audio::capacitySeconds()) + "s", os::Tone::Good);
                    os::invalidate();
                }
                return;
            case REC:
                stopAndTranscribe();
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
        if (mode_ != REC) return;
        if (!audio::recordChunk()) { stopAndTranscribe(); return; }
        os::invalidate();
    }

    void draw() override {
        switch (mode_) {
            case IDLE:   return drawIdle();
            case REC:    return drawRec();
            case RESULT: return drawResult();
        }
    }

private:
    void drawIdle() {
        if (!audio::micReady()) {
            ui::centered(48, "Microphone unavailable", ui::c().bad);
            ui::centered(62, "I2S did not come up", ui::c().dim);
            ui::hint("` back");
            return;
        }
        ui::icon(SCREEN_W / 2 - 5, 34, ui::Icon::Mic, ui::c().accent);
        ui::centered(52, "Press TAB to record", ui::c().fg);
        ui::centered(66, "up to " + String((unsigned)audio::capacitySeconds()) + " seconds  at " +
                         String(audio::sampleRate() / 1000) + "kHz", ui::c().dim);

        String engine = ai::sttLabel(ai::preferredStt());
        bool ready = ai::sttConfigured(ai::preferredStt());
        ui::centered(86, engine, ready ? ui::c().good : ui::c().warn);
        if (!ready) ui::centered(98, "not configured - Settings", ui::c().dim);
        ui::hint("TAB record   Q quality   ` back");
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
        ui::hint("any key stops and transcribes");
    }

    void drawResult() {
        ui::pager(text_, scroll_, ui::c().fg, theme::bodyRows());
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
        if (!audio::micReady()) { os::toast("no mic", os::Tone::Bad); return; }
        havePcm_ = false;
        prepareBudget();
        // The capture buffer and the UI canvas cannot both fit in internal RAM.
        ui::releaseCanvas();
        if (theme::sounds()) audio::chirpOk();
        audio::recordStart();
        if (!audio::recording()) {
            os::toast("not enough memory to record", os::Tone::Bad);
            ui::acquireCanvas();
            return;
        }
        hw::led(70, 0, 0);            // the screen is not always facing you
        mode_ = REC;
        os::invalidate();
    }

    void stopAndTranscribe() {
        audio::recordStop();
        hw::ledOff();
        size_t n = audio::recordedSamples();
        if (n < audio::sampleRate() / 2) {
            os::toast("too short", os::Tone::Bad);
            audio::freeBuffer();
            ui::acquireCanvas();
            mode_ = IDLE;
            os::invalidate();
            return;
        }
        mode_ = RESULT;
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
            text_ = "[" + (r.error.length() ? r.error : String("empty transcript")) + "]";
            if (theme::sounds()) audio::chirpErr();
            os::toast(r.error, os::Tone::Bad);
        }
        scroll_ = 0;
        os::invalidate();
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
    String text_;
    int scroll_ = 0;
    bool havePcm_ = false;
};

App* voiceApp() { static Voice a; return &a; }
