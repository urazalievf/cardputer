#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/audio.h"
#include "../kernel/cloud.h"
#include "../kernel/store.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// Conversational Claude. Routed through the Mac daemon when it's up (your
// Max subscription, no per-token cost), otherwise straight to the API.
class Ask : public App {
    struct Msg { String role, content; };
public:
    const char* name() const override { return "Ask"; }
    const char* blurb() const override { return "chat claude"; }

    String title() const override {
        if (busy_) return "Thinking...";
        return String("Ask  ") + (cloud::hostOnline() ? "[mac]" : "[api]") +
               "  " + String((int)history_.size()) + " msgs";
    }

    bool escExits() const override { return input_.length() == 0; }

    void onEnter() override { load(); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (busy_) return;
        if (k.tab)   { dictate(); return; }
        if (k.enter) { send(); return; }
        if (input_.length() == 0) {
            if (k.down) { scroll_++; os::invalidate(); return; }
            if (k.up)   { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
            if (k.ctrl && k.is('l')) { history_.clear(); lines_.clear(); save(); os::invalidate(); return; }
        }
        if (ui::editBuffer(input_, k, 400)) { scroll_ = maxScroll(); os::invalidate(); }
    }

    void draw() override {
        int rows = 8;
        if (scroll_ > maxScroll()) scroll_ = maxScroll();
        for (int i = 0; i < rows && scroll_ + i < (int)lines_.size(); i++)
            ui::text(2, BODY_Y + i * ROW_H, lines_[scroll_ + i].text,
                     lines_[scroll_ + i].color);

        ui::gfx().drawLine(0, 100, SCREEN_W, 100, ui::DIM);
        ui::inputLine(104, busy_ ? "" : "> ", busy_ ? String("...") : input_,
                      busy_ ? ui::WARN : ui::FG, !busy_);
        ui::hint("Enter send  TAB dictate  ctrl+L clear  ` back");
    }

private:
    struct Line { String text; uint16_t color; };

    int maxScroll() const { return max(0, (int)lines_.size() - 8); }

    void push(const String& who, const String& text, uint16_t color) {
        for (auto& l : ui::wrap(who + ": " + text, CHARS_PER_LINE))
            lines_.push_back({l, color});
        while (lines_.size() > 200) lines_.erase(lines_.begin());
        scroll_ = maxScroll();
    }

    void rebuild() {
        lines_.clear();
        for (auto& m : history_)
            push(m.role == "user" ? "You" : "Claude", m.content,
                 m.role == "user" ? ui::WARN : ui::ACCENT);
    }

    void dictate() {
        if (!audio::micReady()) { os::toast("no mic"); return; }
        audio::chirpOk();
        audio::recordStart();
        // Short blocking capture with a live meter — dictation is a burst, not a session.
        while (audio::recordChunk()) {
            ui::clear();
            ui::centered(40, "Listening...", ui::BAD);
            int w = (int)(audio::level() * (SCREEN_W - 20));
            ui::gfx().drawRect(10, 58, SCREEN_W - 20, 14, ui::DIM);
            ui::gfx().fillRect(11, 59, max(0, w - 2), 12, ui::GOOD);
            ui::centered(84, String(audio::recordedSeconds(), 1) + "s  -  any key to stop", ui::DIM);
            ui::statusBar("Dictating");
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) break;
        }
        audio::recordStop();
        if (audio::recordedSamples() < audio::SAMPLE_RATE / 2) {
            os::toast("too short");
            os::invalidate();
            return;
        }
        busy_ = true;
        os::invalidate();
        ui::clear(); ui::centered(58, "Transcribing...", ui::WARN); ui::statusBar("Ask");
        auto r = cloud::transcribe(audio::pcm(), audio::recordedSamples());
        busy_ = false;
        if (r.ok) { input_ += (input_.length() ? " " : "") + r.text; }
        else      { os::toast(r.error); }
        os::invalidate();
    }

    void send() {
        String msg = input_;
        msg.trim();
        if (!msg.length()) return;
        input_ = "";
        history_.push_back({"user", msg});
        push("You", msg, ui::WARN);

        busy_ = true;
        os::invalidate();
        ui::clear(); draw(); ui::statusBar("Ask");

        // Replay a bounded slice of the conversation so context survives.
        String convo;
        size_t start = history_.size() > 12 ? history_.size() - 12 : 0;
        for (size_t i = start; i < history_.size(); i++)
            convo += (history_[i].role == "user" ? "User: " : "Assistant: ") + history_[i].content + "\n";

        auto r = cloud::ask(convo + "Assistant:", SYSTEM, 400);
        String reply = r.ok ? r.text : ("[" + r.error + "]");
        history_.push_back({"assistant", reply});
        push("Claude", reply, r.ok ? ui::ACCENT : ui::BAD);
        if (r.ok) os::toast(String("via ") + r.sourceName());

        while (history_.size() > 24) history_.erase(history_.begin());
        save();
        busy_ = false;
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
        String json = p.getString("hist", "");
        p.end();
        JsonDocument doc;
        if (!json.length() || deserializeJson(doc, json)) return;
        for (JsonObject o : doc.as<JsonArray>())
            history_.push_back({o["r"].as<String>(), o["c"].as<String>()});
        rebuild();
    }

    static constexpr const char* SYSTEM =
        "You are the assistant built into CardputerOS, running on a pocket handheld "
        "with a 240x135 screen. Reply in PLAIN TEXT only - no markdown, no code fences, "
        "no bullet lists. Be sharp and concise; stay under 400 characters unless asked "
        "for detail.";

    std::vector<Msg> history_;
    std::vector<Line> lines_;
    String input_;
    int scroll_ = 0;
    bool busy_ = false, loaded_ = false;
};

App* askApp() { static Ask a; return &a; }
