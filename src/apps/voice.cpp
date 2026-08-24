#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/audio.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"
#include "../kernel/ai.h"

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
        if (mode_ == RESULT) { mode_ = IDLE; audio::freeBuffer(); ui::acquireCanvas(); return true; }
        return false;
    }

    void onEnter() override { mode_ = IDLE; text_ = ""; scroll_ = 0; os::invalidate(); }
    void onExit() override {
        if (mode_ == REC) audio::recordStop();
        audio::freeBuffer();
        ui::acquireCanvas();
    }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case IDLE:
                if (k.tab || k.space || k.enter) start();
                return;
            case REC:
                stopAndTranscribe();
                return;
            case RESULT:
                if (k.tab)               { start(); return; }
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
        ui::centered(66, "up to " + String((unsigned)audio::capacitySeconds()) + " seconds",
                     ui::c().dim);

        String engine = ai::sttLabel(ai::preferredStt());
        bool ready = ai::sttConfigured(ai::preferredStt());
        ui::centered(86, engine, ready ? ui::c().good : ui::c().warn);
        if (!ready) ui::centered(98, "not configured - Settings", ui::c().dim);
        ui::hint("TAB record   ` back");
    }

    void drawRec() {
        float lv = audio::level();
        ui::centered(26, "Recording", ui::c().bad);

        // Level meter as discrete blocks: easier to read at a glance than a bar.
        const int n = 24, bw = 8;
        int x0 = (SCREEN_W - n * bw) / 2;
        int lit = (int)(lv * n);
        for (int i = 0; i < n; i++) {
            uint16_t col = i < lit ? (i > n * 3 / 4 ? ui::c().bad
                                    : i > n / 2     ? ui::c().warn : ui::c().good)
                                   : ui::c().surface;
            ui::gfx().fillRoundRect(x0 + i * bw, 44, bw - 2, 18, 1, col);
        }
        float frac = audio::recordedSeconds() / max(1.0f, (float)audio::capacitySeconds());
        ui::progress(12, 72, SCREEN_W - 24, 8, frac, ui::c().accent);
        ui::centered(88, String(audio::recordedSeconds(), 1) + "s", ui::c().dim);
        ui::hint("any key stops and transcribes");
    }

    void drawResult() {
        ui::pager(text_, scroll_, ui::c().fg, theme::bodyRows());
        ui::hint("S note  D daily  C ask  TAB again  ` back");
    }

    void start() {
        if (!audio::micReady()) { os::toast("no mic", os::Tone::Bad); return; }
        // The capture buffer and the UI canvas cannot both fit in internal RAM.
        ui::releaseCanvas();
        if (theme::sounds()) audio::chirpOk();
        audio::recordStart();
        if (!audio::recording()) {
            os::toast("not enough memory to record", os::Tone::Bad);
            ui::acquireCanvas();
            return;
        }
        mode_ = REC;
        os::invalidate();
    }

    void stopAndTranscribe() {
        audio::recordStop();
        size_t n = audio::recordedSamples();
        if (n < audio::SAMPLE_RATE / 2) {
            os::toast("too short", os::Tone::Bad);
            audio::freeBuffer();
            ui::acquireCanvas();
            mode_ = IDLE;
            os::invalidate();
            return;
        }
        mode_ = RESULT;
        ui::busy("Transcribing " + String(audio::recordedSeconds(), 1) + "s");
        auto r = ai::transcribe(audio::pcm(), n);
        audio::freeBuffer();
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

    void saveNote() {
        String file = store::newNoteName(ui::firstLine(text_, 30));
        String body = "# " + ui::firstLine(text_, 40) + "\n\n" + text_ + "\n";
        bool ok = store::writeNote(file, body);
        os::toast(ok ? "saved to Notes" : "save failed", ok ? os::Tone::Good : os::Tone::Bad);
    }

    void appendDaily() {
        ui::busy("Appending to daily note");
        auto r = cloud::vaultDailyAppend("- " + text_ + "\n");
        os::toast(r.ok ? "added to today's note" : r.error,
                  r.ok ? os::Tone::Good : os::Tone::Bad);
        os::invalidate();
    }

    void sendToAI() {
        ui::busy(String("Asking ") + ai::spec(ai::preferred()).label);
        auto r = ai::ask(text_,
            "You are a pocket assistant on a 240x135 handheld. Plain text only, "
            "no markdown, under 400 characters.", 300);
        text_ = r.ok ? r.text : ("[" + r.error + "]");
        os::toast(r.ok ? String("via ") + r.usedLabel() : r.error,
                  r.ok ? os::Tone::Good : os::Tone::Bad);
        scroll_ = 0;
        os::invalidate();
    }

    Mode mode_ = IDLE;
    String text_;
    int scroll_ = 0;
};

App* voiceApp() { static Voice a; return &a; }
