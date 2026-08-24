#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/store.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"
#include "../kernel/audio.h"
#include <esp_heap_caps.h>

// Everything configurable lives here and lands in NVS. Nothing sensitive is
// ever written to the repo — you type the keys once on the device.
class Settings : public App {
    enum Kind { STR, SECRET, INT, ACTION, INFO };
    struct Field {
        const char* label;
        const char* key;
        Kind kind;
        const char* def;
    };
    enum Mode { LIST, EDIT };

public:
    const char* name() const override { return "Settings"; }
    const char* blurb() const override { return "keys + host"; }

    String title() const override {
        return mode_ == EDIT ? String("Edit ") + fields()[sel_].label : String("Settings");
    }

    bool escExits() const override { return mode_ == LIST; }

    void onEnter() override { mode_ = LIST; os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        const auto& f = fields();
        if (mode_ == EDIT) {
            if (k.esc)   { mode_ = LIST; os::invalidate(); return; }
            if (k.enter) { commit(); return; }
            if (ui::editBuffer(buf_, k, 200)) os::invalidate();
            return;
        }
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)f.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter) {
            if (f[sel_].kind == ACTION) { runAction(f[sel_].key); return; }
            if (f[sel_].kind == INFO)   return;
            buf_ = (f[sel_].kind == SECRET) ? String("") : store::getStr(f[sel_].key, f[sel_].def);
            mode_ = EDIT;
            os::invalidate();
            return;
        }
        if (k.is('x') && f[sel_].kind != ACTION && f[sel_].kind != INFO) {
            store::remove(f[sel_].key);
            os::toast(String(f[sel_].label) + " cleared");
        }
    }

    void draw() override {
        const auto& f = fields();
        if (mode_ == EDIT) {
            ui::text(2, BODY_Y, f[sel_].label, ui::ACCENT);
            if (f[sel_].kind == SECRET)
                ui::text(2, BODY_Y + 12, "typing replaces the stored value", ui::DIM);
            ui::inputLine(BODY_Y + 34, "> ", f[sel_].kind == SECRET ? mask(buf_) : buf_, ui::FG);
            ui::hint("Enter save   ` cancel");
            return;
        }

        std::vector<String> rows;
        for (auto& fl : f) {
            String v;
            switch (fl.kind) {
                case SECRET: {
                    String s = store::getStr(fl.key, "");
                    v = s.length() ? ("set (" + String((int)s.length()) + ")") : "not set";
                    break;
                }
                case STR:    v = store::getStr(fl.key, fl.def); if (!v.length()) v = "-"; break;
                case INT:    v = String(store::getInt(fl.key, atoi(fl.def)));               break;
                case ACTION: v = ">";                                                       break;
                case INFO:   v = infoValue(fl.key);                                         break;
            }
            String row = String(fl.label);
            while (row.length() < 15) row += ' ';
            rows.push_back(row + ui::ellipsize(v, 22));
        }
        scroll_ = ui::drawList(rows, sel_, scroll_, 9, BODY_Y);
        ui::hint("Enter edit   X clear   ` back");
    }

private:
    static String mask(const String& s) {
        String m;
        for (size_t i = 0; i < s.length(); i++) m += (i + 4 >= s.length()) ? s[i] : '*';
        return m;
    }

    static const std::vector<Field>& fields() {
        static const std::vector<Field> f = {
            {"Mac host",   store::K_HOST,      STR,    ""},
            {"Mac port",   store::K_HOST_PORT, INT,    "8787"},
            {"Find Mac",   "act_discover",     ACTION, ""},
            {"Test Mac",   "act_ping",         ACTION, ""},
            {"Anthropic",  store::K_ANTHROPIC, SECRET, ""},
            {"OpenAI",     store::K_OPENAI,    SECRET, ""},
            {"Model",      store::K_MODEL,     STR,    "claude-haiku-4-5-20251001"},
            {"Vault dir",  store::K_VAULT,     STR,    "Cardputer"},
            {"Timezone",   store::K_TZ,        STR,    "EST5EDT,M3.2.0,M11.1.0"},
            {"Brightness", store::K_BRIGHT,    INT,    "120"},
            {"Sync clock", "act_ntp",          ACTION, ""},
            {"Forget WiFi","act_wipewifi",     ACTION, ""},
            {"IP",         "inf_ip",           INFO,   ""},
            {"Free heap",  "inf_heap",         INFO,   ""},
            {"Mic buffer", "inf_mic",          INFO,   ""},
            {"SD card",    "inf_sd",           INFO,   ""},
            {"Version",    "inf_ver",          INFO,   ""},
        };
        return f;
    }

    static String infoValue(const String& key) {
        if (key == "inf_ip")   return net::connected() ? net::ip() : String("offline");
        if (key == "inf_heap") return String(ESP.getFreeHeap() / 1024) + "K";
        if (key == "inf_mic")  return audio::micReady()
                                      ? String((unsigned)audio::capacitySeconds()) + "s"
                                      : String("unavailable");
        if (key == "inf_sd")   return store::sdReady()
                                      ? String((int)store::sdUsedMB()) + "/" + String((int)store::sdTotalMB()) + "MB"
                                      : String("none");
        if (key == "inf_ver")  return CARDPUTER_OS_VERSION;
        return "";
    }

    void commit() {
        const auto& f = fields()[sel_];
        String v = buf_;
        v.trim();
        if (f.kind == INT) store::setInt(f.key, v.toInt());
        else if (v.length() || f.kind != SECRET) store::setStr(f.key, v);

        if (strcmp(f.key, store::K_BRIGHT) == 0)
            M5Cardputer.Display.setBrightness(constrain(v.toInt(), 10, 255));
        if (strcmp(f.key, store::K_TZ) == 0) net::syncTime();
        if (strcmp(f.key, store::K_HOST) == 0 || strcmp(f.key, store::K_HOST_PORT) == 0)
            cloud::begin();

        os::toast(String(f.label) + " saved");
        mode_ = LIST;
        os::invalidate();
    }

    void runAction(const String& key) {
        if (key == "act_discover") {
            os::toast("searching...");
            ui::clear(); draw(); ui::statusBar("Settings");
            os::toast(cloud::discoverHost()
                          ? "found " + store::getStr(store::K_HOST, "")
                          : "no daemon advertised");
        } else if (key == "act_ping") {
            os::toast("pinging...");
            ui::clear(); draw(); ui::statusBar("Settings");
            os::toast(cloud::pingHost(3000) ? "daemon OK" : "no answer");
        } else if (key == "act_ntp") {
            net::syncTime();
            os::toast(net::timeValid() ? "clock " + net::clockString() : "NTP failed");
        } else if (key == "act_wipewifi") {
            if (ui::confirm("Forget every saved WiFi network?")) {
                for (auto& s : net::savedNetworks()) net::forgetNetwork(s);
                os::toast("all networks forgotten");
            }
        }
        os::invalidate();
    }

    Mode mode_ = LIST;
    int sel_ = 0, scroll_ = 0;
    String buf_;
};

App* settingsApp() { static Settings a; return &a; }
