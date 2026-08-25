#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"
#include "../kernel/ai.h"
#include "../kernel/audio.h"
#include "../kernel/bt.h"
#include "../kernel/hw.h"
#include "../kernel/usbdisk.h"
#include <esp_heap_caps.h>

// Everything the OS can be told. Entries are declared as data so adding a
// preference is one line, and every value lands in NVS immediately.
class Settings : public App {
    enum Kind : uint8_t { HEADER, TOGGLE, SLIDER, ENUMSEL, STR, SECRET, ACTION, INFO, HUE,
                          LAUNCH,   // opens another app; key is its name()
                          ROUTE };  // the "ask who?" picker
    struct Entry {
        Kind kind;
        const char* label;
        const char* key;      // NVS key, or an action/info id
        int lo = 0, hi = 0, step = 1;
        const char* def = "";
    };
    enum Mode : uint8_t { LIST, EDIT, HUEPICK, KEYTEST, COLORS, COLORPICK };

public:
    const char* name() const override { return "Settings"; }
    const char* blurb() const override { return "make it yours"; }
    ui::Icon icon() const override { return ui::Icon::Gear; }

    String title() const override {
        if (mode_ == EDIT)    return String("Edit ") + entries()[sel_].label;
        if (mode_ == HUEPICK) return "Accent colour";
        if (mode_ == KEYTEST) return "Keyboard test";
        if (mode_ == COLORS)  return String("Colours  ") + theme::presetName(theme::preset());
        if (mode_ == COLORPICK) return theme::colorRoleName(role_);
        return "Settings";
    }

    bool onBack() override {
        if (mode_ == COLORPICK) { theme::setColorRole(role_, original_); mode_ = COLORS; return true; }
        if (mode_ == COLORS)    { mode_ = LIST; return true; }
        if (mode_ != LIST)      { mode_ = LIST; return true; }
        return false;
    }

    void onEnter() override { mode_ = LIST; os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        const auto& e = entries();
        if (mode_ == EDIT)    return keyEdit(k);
        if (mode_ == HUEPICK) return keyHue(k);
        if (mode_ == KEYTEST) { last_ = k; os::invalidate(); return; }
        if (mode_ == COLORS)    return keyColors(k);
        if (mode_ == COLORPICK) return keyColorPick(k);

        if (k.up   || k.is('k')) { moveSel(-1); return; }
        if (k.down || k.is('j')) { moveSel(+1); return; }

        const Entry& it = e[sel_];
        // Left/right adjust in place: faster than opening an editor for a
        // slider or a short enum.
        if (k.left)  { nudge(it, -1); return; }
        if (k.right) { nudge(it, +1); return; }
        if (k.enter || k.space) { activate(it); return; }
        if (k.is('x') && (it.kind == STR || it.kind == SECRET)) {
            store::remove(it.key);
            os::toast(String(it.label) + " cleared", os::Tone::Good);
        }
    }

    void draw() override {
        if (mode_ == EDIT)    return drawEdit();
        if (mode_ == HUEPICK) return drawHue();
        if (mode_ == KEYTEST)   return drawKeyTest();
        if (mode_ == COLORS)    return drawColors();
        if (mode_ == COLORPICK) return drawColorPick();

        const auto& e = entries();
        std::vector<ui::Row> rows;
        rows.reserve(e.size());
        for (auto& it : e) {
            ui::Row r;
            if (it.kind == HEADER) {
                r.label = String("- ") + it.label + " -";
                r.tint = ui::c().accent;
            } else {
                r.label = it.label;
                r.detail = valueOf(it);
            }
            rows.push_back(r);
        }
        scroll_ = ui::listView(rows, sel_, scroll_);
        ui::hint("Enter open   < > adjust   X clear   ` back");
    }

private:
    // ---------- table ----------
    static const std::vector<Entry>& entries() {
        static const std::vector<Entry> e = {
            {HEADER, "Appearance", ""},
            {ENUMSEL,"Theme",       "thpreset"},
            {HUE,    "Accent",      "thhue"},
            {ACTION, "Edit colours","a_colors"},
            {ACTION, "Reset colours","a_resetcolors"},
            {SLIDER, "Brightness",  "bright",   10, 255, 15},
            {TOGGLE, "Big text",    "thbig"},
            {TOGGLE, "Show hints",  "thhints"},
            {TOGGLE, "Status clock","thclock"},
            {TOGGLE, "Sounds",      "thsound"},

            {HEADER, "Connectivity", ""},
            {LAUNCH, "WiFi networks","WiFi"},
            {LAUNCH, "Bluetooth",   "Bluetooth"},
            {STR,    "Mac host",    "host"},
            {SLIDER, "Mac port",    "hostport", 1, 65535, 1, "8787"},
            {ACTION, "Find Mac",    "a_discover"},
            {ACTION, "Test Mac",    "a_ping"},
            {STR,    "Bluetooth name","btname",  0,0,1, "CardputerOS"},
            {ACTION, "Forget WiFi", "a_wipewifi"},
            {ACTION, "Scan Grove port","a_i2c"},
            {ACTION, "USB drive",   "a_usb"},

            {HEADER, "Assistant", ""},
            {ROUTE,  "Ask who?",    "route"},
            {TOGGLE, "Auto fallback","aifall"},
            {ENUMSEL,"Speech input","aistt"},
            {STR,    "Claude model","m_anthropic", 0,0,1, "claude-haiku-4-5-20251001"},
            {STR,    "GPT model",   "m_openai",    0,0,1, "gpt-4o-mini"},
            {STR,    "Gemini model","m_gemini",    0,0,1, "gemini-2.0-flash"},
            {STR,    "Groq model",  "m_groq",      0,0,1, "llama-3.3-70b-versatile"},
            {STR,    "Router model","m_openrtr",   0,0,1, "anthropic/claude-3.5-haiku"},
            {STR,    "Ollama model","m_ollama",    0,0,1, "llama3.2"},
            {STR,    "Mac backend", "m_host",      0,0,1, "claude"},

            {HEADER, "API keys", ""},
            {SECRET, "Claude key",  "k_anthropic"},
            {SECRET, "OpenAI key",  "k_openai"},
            {SECRET, "Gemini key",  "k_gemini"},
            {SECRET, "Groq key",    "k_groq"},
            {SECRET, "OpenRouter",  "k_openrtr"},
            {STR,    "Ollama host", "ollamahost"},

            {HEADER, "Notes & vault", ""},
            {STR,    "Vault folder","vault",     0,0,1, "Cardputer"},
            {ACTION, "Remount SD",  "a_sd"},

            {HEADER, "System", ""},
            {STR,    "Timezone",    "tz",        0,0,1, "EST5EDT,M3.2.0,M11.1.0"},
            {ACTION, "Sync clock",  "a_ntp"},
            {ACTION, "Keyboard test","a_keytest"},
            {ACTION, "Test RGB LED","a_led"},
            {SLIDER, "IR LED pin",  "irpin",     0, 48, 1, "44"},
            {ACTION, "Factory reset","a_reset"},

            {HEADER, "About", ""},
            {INFO,   "Version",     "i_ver"},
            {INFO,   "IP",          "i_ip"},
            {INFO,   "Free heap",   "i_heap"},
            {INFO,   "Canvas",      "i_canvas"},
            {INFO,   "Mic buffer",  "i_mic"},
            {INFO,   "SD card",     "i_sd"},
            {INFO,   "USB drive",   "i_usb"},
            {INFO,   "Bluetooth",   "i_bt"},
            {INFO,   "Daemon",      "i_host"},
        };
        return e;
    }

    void moveSel(int dir) {
        const auto& e = entries();
        int n = (int)e.size();
        do {
            sel_ = (sel_ + dir + n) % n;
        } while (e[sel_].kind == HEADER);          // headers are never focusable
        os::invalidate();
    }

    // ---------- values ----------
    static String enumValue(const char* key) {
        if (!strcmp(key, "thpreset")) return theme::presetName(theme::preset());
        if (!strcmp(key, "aiprov"))   return ai::spec(ai::preferred()).label;
        if (!strcmp(key, "aistt"))    return ai::sttLabel(ai::preferredStt());
        return "?";
    }

    static String valueOf(const Entry& it) {
        switch (it.kind) {
            case TOGGLE:  return store::getInt(it.key, defaultToggle(it.key)) ? "on" : "off";
            case SLIDER:  return String(store::getInt(it.key, atoi(it.def[0] ? it.def : "0")));
            case ENUMSEL: return enumValue(it.key);
            case HUE: {
                int h = theme::accentHue();
                return h > 255 ? String("theme") : String(h);
            }
            case STR: {
                String v = store::getStr(it.key, it.def);
                return v.length() ? ui::ellipsize(v, 16) : String("-");
            }
            case SECRET: {
                String v = store::getStr(it.key, "");
                return v.length() ? String("set:") + (int)v.length() : String("not set");
            }
            case ACTION:  return ">";
            case LAUNCH:  return launchDetail(it.key);
            case ROUTE:   return routeDetail();
            case INFO:    return info(it.key);
            default:      return "";
        }
    }

    static String launchDetail(const String& appName) {
        if (appName == "WiFi")
            return net::connected() ? ui::ellipsize(net::ssid(), 14) : String("offline");
        if (appName == "Bluetooth") return bt::status();
        return ">";
    }

    static String routeDetail() {
        if (ai::preferred() != ai::Provider::Host) return ai::spec(ai::preferred()).label;
        String be = store::getStr("m_host", "claude");
        if (be == "codex") return "ChatGPT/Mac";
        if (be.length()) be.setCharAt(0, toupper(be[0]));
        return be + "/Mac";
    }

    static int defaultToggle(const char* key) {
        if (!strcmp(key, "thbig")) return 0;
        return 1;                    // hints, clock, sounds, fallback default on
    }

    static String info(const String& key) {
        if (key == "i_ver")    return CARDPUTER_OS_VERSION;
        if (key == "i_ip")     return net::connected() ? net::ip() : String("offline");
        if (key == "i_heap")   return String(ESP.getFreeHeap() / 1024) + "K";
        if (key == "i_canvas") return ui::canvasActive() ? "on" : "direct";
        if (key == "i_mic")    return audio::micReady()
                                      ? String((unsigned)audio::capacitySeconds()) + "s max"
                                      : String("unavailable");
        if (key == "i_usb")    return usbdisk::available()
                                      ? (usbdisk::attached() ? String("host has it")
                                                             : String((int)usbdisk::sizeMB()) + "MB ready")
                                      : String("no card at boot");
        if (key == "i_sd")     return store::sdReady()
                                      ? String((int)store::sdUsedMB()) + "/" +
                                        String((int)store::sdTotalMB()) + "MB"
                                      : String("none / not FAT32");
        if (key == "i_bt")     return bt::status();
        if (key == "i_host")   return cloud::hostOnline()
                                      ? (cloud::hostFeatures().length() ? cloud::hostFeatures()
                                                                        : String("online"))
                                      : String("offline");
        return "";
    }

    // ---------- interaction ----------
    void nudge(const Entry& it, int dir) {
        switch (it.kind) {
            case TOGGLE: {
                bool v = !store::getInt(it.key, defaultToggle(it.key));
                applyToggle(it.key, v);
                break;
            }
            case SLIDER: {
                int cur = store::getInt(it.key, atoi(it.def[0] ? it.def : "0"));
                int v = constrain(cur + dir * it.step, it.lo, it.hi);
                store::setInt(it.key, v);
                if (!strcmp(it.key, "bright")) {
                    theme::setBrightness((uint8_t)v);
                    M5Cardputer.Display.setBrightness(theme::brightness());
                }
                break;
            }
            case ENUMSEL: cycleEnum(it.key, dir); break;
            case HUE: {
                int h = theme::accentHue();
                h = (h > 255) ? 0 : h + dir * 8;
                if (h < 0) h = 256;               // wrap back to "theme default"
                if (h > 255) h = 256;
                theme::setAccentHue(h);
                break;
            }
            default: return;
        }
        os::invalidate();
    }

    static void applyToggle(const String& key, bool v) {
        if (key == "thbig")        theme::setBigText(v);
        else if (key == "thhints") theme::setShowHints(v);
        else if (key == "thclock") theme::setStatusClock(v);
        else if (key == "thsound") theme::setSounds(v);
        else if (key == "aifall")  ai::setAutoFallback(v);
        else store::setInt(key.c_str(), v ? 1 : 0);
    }

    static void cycleEnum(const String& key, int dir) {
        if (key == "thpreset") { theme::setPreset(theme::preset() + dir); return; }
        if (key == "aiprov") {
            int n = (int)ai::Provider::COUNT;
            int v = ((int)ai::preferred() + dir + n) % n;
            ai::setPreferred((ai::Provider)v);
            return;
        }
        if (key == "aistt") {
            int n = (int)ai::Stt::COUNT;
            int v = ((int)ai::preferredStt() + dir + n) % n;
            ai::setPreferredStt((ai::Stt)v);
            return;
        }
    }

    void activate(const Entry& it) {
        switch (it.kind) {
            case TOGGLE:
            case SLIDER:
                nudge(it, +1);
                return;
            case HUE:
                mode_ = HUEPICK;
                hue_ = theme::accentHue() > 255 ? 0 : theme::accentHue();
                os::invalidate();
                return;
            case ENUMSEL:  return openEnumChooser(it);
            case LAUNCH:   os::launchByName(it.key); return;
            case ROUTE:    chooseAssistant(); os::invalidate(); return;
            case STR:
            case SECRET:
                buf_ = (it.kind == SECRET) ? String("") : store::getStr(it.key, it.def);
                mode_ = EDIT;
                os::invalidate();
                return;
            case ACTION:   return runAction(it.key);
            default:       return;
        }
    }

    void openEnumChooser(const Entry& it) {
        String key = it.key;
        std::vector<String> opts;
        int cur = 0;
        if (key == "thpreset") {
            for (int i = 0; i < theme::presetCount(); i++) opts.push_back(theme::presetName(i));
            cur = theme::preset();
        } else if (key == "aiprov") {
            for (int i = 0; i < (int)ai::Provider::COUNT; i++) {
                ai::Provider p = (ai::Provider)i;
                opts.push_back(String(ai::spec(p).label) +
                               (ai::configured(p) ? "  *" : "  (not set up)"));
            }
            cur = (int)ai::preferred();
        } else if (key == "aistt") {
            for (int i = 0; i < (int)ai::Stt::COUNT; i++) {
                ai::Stt s = (ai::Stt)i;
                opts.push_back(String(ai::sttLabel(s)) +
                               (ai::sttConfigured(s) ? "  *" : "  (not set up)"));
            }
            cur = (int)ai::preferredStt();
        }
        int pick = ui::chooser(it.label, opts, cur);
        if (pick >= 0) {
            if (key == "thpreset")    theme::setPreset(pick);
            else if (key == "aiprov") ai::setPreferred((ai::Provider)pick);
            else if (key == "aistt")  ai::setPreferredStt((ai::Stt)pick);
            os::toast(String(it.label) + ": " + enumValue(it.key), os::Tone::Good);
        }
        os::invalidate();
    }

    void keyEdit(const KeyEvent& k) {
        if (k.enter) { commit(); return; }
        if (ui::editBuffer(buf_, k, 220)) os::invalidate();
    }

    void drawEdit() {
        const Entry& it = entries()[sel_];
        ui::text(4, BODY_Y + 4, it.label, ui::c().accent);
        if (it.kind == SECRET)
            ui::text(4, BODY_Y + 16, "stored in NVS, never in the repo", ui::c().dim);
        else if (it.def[0])
            ui::text(4, BODY_Y + 16, ui::ellipsize(String("default: ") + it.def, 38), ui::c().dim);
        ui::inputLine(BODY_Y + 34, "> ", it.kind == SECRET ? mask(buf_) : buf_, ui::c().fg);
        ui::text(4, BODY_Y + 58, String(buf_.length()) + " chars", ui::c().dim);
        ui::hint("Enter save   ` cancel   ctrl+Bksp word");
    }

    static String mask(const String& s) {
        String m;
        for (size_t i = 0; i < s.length(); i++) m += (i + 4 >= s.length()) ? s[i] : '*';
        return m;
    }

    void commit() {
        const Entry& it = entries()[sel_];
        String v = buf_;
        v.trim();
        if (it.kind == SECRET && !v.length()) { mode_ = LIST; os::invalidate(); return; }
        store::setStr(it.key, v);
        if (!strcmp(it.key, "tz")) net::syncTime();
        if (!strcmp(it.key, "host")) cloud::begin();
        os::toast(String(it.label) + " saved", os::Tone::Good);
        mode_ = LIST;
        os::invalidate();
    }

    // ---------- colour editor ----------
    void keyColors(const KeyEvent& k) {
        int n = theme::colorRoleCount();
        if (k.up   || k.is('k')) { colorSel_ = (colorSel_ - 1 + n) % n; os::invalidate(); return; }
        if (k.down || k.is('j')) { colorSel_ = (colorSel_ + 1) % n;     os::invalidate(); return; }
        if (k.enter || k.space) {
            role_ = colorSel_;
            original_ = theme::colorRole(role_);
            theme::toHsv(original_, h_, s_, v_);
            bar_ = 0;
            mode_ = COLORPICK;
            os::invalidate();
        }
    }

    void drawColors() {
        int n = theme::colorRoleCount();
        int rows = theme::bodyRows();
        if (colorSel_ < colorScroll_) colorScroll_ = colorSel_;
        if (colorSel_ >= colorScroll_ + rows) colorScroll_ = colorSel_ - rows + 1;
        int rh = theme::rowHeight();

        for (int i = 0; i < rows && colorScroll_ + i < n; i++) {
            int idx = colorScroll_ + i;
            int y = BODY_Y + i * rh;
            bool sel = idx == colorSel_;
            if (sel) ui::panel(1, y - 1, SCREEN_W - 5, rh, ui::c().selbg, 3);
            // The swatch is the point: show the colour, not its hex value.
            ui::gfx().fillRoundRect(5, y, 14, rh - 3, 2, theme::colorRole(idx));
            ui::gfx().drawRoundRect(5, y, 14, rh - 3, 2, ui::c().border);
            ui::text(24, y, theme::colorRoleName(idx), sel ? ui::c().selfg : ui::c().fg);
        }
        ui::hint("Enter edit   ` back");
    }

    void keyColorPick(const KeyEvent& k) {
        if (k.esc) { theme::setColorRole(role_, original_); mode_ = COLORS; os::invalidate(); return; }
        if (k.enter) { os::toast("colour saved", os::Tone::Good); mode_ = COLORS; os::invalidate(); return; }
        if (k.up   || k.is('k')) { bar_ = (bar_ + 2) % 3; os::invalidate(); return; }
        if (k.down || k.is('j')) { bar_ = (bar_ + 1) % 3; os::invalidate(); return; }
        int step = k.shift ? 16 : 4;
        int delta = (k.right || k.is('l')) ? step : (k.left || k.is('h')) ? -step : 0;
        if (!delta) return;
        if (bar_ == 0) h_ = (uint8_t)((h_ + delta + 256) % 256);
        else if (bar_ == 1) s_ = (uint8_t)constrain((int)s_ + delta, 0, 255);
        else v_ = (uint8_t)constrain((int)v_ + delta, 0, 255);
        theme::setColorRole(role_, theme::fromHsv(h_, s_, v_));
        os::invalidate();
    }

    void drawColorPick() {
        auto& g = ui::gfx();
        const char* names[] = {"hue", "saturation", "brightness"};
        int vals[] = {h_, s_, v_};

        for (int b = 0; b < 3; b++) {
            int y = BODY_Y + 4 + b * 22;
            for (int x = 0; x < SCREEN_W - 40; x++) {
                uint8_t t = (uint8_t)(x * 255 / (SCREEN_W - 41));
                uint16_t col = b == 0 ? theme::fromHsv(t, s_ ? s_ : 255, v_ ? v_ : 255)
                             : b == 1 ? theme::fromHsv(h_, t, v_ ? v_ : 255)
                                      : theme::fromHsv(h_, s_, t);
                g.drawFastVLine(4 + x, y, 12, col);
            }
            int mx = 4 + vals[b] * (SCREEN_W - 41) / 255;
            g.drawFastVLine(mx, y - 2, 16, b == bar_ ? ui::c().fg : ui::c().border);
            ui::text(SCREEN_W - 34, y + 2, String(vals[b]), b == bar_ ? ui::c().fg : ui::c().dim);
            if (b == bar_) ui::text(4, y + 14, names[b], ui::c().dim);
        }

        // Live preview using the colour in its actual role.
        ui::panel(4, BODY_Y + 74, SCREEN_W - 8, 18, ui::c().surface, 4);
        ui::gfx().fillRoundRect(8, BODY_Y + 77, 12, 12, 2, theme::colorRole(role_));
        ui::text(26, BODY_Y + 79, theme::colorRoleName(role_), ui::c().fg);
        ui::hint("arrows adjust  shift=faster  Enter save  ` undo");
    }

    // ---------- hue picker ----------
    void keyHue(const KeyEvent& k) {
        if (k.left  || k.is('h')) { hue_ = (hue_ - 4 + 256) % 256; theme::setAccentHue(hue_); }
        if (k.right || k.is('l')) { hue_ = (hue_ + 4) % 256;       theme::setAccentHue(hue_); }
        if (k.is('r')) { theme::setAccentHue(256); os::toast("accent back to theme"); mode_ = LIST; }
        if (k.enter)   { os::toast("accent saved", os::Tone::Good); mode_ = LIST; }
        os::invalidate();
    }

    void drawHue() {
        // A live spectrum with the current pick marked, over real UI chrome so
        // you can see what the colour actually does before committing.
        for (int x = 0; x < SCREEN_W; x++) {
            uint8_t h = (uint8_t)(x * 256 / SCREEN_W);
            ui::gfx().drawFastVLine(x, BODY_Y + 4, 22, theme::hueColor(h));
        }
        int mx = hue_ * SCREEN_W / 256;
        ui::gfx().fillTriangle(mx, BODY_Y + 28, mx - 4, BODY_Y + 34, mx + 4, BODY_Y + 34,
                               ui::c().fg);

        ui::panel(8, BODY_Y + 38, SCREEN_W - 16, 30, ui::c().surface, 5);
        ui::outline(8, BODY_Y + 38, SCREEN_W - 16, 30, ui::c().accent, 5);
        ui::text(16, BODY_Y + 44, "Selected row looks like this", ui::c().accent);
        ui::text(16, BODY_Y + 55, String("hue ") + hue_, ui::c().dim);
        ui::progress(8, BODY_Y + 74, SCREEN_W - 16, 8, hue_ / 255.0f, ui::c().accent);
        ui::hint("< > pick   Enter save   R reset   ` back");
    }

    // A live view of exactly what the keyboard reports. Arrows on this device
    // are the ; . , / keycaps, and this is the fastest way to prove it.
    void drawKeyTest() {
        int y = BODY_Y + 2;
        String chars;
        for (char ch : last_.chars) { chars += ch; chars += ' '; }
        ui::text(4, y, "chars: " + (chars.length() ? chars : String("-")), ui::c().fg);

        String arrows;
        if (last_.up) arrows += "UP ";
        if (last_.down) arrows += "DOWN ";
        if (last_.left) arrows += "LEFT ";
        if (last_.right) arrows += "RIGHT ";
        ui::text(4, y + 13, "arrows: " + (arrows.length() ? arrows : String("-")),
                 arrows.length() ? ui::c().good : ui::c().dim);

        String mods;
        if (last_.fn) mods += "fn ";
        if (last_.ctrl) mods += "ctrl ";
        if (last_.shift) mods += "shift ";
        if (last_.opt) mods += "opt ";
        if (last_.alt) mods += "alt ";
        ui::text(4, y + 26, "mods: " + (mods.length() ? mods : String("-")),
                 mods.length() ? ui::c().accent2 : ui::c().dim);

        String special;
        if (last_.enter) special += "enter ";
        if (last_.del) special += "bksp ";
        if (last_.tab) special += "tab ";
        if (last_.space) special += "space ";
        if (last_.esc) special += "esc ";
        ui::text(4, y + 39, "keys: " + (special.length() ? special : String("-")),
                 special.length() ? ui::c().warn : ui::c().dim);

        ui::text(4, y + 58, "Arrows are the ; . , / keycaps.", ui::c().dim);
        ui::text(4, y + 69, "fn makes them arrows only.", ui::c().dim);
        ui::hint("press anything   ` exits");
    }

    // ---------- actions ----------
    void runAction(const String& key) {
        if (key == "a_discover") {
            bool found = false;
            ui::await("Looking for the Mac", [&] { found = cloud::discoverHost(); });
            os::toast(found
                          ? "found " + store::getStr(store::K_HOST, "")
                          : "no daemon advertised",
                      cloud::hostOnline() ? os::Tone::Good : os::Tone::Bad);
        } else if (key == "a_ping") {
            bool ok = false;
            ui::await("Pinging daemon", [&] { ok = cloud::pingHost(3000); });
            os::toast(ok ? "daemon OK  " + cloud::hostFeatures() : "no answer",
                      ok ? os::Tone::Good : os::Tone::Bad);
        } else if (key == "a_ntp") {
            net::syncTime();
            os::toast(net::timeValid() ? "clock " + net::clockString() : "NTP failed",
                      net::timeValid() ? os::Tone::Good : os::Tone::Bad);
        } else if (key == "a_sd") {
            store::sdRelease();
            bool ok = store::sdAcquire(/*force=*/true);
            os::toast(ok ? "SD mounted" : "no card, or not FAT32",
                      ok ? os::Tone::Good : os::Tone::Bad);
        } else if (key == "a_wipewifi") {
            if (ui::confirm("Forget every saved WiFi network?")) {
                for (auto& s : net::savedNetworks()) net::forgetNetwork(s);
                os::toast("all networks forgotten", os::Tone::Good);
            }
        } else if (key == "a_keytest") {
            last_ = KeyEvent();
            mode_ = KEYTEST;
            os::invalidate();
            return;
        } else if (key == "a_colors") {
            colorSel_ = 0;
            mode_ = COLORS;
            os::invalidate();
            return;
        } else if (key == "a_resetcolors") {
            if (ui::confirm("Reset custom colours to the Midnight preset?")) {
                theme::resetCustomToPreset(0);
                theme::setPreset(0);
                os::toast("colours reset", os::Tone::Good);
            }
        } else if (key == "a_i2c") {
            std::vector<hw::I2CDevice> found;
            ui::await("Scanning Grove port", [&] { found = hw::i2cScan(); });
            if (found.empty()) {
                os::toast("nothing on the Grove port", os::Tone::Bad);
            } else {
                std::vector<String> rows;
                for (auto& d : found) {
                    char b[8];
                    snprintf(b, sizeof(b), "0x%02X", d.addr);
                    rows.push_back(String(b) + "  " + d.guess);
                }
                ui::chooser(String((int)found.size()) + " device(s)", rows, 0);
            }
        } else if (key == "a_usb") {
            if (!usbdisk::available()) {
                os::toast("no card was present at boot", os::Tone::Bad);
            } else if (usbdisk::attached()) {
                usbdisk::detach();
                os::toast("card handed back to the device", os::Tone::Good);
            } else {
                os::launchByName("Files");    // the mode lives there, with the counters
                os::toast("press U in Files to hand the card over");
            }
        } else if (key == "a_led") {
            hw::ledPulse(255, 0, 0, 200);
            hw::ledPulse(0, 255, 0, 200);
            hw::ledPulse(0, 0, 255, 200);
            os::toast("LED should have flashed R/G/B");
        } else if (key == "a_reset") {
            if (ui::confirm("Erase all settings, keys and WiFi? Notes on the SD card are kept.")) {
                store::factoryReset();
                os::toast("reset - rebooting", os::Tone::Bad);
                delay(900);
                ESP.restart();
            }
        }
        os::invalidate();
    }

    Mode mode_ = LIST;
    int sel_ = 1, scroll_ = 0, hue_ = 0;
    String buf_;
    KeyEvent last_;
    int colorSel_ = 0, colorScroll_ = 0, role_ = 0, bar_ = 0;
    uint16_t original_ = 0;
    uint8_t h_ = 0, s_ = 0, v_ = 0;
};

App* settingsApp() { static Settings a; return &a; }
