#include "os.h"
#include "ui.h"
#include "theme.h"
#include "net.h"
#include "store.h"
#include "cloud.h"
#include "ai.h"
#include "audio.h"
#include "bt.h"
#include <stdarg.h>
#include <esp_heap_caps.h>

namespace os {

static std::vector<App*> s_apps;
static std::vector<int>  s_stack;      // navigation history, launcher excluded
static int  s_current = 0;
static bool s_dirty = true;
static String s_toast;
static Tone s_toastTone = Tone::Info;
static uint32_t s_toastUntil = 0;

void logf(const char* fmt, ...) {
    char buf[224];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[%7lu] %s\n", (unsigned long)millis(), buf);
}

void bootReport() {
    logf("CardputerOS %s", CARDPUTER_OS_VERSION);
    logf("chip     %s rev%d, %d cores @ %dMHz",
         ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), getCpuFrequencyMhz());
    logf("heap     %u KB free (%u KB internal, %u KB psram)",
         (unsigned)(ESP.getFreeHeap() / 1024),
         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    logf("canvas   %s", ui::canvasActive() ? "on (64KB sprite)" : "off - direct draw");
    logf("mic      %s, buffer %us (allocated on demand)",
         audio::micReady() ? "ready" : "UNAVAILABLE", (unsigned)audio::capacitySeconds());
    if (store::sdReady())
        logf("sd       mounted, %llu/%llu MB used, %d notes",
             store::sdUsedMB(), store::sdTotalMB(), (int)store::listNotes().size());
    else
        logf("sd       NOT MOUNTED - no card, or not FAT32 (exFAT/NTFS won't mount)");
    logf("wifi     %d saved network(s)", (int)net::savedNetworks().size());
    logf("theme    %s, accent hue %d", theme::presetName(theme::preset()), theme::accentHue());
    logf("ai       default %s, %d provider(s) configured",
         ai::spec(ai::preferred()).label, (int)ai::available().size());
    logf("ready");
}

void begin() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);

    store::begin();
    theme::begin();
    M5Cardputer.Display.setBrightness(theme::brightness());
    ui::begin();
    ui::splash("CardputerOS", "starting");

    audio::begin();
    net::begin();
    cloud::begin();
    ai::begin();

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 900) delay(10);
    bootReport();
    s_dirty = true;
}

void registerApp(App* app) { s_apps.push_back(app); }
const std::vector<App*>& apps() { return s_apps; }
App* current() { return s_apps.empty() ? nullptr : s_apps[s_current]; }
int currentIndex() { return s_current; }
bool canGoBack() { return !s_stack.empty() || s_current != 0; }

static void switchTo(int index) {
    if (index < 0 || index >= (int)s_apps.size() || index == s_current) return;
    s_apps[s_current]->onExit();
    s_current = index;
    logf("app -> %s", s_apps[s_current]->name());
    s_apps[s_current]->onEnter();
    s_dirty = true;
}

void launch(int index) {
    if (index == s_current) return;
    if (index > 0) s_stack.push_back(s_current);
    else s_stack.clear();
    if (s_stack.size() > 8) s_stack.erase(s_stack.begin());
    switchTo(index);
}

void launchByName(const char* name) {
    for (size_t i = 0; i < s_apps.size(); i++)
        if (strcmp(s_apps[i]->name(), name) == 0) { launch(i); return; }
}

void home() { s_stack.clear(); switchTo(0); }

void back() {
    App* app = current();
    if (app && app->onBack()) { s_dirty = true; return; }   // handled internally
    if (!s_stack.empty()) {
        int prev = s_stack.back();
        s_stack.pop_back();
        switchTo(prev);
        return;
    }
    home();
}

void invalidate() { s_dirty = true; }
bool consumeDirty() { bool d = s_dirty; s_dirty = false; return d; }

void toast(const String& msg, Tone tone) {
    s_toast = msg;
    s_toastTone = tone;
    s_toastUntil = millis() + 2600;
    s_dirty = true;
}
const String& toastText() { return s_toast; }
Tone toastTone() { return s_toastTone; }
bool toastActive() { return millis() < s_toastUntil; }

KeyEvent readKey() {
    KeyEvent k;
    auto ks = M5Cardputer.Keyboard.keysState();
    k.enter = ks.enter; k.del = ks.del; k.tab = ks.tab; k.space = ks.space;
    k.fn = ks.fn; k.ctrl = ks.ctrl; k.shift = ks.shift; k.opt = ks.opt; k.alt = ks.alt;
    // The Cardputer prints arrow glyphs on ; . , / and every other firmware
    // treats them as arrows in menus, so they set the arrow flags directly.
    // Without fn the character is ALSO kept, so text entry still types them:
    // navigation screens read the flags, text screens read chars first.
    for (char ch : ks.word) {
        bool arrow = true;
        switch (ch) {
            case ';': k.up = true;    break;
            case '.': k.down = true;  break;
            case ',': k.left = true;  break;
            case '/': k.right = true; break;
            default:  arrow = false;  break;
        }
        if (arrow && k.fn) continue;          // fn makes it unambiguously an arrow
        if (ch == '`') { k.esc = true; continue; }
        k.chars.push_back(ch);
    }
    return k;
}

// Shortcuts that work from anywhere. Kept few and mnemonic; anything an app
// might legitimately want is left alone.
// With ctrl held the keyboard reports the shifted glyph, so ctrl+1 arrives as
// '!'. Map the top row back to digits.
static char unshiftDigit(char ch) {
    static const char* shifted = ")!@#$%^&*(";
    for (int i = 0; i < 10; i++) if (shifted[i] == ch) return '0' + i;
    return ch;
}

static bool handleGlobal(const KeyEvent& k) {
    if (!k.ctrl) return false;
    if (k.is('h')) { home(); return true; }
    if (k.is('t')) {
        theme::setPreset(theme::preset() + 1);
        toast(String("theme: ") + theme::presetName(theme::preset()), Tone::Good);
        return true;
    }
    for (char raw : k.chars) {
        char ch = unshiftDigit(raw);
        if (ch >= '1' && ch <= '9') {
            int nth = ch - '0';
            // Number keys address visible apps, matching the launcher grid.
            int seen = 0;
            for (size_t i = 1; i < s_apps.size(); i++) {
                if (s_apps[i]->hidden()) continue;
                if (++seen == nth) { launch(i); return true; }
            }
            return true;
        }
    }
    return false;
}

void run() {
    M5Cardputer.update();
    net::tick();
    bt::tick();

    static bool reported = false;
    if (Serial && !reported)      { reported = true;  bootReport(); }
    else if (!Serial && reported) { reported = false; }

    static bool hadToast = false;
    if (hadToast && !toastActive()) { hadToast = false; s_dirty = true; }
    if (toastActive()) hadToast = true;

    App* app = current();
    if (!app) return;

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        KeyEvent k = readKey();
        if (!handleGlobal(k)) {
            if (k.esc) back();
            else app->onKey(k);
        }
    }

    app->tick();

    if (consumeDirty()) {
        ui::beginFrame();
        app->draw();
        ui::statusBar(app->title(), app->icon(), app->accent());
        ui::endFrame();
    }
}

}  // namespace os
