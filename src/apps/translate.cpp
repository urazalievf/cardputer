#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/audio.h"
#include "../kernel/ai.h"
#include "../kernel/store.h"

// Speak, get it back in another language. The single best argument for putting
// a microphone and a language model in the same pocket device.
class Translate : public App {
public:
    const char* name() const override { return "Translate"; }
    const char* blurb() const override { return "speak"; }
    ui::Icon icon() const override { return ui::Icon::Chat; }

    String title() const override { return String("-> ") + langName(); }

    bool onBack() override {
        if (out_.length()) { out_ = ""; src_ = ""; os::invalidate(); return true; }
        if (in_.length())  { in_ = ""; os::invalidate(); return true; }
        return false;
    }

    void onEnter() override { lang_ = store::getInt("trlang", 0); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (k.tab) { speakAndTranslate(); return; }
        if (k.enter) { translate(in_); return; }
        if (k.ctrl && k.is('l')) { pickLanguage(); return; }
        if (in_.length() == 0 && k.chars.empty()) {
            if (k.down) { scroll_++; os::invalidate(); return; }
            if (k.up)   { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
        }
        if (ui::editBuffer(in_, k, 300)) os::invalidate();
    }

    void draw() override {
        if (out_.length()) {
            ui::text(4, BODY_Y + 2, ui::ellipsize(src_, 38), ui::c().dim);
            ui::gfx().drawFastHLine(0, BODY_Y + 14, SCREEN_W, ui::c().border);
            ui::pager(out_, scroll_, ui::c().accent2, 7, BODY_Y + 20);
        } else {
            ui::centered(34, "Speak or type", ui::c().dim);
            ui::centered(50, String("into ") + langName(), ui::c().accent);
            ui::centered(70, "TAB to talk", ui::c().accent2);
        }
        ui::inputLine(104, "> ", in_, ui::c().fg);
        ui::hint("TAB talk  Enter go  ctrl+L language  ` back");
    }

private:
    // Kept short: every extra option is another press on a 56-key thumbboard.
    static const char* const* langs(int& n) {
        static const char* L[] = {
            "Spanish", "French", "German", "Italian", "Portuguese",
            "Russian", "Ukrainian", "Uzbek", "Turkish", "Arabic",
            "Hindi", "Chinese (Simplified)", "Japanese", "Korean", "English",
        };
        n = sizeof(L) / sizeof(L[0]);
        return L;
    }

    String langName() const {
        int n;
        auto L = langs(n);
        return L[constrain(lang_, 0, n - 1)];
    }

    void pickLanguage() {
        int n;
        auto L = langs(n);
        std::vector<String> opts;
        for (int i = 0; i < n; i++) opts.push_back(L[i]);
        int pick = ui::chooser("Translate into", opts, lang_);
        if (pick >= 0) {
            lang_ = pick;
            store::setInt("trlang", lang_);
            os::toast(String("into ") + langName(), os::Tone::Good);
        }
        os::invalidate();
    }

    void speakAndTranslate() {
        if (!audio::micReady()) { os::toast("no mic", os::Tone::Bad); return; }
        ui::releaseCanvas();
        audio::setSampleRate(store::getInt("micrate", 16000));
        audio::setHeadroomBytes(ai::preferredStt() != ai::Stt::Host ? 72 * 1024 : 40 * 1024);
        if (theme::sounds()) audio::chirpOk();
        audio::recordStart();
        if (!audio::recording()) {
            os::toast("not enough memory to record", os::Tone::Bad);
            ui::acquireCanvas();
            return;
        }
        while (audio::recordChunk()) {
            ui::beginFrame();
            ui::centered(32, "Listening", ui::c().bad);
            ui::progress(20, 50, SCREEN_W - 40, 12, audio::level(), ui::c().good);
            ui::centered(74, String(audio::recordedSeconds(), 1) + "s  -  any key stops",
                         ui::c().dim);
            ui::statusBar(String("-> ") + langName(), ui::Icon::Mic);
            ui::endFrame();
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) break;
        }
        audio::recordStop();
        size_t n = audio::recordedSamples();
        if (n < audio::sampleRate() / 2) {
            audio::freeBuffer();
            ui::acquireCanvas();
            os::toast("too short", os::Tone::Bad);
            os::invalidate();
            return;
        }
        ai::Result r;
        ui::await("Transcribing", [&] { r = ai::transcribe(audio::pcm(), n); });
        audio::freeBuffer();
        ui::acquireCanvas();
        if (!r.ok) { os::toast(r.error, os::Tone::Bad); os::invalidate(); return; }
        translate(r.text);
    }

    void translate(const String& text) {
        String t = text;
        t.trim();
        if (!t.length()) return;
        ai::Result r;
        String lang = langName();
        ui::await(String("Translating to ") + lang, [&] {
            r = ai::ask(t,
                String("Translate the user's text into ") + lang +
                ". Reply with the translation ONLY - no quotes, no notes, no romanisation "
                "unless the target script is Latin, no markdown.",
                400);
        });
        src_ = t;
        out_ = r.ok ? r.text : ("[" + r.error + "]");
        if (!r.ok) os::toast(r.error, os::Tone::Bad);
        in_ = "";
        scroll_ = 0;
        os::invalidate();
    }

    String in_, out_, src_;
    int lang_ = 0, scroll_ = 0;
};

App* translateApp() { static Translate a; return &a; }
