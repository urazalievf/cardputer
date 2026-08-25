#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/audio.h"
#include "../kernel/ai.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"
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
        return routeLabel() + "  " + String((int)history_.size()) + " msgs";
    }

    // "Gemini via Mac" reads better than "Mac daemon" when the daemon is just
    // the transport and the account is what matters.
    static String routeLabel() {
        if (ai::preferred() != ai::Provider::Host) return ai::spec(ai::preferred()).label;
        String be = store::getStr("m_host", "claude");
        be.setCharAt(0, toupper(be[0]));
        return be + " via Mac";
    }

    bool onBack() override {
        if (input_.length()) { input_ = ""; return true; }
        return false;
    }

    void onEnter() override { load(); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (k.tab)   { voiceAsk(); return; }
        if (k.enter) { send(); return; }
        if (k.ctrl && k.is('p')) { chooseAssistant(); os::invalidate(); return; }
        if (input_.length() == 0 && k.chars.empty()) {
            if (k.down) { scroll_++; os::invalidate(); return; }
            if (k.up)   { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
            if (k.ctrl && k.is('l')) {
                history_.clear(); lines_.clear(); save();
                os::toast("conversation cleared");
                return;
            }
            if (k.ctrl && k.is('d')) { if (dictate()) os::invalidate(); return; }
        }
        if (ui::editBuffer(input_, k, 400)) { scroll_ = maxScroll(); os::invalidate(); }
    }

    void draw() override {
        const int rows = 8;
        if (scroll_ > maxScroll()) scroll_ = maxScroll();
        if (lines_.empty()) {
            ui::centered(36, "Ask anything", ui::c().dim);
            ui::centered(52, routeLabel(), ui::c().accent);
            ui::centered(68, "TAB to ask out loud", ui::c().accent2);
            if (!ai::configured(ai::preferred()))
                ui::centered(84, "not set up yet - ctrl+P", ui::c().warn);
        }
        for (int i = 0; i < rows && scroll_ + i < (int)lines_.size(); i++)
            ui::text(3, BODY_Y + i * 10, lines_[scroll_ + i].text, lines_[scroll_ + i].color);

        ui::inputLine(104, "> ", input_, ui::c().fg);
        ui::hint("TAB talk  Enter send  ctrl+P who  ctrl+L clear");
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

    // One button: talk, transcribe, send. The whole point of a device with a
    // microphone and 56 keys the size of rice grains.
    void voiceAsk() {
        if (!dictate()) return;
        String heard = input_;
        heard.trim();
        if (!heard.length()) return;
        send();
    }

    bool dictate() {
        if (!audio::micReady()) {
            os::toast("microphone unavailable - see Voice > M", os::Tone::Bad);
            return false;
        }
        ui::releaseCanvas();
        audio::setSampleRate(store::getInt("micrate", 16000));
        audio::setHeadroomBytes(ai::preferredStt() != ai::Stt::Host ? 72 * 1024 : 40 * 1024);
        if (theme::sounds()) audio::chirpOk();
        if (!audio::recordStart()) {
            os::toast(audio::startError(), os::Tone::Bad);
            ui::acquireCanvas();
            return false;
        }
        // The key that started dictation is still down; arm the stop only once
        // it has been released, or the capture ends one chunk in.
        bool armed = false;
        while (audio::recordChunk()) {
            ui::beginFrame();
            ui::centered(34, "Listening", ui::c().bad);
            ui::progress(20, 52, SCREEN_W - 40, 12, audio::level(), ui::c().good);
            ui::centered(76, String(audio::recordedSeconds(), 1) + "s  -  " +
                             (armed ? "any key stops" : "let go to arm stop"),
                         ui::c().dim);
            ui::centered(92, ai::sttLabel(ai::preferredStt()), ui::c().dim);
            ui::statusBar("Dictating", ui::Icon::Mic);
            ui::endFrame();
            M5Cardputer.update();
            bool down = M5Cardputer.Keyboard.isPressed();
            if (!armed && !down) { armed = true; continue; }
            if (armed && down) break;
        }
        audio::recordStop();
        size_t n = audio::recordedSamples();
        if (n < audio::sampleRate() / 2) {
            float secs = (float)n / audio::sampleRate();
            String why = audio::stopReason() == audio::Stop::MicFailed
                       ? "microphone stopped after " + String(secs, 1) + "s"
                       : n == 0 ? String("captured nothing - try Voice > M")
                                : "too short - " + String(secs, 1) + "s";
            audio::freeBuffer();
            ui::acquireCanvas();
            os::toast(why, os::Tone::Bad);
            os::invalidate();
            return false;
        }
        ai::Result r;
        ui::await("Transcribing", [&] { r = ai::transcribe(audio::pcm(), n); });
        audio::freeBuffer();
        ui::acquireCanvas();
        if (!r.ok) { os::toast(r.error, os::Tone::Bad); os::invalidate(); return false; }
        input_ += (input_.length() ? " " : "") + r.text;
        os::invalidate();
        return true;
    }

    void send() {
        String msg = input_;
        msg.trim();
        if (!msg.length()) return;
        input_ = "";
        history_.push_back({"user", msg});
        push("You", msg, ui::c().warn);

        std::vector<ai::Msg> convo;
        size_t start = history_.size() > 12 ? history_.size() - 12 : 0;
        for (size_t i = start; i < history_.size(); i++) convo.push_back(history_[i]);

        ai::Result r;
        ui::await(String("Asking ") + routeLabel(), [&] { r = ai::chat(convo, SYSTEM, 400); });
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

bool chooseAssistant() {
    struct Route { String label; ai::Provider provider; String backend; };
    std::vector<Route> routes;

    // Account-based first: these are logged in on the Mac, so there is no key
    // to paste and nothing sensitive on the handheld.
    auto backends = cloud::hostBackends();
    if (backends.empty() && cloud::hostOnline())
        backends = {"claude", "codex", "gemini"};
    for (auto& b : backends) {
        String pretty = b;
        if (b == "codex") pretty = "ChatGPT";
        else if (b.length()) pretty.setCharAt(0, toupper(b[0]));
        routes.push_back({pretty + "  (Mac login)", ai::Provider::Host, b});
    }

    for (int i = 1; i < (int)ai::Provider::COUNT; i++) {
        ai::Provider p = (ai::Provider)i;
        routes.push_back({String(ai::spec(p).label) + "  " +
                          (ai::configured(p) ? String("(key set)") : String("(needs key)")),
                          p, ""});
    }
    if (routes.empty()) {
        os::toast("no assistant available - add a key in Settings", os::Tone::Bad);
        return false;
    }

    int cur = 0;
    String curBackend = store::getStr("m_host", "claude");
    for (size_t i = 0; i < routes.size(); i++) {
        if (routes[i].provider != ai::preferred()) continue;
        if (routes[i].provider == ai::Provider::Host && routes[i].backend != curBackend) continue;
        cur = i;
        break;
    }

    std::vector<String> labels;
    for (auto& r : routes) labels.push_back(r.label);
    int pick = ui::chooser("Ask who?", labels, cur);
    if (pick < 0) return false;

    ai::setPreferred(routes[pick].provider);
    if (routes[pick].provider == ai::Provider::Host)
        store::setStr("m_host", routes[pick].backend);
    os::toast(String("now asking ") + routes[pick].label, os::Tone::Good);
    return true;
}
