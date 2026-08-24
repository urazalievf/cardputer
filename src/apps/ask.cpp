#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/audio.h"
#include "../kernel/ai.h"
#include "../kernel/store.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// Conversational assistant, whichever vendor you pointed it at. TAB swaps
// provider mid-conversation without losing the thread.
class Ask : public App {
public:
    const char* name() const override { return "Ask"; }
    const char* blurb() const override { return "chat"; }
    ui::Icon icon() const override { return ui::Icon::Chat; }

    String title() const override {
        return String(ai::spec(ai::preferred()).label) + "  " +
               String((int)history_.size()) + " msgs";
    }

    bool onBack() override {
        if (input_.length()) { input_ = ""; return true; }
        return false;
    }

    void onEnter() override { load(); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (k.tab)   { pickProvider(); return; }
        if (k.enter) { send(); return; }
        if (input_.length() == 0) {
            if (k.down) { scroll_++; os::invalidate(); return; }
            if (k.up)   { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
            if (k.ctrl && k.is('l')) {
                history_.clear(); lines_.clear(); save();
                os::toast("conversation cleared");
                return;
            }
            if (k.ctrl && k.is('d')) { dictate(); return; }
        }
        if (ui::editBuffer(input_, k, 400)) { scroll_ = maxScroll(); os::invalidate(); }
    }

    void draw() override {
        const int rows = 8;
        if (scroll_ > maxScroll()) scroll_ = maxScroll();
        if (lines_.empty()) {
            ui::centered(40, "Ask anything", ui::c().dim);
            ui::centered(56, ai::spec(ai::preferred()).label, ui::c().accent);
            if (!ai::configured(ai::preferred()))
                ui::centered(70, "not configured - Settings", ui::c().warn);
        }
        for (int i = 0; i < rows && scroll_ + i < (int)lines_.size(); i++)
            ui::text(3, BODY_Y + i * 10, lines_[scroll_ + i].text, lines_[scroll_ + i].color);

        ui::inputLine(104, "> ", input_, ui::c().fg);
        ui::hint("Enter send  TAB provider  ctrl+D dictate  ctrl+L clear");
    }

private:
    struct Line { String text; uint16_t color; };

    int maxScroll() const { return max(0, (int)lines_.size() - 8); }

    void push(const String& who, const String& text, uint16_t color) {
        for (auto& l : ui::wrap(who + ": " + text, 38)) lines_.push_back({l, color});
        while (lines_.size() > 200) lines_.erase(lines_.begin());
        scroll_ = maxScroll();
    }

    void rebuild() {
        lines_.clear();
        for (auto& m : history_)
            push(m.role == "user" ? "You" : "AI", m.content,
                 m.role == "user" ? ui::c().warn : ui::c().accent2);
    }

    void pickProvider() {
        std::vector<String> opts;
        for (int i = 0; i < (int)ai::Provider::COUNT; i++) {
            ai::Provider p = (ai::Provider)i;
            opts.push_back(String(ai::spec(p).label) + "  " +
                           (ai::configured(p) ? ai::model(p) : String("(not set up)")));
        }
        int pick = ui::chooser("Assistant", opts, (int)ai::preferred());
        if (pick >= 0) {
            ai::setPreferred((ai::Provider)pick);
            os::toast(String("now using ") + ai::spec((ai::Provider)pick).label, os::Tone::Good);
        }
        os::invalidate();
    }

    void dictate() {
        if (!audio::micReady()) { os::toast("no mic", os::Tone::Bad); return; }
        ui::releaseCanvas();
        if (theme::sounds()) audio::chirpOk();
        audio::recordStart();
        if (!audio::recording()) {
            os::toast("not enough memory to record", os::Tone::Bad);
            ui::acquireCanvas();
            return;
        }
        while (audio::recordChunk()) {
            ui::beginFrame();
            ui::centered(34, "Listening", ui::c().bad);
            ui::progress(20, 52, SCREEN_W - 40, 12, audio::level(), ui::c().good);
            ui::centered(76, String(audio::recordedSeconds(), 1) + "s  -  any key stops",
                         ui::c().dim);
            ui::statusBar("Dictating", ui::Icon::Mic);
            ui::endFrame();
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) break;
        }
        audio::recordStop();
        size_t n = audio::recordedSamples();
        if (n < audio::SAMPLE_RATE / 2) {
            audio::freeBuffer();
            ui::acquireCanvas();
            os::toast("too short", os::Tone::Bad);
            os::invalidate();
            return;
        }
        ui::busy("Transcribing");
        auto r = ai::transcribe(audio::pcm(), n);
        audio::freeBuffer();
        ui::acquireCanvas();
        if (r.ok) input_ += (input_.length() ? " " : "") + r.text;
        else      os::toast(r.error, os::Tone::Bad);
        os::invalidate();
    }

    void send() {
        String msg = input_;
        msg.trim();
        if (!msg.length()) return;
        input_ = "";
        history_.push_back({"user", msg});
        push("You", msg, ui::c().warn);

        ui::busy(String("Asking ") + ai::spec(ai::preferred()).label);

        std::vector<ai::Msg> convo;
        size_t start = history_.size() > 12 ? history_.size() - 12 : 0;
        for (size_t i = start; i < history_.size(); i++) convo.push_back(history_[i]);

        auto r = ai::chat(convo, SYSTEM, 400);
        String reply = r.ok ? r.text : ("[" + r.error + "]");
        history_.push_back({"assistant", reply});
        push(r.ok ? String(r.usedLabel()) : String("error"), reply,
             r.ok ? ui::c().accent2 : ui::c().bad);
        if (r.ok) os::toast(String(r.usedLabel()) + "  " + String(r.ms / 1000.0f, 1) + "s",
                            os::Tone::Good);
        else      os::toast(r.error, os::Tone::Bad);

        while (history_.size() > 24) history_.erase(history_.begin());
        save();
        os::invalidate();
    }

    void save() {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (auto& m : history_) {
            JsonObject o = arr.add<JsonObject>();
            o["r"] = m.role;
            o["c"] = m.content;
        }
        String json;
        serializeJson(doc, json);
        Preferences p;
        if (!p.begin("chat", false)) return;
        p.putString("hist", json);
        p.end();
    }

    void load() {
        if (loaded_) return;
        loaded_ = true;
        Preferences p;
        if (!p.begin("chat", true)) return;
        String json = p.isKey("hist") ? p.getString("hist", "") : String("");
        p.end();
        JsonDocument doc;
        if (!json.length() || deserializeJson(doc, json)) return;
        for (JsonObject o : doc.as<JsonArray>())
            history_.push_back({o["r"].as<String>(), o["c"].as<String>()});
        rebuild();
    }

    static constexpr const char* SYSTEM =
        "You are the assistant built into CardputerOS, on a pocket handheld with a "
        "240x135 screen. Reply in PLAIN TEXT only - no markdown, no code fences, no "
        "bullet lists. Be sharp and concise; stay under 400 characters unless asked "
        "for detail.";

    std::vector<ai::Msg> history_;
    std::vector<Line> lines_;
    String input_;
    int scroll_ = 0;
    bool loaded_ = false;
};

App* askApp() { static Ask a; return &a; }
