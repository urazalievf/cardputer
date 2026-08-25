#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/ai.h"
#include "../kernel/store.h"
#include "../kernel/dictate.h"

// Speak, get it back in another language. The single best argument for putting
// a microphone and a language model in the same pocket device.
//
// The screen is the hard part. The built-in glyph set is ASCII, so a Russian or
// Chinese reply used to render as nothing at all. Anything the embedded efont
// covers is now drawn in its own script; anything it does not -- Korean,
// Arabic, Hindi -- comes back with a romanisation that the default font can
// actually draw, which is more use in a conversation than a row of blanks.
class Translate : public App {
public:
    const char* name() const override { return "Translate"; }
    const char* blurb() const override { return "speak"; }
    ui::Icon icon() const override { return ui::Icon::Chat; }

    String title() const override { return String("-> ") + langName(); }

    bool onBack() override {
        if (out_.length()) { clearResult(); os::invalidate(); return true; }
        if (in_.length())  { in_ = ""; os::invalidate(); return true; }
        return false;
    }

    void onEnter() override { lang_ = store::getInt("trlang", 0); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (k.tab) { speakAndTranslate(); return; }
        if (k.enter) { translate(in_); return; }
        if (k.ctrl && k.is('l')) { pickLanguage(); return; }
        if (k.ctrl && k.is('r') && out_.length()) { showRoman_ = !showRoman_; os::invalidate(); return; }
        // fn+arrow only. Bare ';' and '.' carry their character too, and this
        // screen has a text field on it -- stealing them would make those two
        // letters untypeable. lines_ is what the last draw actually laid out,
        // the only honest bound when the font is proportional.
        if (out_.length() && k.chars.empty()) {
            if (k.down) { if (scroll_ < lines_ - 1) scroll_++; os::invalidate(); return; }
            if (k.up)   { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
        }
        if (ui::editBuffer(in_, k, 300)) os::invalidate();
    }

    void draw() override {
        if (out_.length()) return drawResult();

        ui::centered(30, "Speak or type", ui::c().dim);
        ui::centered(46, String("into ") + langName(), ui::c().accent);
        if (!scriptOk_) ui::centered(60, "shown romanised - no font", ui::c().dim);

        bool ready = ai::configured(ai::preferred());
        ui::centered(78, ready ? String("TAB to talk")
                               : String(ai::setupHint(ai::preferred())),
                     ready ? ui::c().accent2 : ui::c().warn);

        ui::inputLine(INPUT_Y, "> ", in_, ui::c().fg);
        ui::hint("TAB talk  Enter go  ctrl+L language  ` back");
    }

private:
    // The input line sits at the bottom and the hint bar sits under it. The
    // result has to stop above both -- it used to be drawn seven rows down from
    // y=36, straight through the prompt and off the panel.
    static const int SRC_Y   = BODY_Y + 1;      // 17
    static const int RULE_Y  = BODY_Y + 12;     // 28
    static const int OUT_Y   = BODY_Y + 17;     // 33
    static const int INPUT_Y = 104;
    static const int OUT_H   = INPUT_Y - 6 - OUT_Y;

    void drawResult() {
        // What was said, dimmed, above the rule.
        ui::text(3, SRC_Y, ui::ellipsize(src_, 39), ui::c().dim);
        ui::gfx().drawFastHLine(0, RULE_Y, SCREEN_W, ui::c().border);

        String body = showRoman_ && roman_.length() ? roman_ : out_;
        if (scriptOk_ && !showRoman_ && !ui::isAscii(body)) {
            // 17px lines: the efont is 16px tall, so the eight-row ASCII layout
            // does not apply here.
            lines_ = ui::unicodePager(body, scroll_, ui::c().accent2,
                                      OUT_H / 17, OUT_Y, 17);
        } else {
            int rows = OUT_H / theme::rowHeight();
            ui::pager(body, scroll_, ui::c().accent2, rows, OUT_Y);
            lines_ = ui::pagerLines(body);
        }

        ui::inputLine(INPUT_Y, "> ", in_, ui::c().fg);
        ui::hint(roman_.length() ? "ctrl+R " + String(showRoman_ ? "script" : "romanised")
                                 + "   TAB talk   ` clear"
                                 : "TAB talk   Enter go   ` clear");
    }

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
            clearResult();
            os::toast(String("into ") + langName(), os::Tone::Good);
        }
        os::invalidate();
    }

    void clearResult() { out_ = ""; src_ = ""; roman_ = ""; scroll_ = 0; showRoman_ = false; }

    void speakAndTranslate() {
        auto d = dictate::run(String("-> ") + langName(), ui::Icon::Mic);
        if (!d.ok) {
            os::toast(d.error, os::Tone::Bad);
            if (d.error.length()) {
                src_ = "";
                out_ = "[" + d.error + "]";
                roman_ = "";
                scriptOk_ = true;
            }
            os::invalidate();
            return;
        }
        translate(d.text);
    }

    void translate(const String& text) {
        String t = text;
        t.trim();
        if (!t.length()) return;

        String lang = langName();
        // Ask for the romanisation in the same round trip. It costs a few
        // tokens and saves a second call on every language this screen cannot
        // draw -- and it is genuinely useful even when it can, because reading
        // a phrase aloud is half the point of a pocket translator.
        String system =
            String("Translate the user's text into ") + lang + ".\n"
            "Reply with exactly two lines and nothing else:\n"
            "1: the translation, in the normal script of the language, no quotes, "
            "no notes, no markdown.\n"
            "2: the same translation romanised into plain unaccented ASCII letters "
            "for an English speaker to read aloud. If the translation is already "
            "plain ASCII, repeat it.";

        ai::Result r;
        ui::await(String("Translating to ") + lang,
                  [&] { r = ai::chat({{"user", t}}, system, 400); });

        src_ = t;
        in_ = "";
        scroll_ = 0;
        showRoman_ = false;

        if (!r.ok) {
            out_ = "[" + r.error + "]";
            if (!ai::configured(ai::preferred()))
                out_ += "\n\n" + String(ai::setupHint(ai::preferred()));
            roman_ = "";
            scriptOk_ = true;
            os::toast(r.error, os::Tone::Bad);
            os::invalidate();
            return;
        }

        splitReply(r.text, out_, roman_);
        // Ask the font, not a table of script ranges: the answer stays right if
        // the font ever changes, and it is the only thing that actually decides
        // whether these glyphs will appear.
        scriptOk_ = ui::renderable(out_);
        if (!scriptOk_) {
            if (roman_.length()) {
                showRoman_ = true;
                os::toast(lang + " has no glyphs - showing romanised", os::Tone::Info);
            } else {
                out_ = "[no font for " + lang + ", and no romanisation came back]";
            }
        }
        os::invalidate();
    }

    // Two lines, but models are inconsistent about numbering them. Take the
    // first non-empty line as the translation and the next as the romanisation,
    // stripping any "1:" / "2." / "-" the model decided to add.
    // Only a real list marker: "1:", "2.", "- ", "* ". Stripping any leading
    // digit would eat the translation of "2 euros" down to "euros".
    static void stripEnumerator(String& line) {
        int i = 0;
        const int n = line.length();
        if (n && (line[0] == '-' || line[0] == '*') && n > 1 && line[1] == ' ') i = 2;
        else {
            while (i < n && isdigit((int)line[i])) i++;
            if (i == 0 || i >= n) return;                     // no digits, or all of them
            if (line[i] != ':' && line[i] != '.' && line[i] != ')') return;
            i++;
        }
        while (i < n && line[i] == ' ') i++;
        // Never strip the whole line away.
        if (i >= n) return;
        line.remove(0, i);
    }

    static void splitReply(const String& reply, String& script, String& roman) {
        script = ""; roman = "";
        int i = 0;
        const int n = reply.length();
        while (i < n && (script.length() == 0 || roman.length() == 0)) {
            int nl = reply.indexOf('\n', i);
            String line = (nl < 0) ? reply.substring(i) : reply.substring(i, nl);
            i = (nl < 0) ? n : nl + 1;
            line.trim();
            stripEnumerator(line);
            if (!line.length()) continue;
            if (!script.length()) script = line;
            else                  roman = line;
        }
        if (!script.length()) script = reply;
        // A model that ignored the format and answered in one line gives us the
        // same string twice; that is not a romanisation worth offering.
        if (roman == script) roman = "";
    }

    String in_, out_, src_, roman_;
    int lang_ = 0, scroll_ = 0, lines_ = 0;
    bool scriptOk_ = true;      // the embedded font can draw out_
    bool showRoman_ = false;
};

App* translateApp() { static Translate a; return &a; }
