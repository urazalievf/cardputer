#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/audio.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"

// Voice memo -> text. TAB starts and stops; capture runs chunked from tick()
// so the level meter stays live instead of freezing the UI.
class Voice : public App {
    enum Mode { IDLE, REC, BUSY, RESULT };
public:
    const char* name() const override { return "Voice"; }
    const char* blurb() const override { return "record + stt"; }

    String title() const override {
        if (mode_ == REC) {
            char b[32];
            snprintf(b, sizeof(b), "REC %.1fs / %us", audio::recordedSeconds(),
                     (unsigned)audio::capacitySeconds());
            return String(b);
        }
        if (mode_ == BUSY)   return "Transcribing...";
        if (mode_ == RESULT) return "Transcript (" + String((int)text_.length()) + ")";
        return "Voice";
    }

    bool escExits() const override { return mode_ == IDLE; }

    void onEnter() override { mode_ = IDLE; text_ = ""; scroll_ = 0; os::invalidate(); }
    void onExit() override { if (mode_ == REC) audio::recordStop(); }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case IDLE:
                if (k.tab || k.space) { start(); }
                return;
            case REC:
                stopAndTranscribe();
                return;
            case BUSY:
                return;
            case RESULT:
                if (k.esc)     { mode_ = IDLE; os::invalidate(); return; }
                if (k.tab)     { start(); return; }
                if (k.down || k.is('j')) { scroll_++; os::invalidate(); return; }
                if (k.up   || k.is('k')) { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
                if (k.is('s')) { saveNote(); return; }
                if (k.is('d')) { appendDaily(); return; }
                if (k.is('c')) { sendToClaude(); return; }
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
            case BUSY:   ui::centered(58, busy_, ui::WARN); ui::hint(""); return;
            case RESULT: return drawResult();
        }
    }

private:
    void drawIdle() {
        if (!audio::micReady()) {
            ui::centered(48, "Microphone unavailable", ui::BAD);
            ui::centered(62, "check I2S / free heap", ui::DIM);
            ui::hint("` back");
            return;
        }
        ui::centered(44, "Press TAB to record", ui::FG);
        ui::centered(58, "up to " + String((unsigned)audio::capacitySeconds()) + "s", ui::DIM);
        ui::centered(76, cloud::hostOnline() ? "stt: mac daemon" : "stt: whisper api",
                     cloud::hostOnline() ? ui::GOOD : ui::DIM);
        ui::hint("TAB record   ` back");
    }

    void drawRec() {
        // Level meter
        float lv = audio::level();
        int w = (int)(lv * (SCREEN_W - 20));
        ui::gfx().drawRect(10, 50, SCREEN_W - 20, 14, ui::DIM);
        ui::gfx().fillRect(11, 51, max(0, w - 2), 12, lv > 0.85f ? ui::BAD : ui::GOOD);

        // Elapsed bar against capacity
        float frac = audio::recordedSeconds() / max(1.0f, (float)audio::capacitySeconds());
        ui::gfx().fillRect(10, 72, (int)(frac * (SCREEN_W - 20)), 3, ui::ACCENT);

        ui::centered(30, "Recording", ui::BAD);
        ui::hint("any key = stop & transcribe");
    }

    void drawResult() {
        auto lines = ui::wrap(text_, CHARS_PER_LINE);
        if (scroll_ > (int)lines.size() - 1) scroll_ = max(0, (int)lines.size() - 1);
        for (int i = 0; i < 9 && scroll_ + i < (int)lines.size(); i++)
            ui::text(2, BODY_Y + i * ROW_H, lines[scroll_ + i], ui::FG);
        ui::hint("S note  D daily  C claude  TAB again  ` back");
    }

    void start() {
        if (!audio::micReady()) { os::toast("no mic"); return; }
        audio::chirpOk();
        audio::recordStart();
        mode_ = REC;
        os::invalidate();
    }

    void busyPaint(const String& msg) {
        busy_ = msg;
        mode_ = BUSY;
        ui::clear(); draw(); ui::statusBar(title().c_str());
    }

    void stopAndTranscribe() {
        audio::recordStop();
        size_t n = audio::recordedSamples();
        if (n < audio::SAMPLE_RATE / 2) {     // under half a second
            os::toast("too short");
            mode_ = IDLE;
            os::invalidate();
            return;
        }
        busyPaint("Transcribing " + String(audio::recordedSeconds(), 1) + "s...");
        auto r = cloud::transcribe(audio::pcm(), n);
        if (r.ok && r.text.length()) {
            text_ = r.text;
            os::toast(String("stt via ") + r.sourceName());
        } else {
            text_ = "[" + (r.error.length() ? r.error : String("empty transcript")) + "]";
            audio::chirpErr();
        }
        scroll_ = 0;
        mode_ = RESULT;
        os::invalidate();
    }

    void saveNote() {
        String file = store::newNoteName(ui::firstLine(text_, 30));
        String body = "# " + ui::firstLine(text_, 40) + "\n\n" + text_ + "\n";
        bool ok = store::writeNote(file, body);
        os::toast(ok ? "saved to Notes" : "save failed");
    }

    void appendDaily() {
        busyPaint("Appending to daily note...");
        auto r = cloud::vaultDailyAppend("- " + text_ + "\n");
        os::toast(r.ok ? "added to daily note" : r.error);
        mode_ = RESULT;
        os::invalidate();
    }

    void sendToClaude() {
        busyPaint("Asking Claude...");
        auto r = cloud::ask(text_,
            "You are a pocket assistant on a 240x135 handheld. Plain text only, "
            "no markdown, under 400 characters.", 300);
        text_ = r.ok ? r.text : ("[" + r.error + "]");
        scroll_ = 0;
        mode_ = RESULT;
        os::invalidate();
    }

    Mode mode_ = IDLE;
    String text_, busy_;
    int scroll_ = 0;
};

App* voiceApp() { static Voice a; return &a; }
