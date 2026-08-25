// CardputerOS kernel — app model, navigation, event loop, shared services.
#pragma once
#include <M5Cardputer.h>
#include <Arduino.h>
#include <vector>

#ifndef CARDPUTER_OS_VERSION
#define CARDPUTER_OS_VERSION "0.2.0-dev"
#endif

static const int SCREEN_W = 240;
static const int SCREEN_H = 135;
static const int STATUS_H = 11;
static const int HINT_Y   = 126;
static const int BODY_Y   = 16;

// Declared here so App can name its icon without depending on the whole UI.
namespace ui {
enum class Icon : uint8_t {
    None, Home, Note, Mic, Chat, Code, Wifi, Folder, Gear, Bluetooth,
    Check, Cross, Arrow, Lock, Cloud, Chip, Clock, Battery,
};
}

// A normalized key press. Arrows live under fn + ; . , / — the glyphs printed
// on those keycaps.
struct KeyEvent {
    std::vector<char> chars;
    bool enter = false, del = false, tab = false, space = false;
    bool esc = false;
    bool up = false, down = false, left = false, right = false;
    bool fn = false, ctrl = false, shift = false, opt = false, alt = false;

    char ch() const { return chars.empty() ? 0 : chars[0]; }
    bool is(char x) const {
        for (char v : chars) if (v == x || tolower(v) == tolower(x)) return true;
        return false;
    }
    bool any() const {
        return !chars.empty() || enter || del || tab || space || esc ||
               up || down || left || right;
    }
};

class App {
public:
    virtual ~App() {}
    virtual const char* name() const = 0;
    virtual const char* blurb() const { return ""; }
    virtual ui::Icon icon() const { return ui::Icon::None; }
    virtual uint16_t accent() const { return 0; }      // 0 = theme accent
    virtual String title() const { return String(name()); }
    // Hidden apps stay reachable by name but keep off the launcher grid —
    // connectivity lives under Settings rather than as its own tile.
    virtual bool hidden() const { return false; }

    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onKey(const KeyEvent&) {}
    virtual void tick() {}
    virtual void draw() = 0;

    // Esc / back. Return true if the app consumed it (closed a sub-view,
    // cleared a field); return false to let the kernel pop the nav stack.
    virtual bool onBack() { return false; }
};

namespace os {

enum class Tone : uint8_t { Info, Good, Bad };

void begin();
void registerApp(App* app);
void run();

void launch(int index);
void launchByName(const char* name);
void back();                      // app first, then the nav stack, then home
void home();
App* current();
const std::vector<App*>& apps();
int  currentIndex();
bool canGoBack();

void invalidate();
bool consumeDirty();

void toast(const String& msg, Tone tone = Tone::Info);
const String& toastText();
Tone toastTone();
bool toastActive();

// Exposed so blocking modals can read the keyboard with the same mapping.
KeyEvent readKey();

void logf(const char* fmt, ...);
// The USB serial port, so the command console can read and write it without
// caring which CDC implementation this build uses.
Stream& consoleStream();
void bootReport();

}  // namespace os
