#include "os.h"
#include "ui.h"
#include "net.h"
#include "store.h"
#include "cloud.h"
#include "audio.h"

namespace os {

static std::vector<App*> s_apps;
static int  s_current = 0;
static bool s_dirty = true;
static String s_toast;
static uint32_t s_toastUntil = 0;

void begin() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);          // keyboard + display + speaker
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);

    store::begin();
    int bright = store::getInt(store::K_BRIGHT, 120);
    M5Cardputer.Display.setBrightness(bright);
    M5Cardputer.Display.fillScreen(ui::BG);

    ui::splash("CardputerOS " CARDPUTER_OS_VERSION, "booting...");
    audio::begin();
    net::begin();
    cloud::begin();
    s_dirty = true;
}

void registerApp(App* app) { s_apps.push_back(app); }
const std::vector<App*>& apps() { return s_apps; }
App* current() { return s_apps.empty() ? nullptr : s_apps[s_current]; }
int currentIndex() { return s_current; }

void launch(int index) {
    if (index < 0 || index >= (int)s_apps.size() || index == s_current) return;
    if (s_apps[s_current]) s_apps[s_current]->onExit();
    s_current = index;
    s_apps[s_current]->onEnter();
    s_dirty = true;
}

void launchByName(const char* name) {
    for (size_t i = 0; i < s_apps.size(); i++)
        if (strcmp(s_apps[i]->name(), name) == 0) { launch(i); return; }
}

void home() { launch(0); }

void invalidate() { s_dirty = true; }
bool consumeDirty() { bool d = s_dirty; s_dirty = false; return d; }

void toast(const String& msg) {
    s_toast = msg;
    s_toastUntil = millis() + 2500;
    s_dirty = true;
}
const String& toastText() { return s_toast; }
bool toastActive() { return millis() < s_toastUntil; }

// Translate the raw Cardputer keyboard state into a KeyEvent.
// Arrows live under fn + ; . , / — the glyphs printed on those keycaps.
static KeyEvent translate(const Keyboard_Class::KeysState& ks) {
    KeyEvent k;
    k.enter = ks.enter; k.del = ks.del; k.tab = ks.tab; k.space = ks.space;
    k.fn = ks.fn; k.ctrl = ks.ctrl; k.shift = ks.shift; k.opt = ks.opt; k.alt = ks.alt;

    for (char c : ks.word) {
        if (k.fn) {
            switch (c) {
                case ';': k.up = true;    continue;
                case '.': k.down = true;  continue;
                case ',': k.left = true;  continue;
                case '/': k.right = true; continue;
                default: break;
            }
        }
        if (c == '`') { k.esc = true; continue; }
        k.chars.push_back(c);
    }
    return k;
}

void run() {
    M5Cardputer.update();
    net::tick();

    static uint32_t lastToast = 0;
    if (lastToast && !toastActive()) { lastToast = 0; s_dirty = true; }
    if (toastActive()) lastToast = 1;

    App* app = current();
    if (!app) return;

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        KeyEvent k = translate(M5Cardputer.Keyboard.keysState());
        if (k.esc && app->escExits()) home();
        else app->onKey(k);
    }

    app->tick();

    if (consumeDirty()) {
        ui::clear();
        app->draw();
        ui::statusBar(app->title().c_str());
    }
}

}  // namespace os
