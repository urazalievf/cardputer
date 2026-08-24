// CardputerOS kernel — app model, event loop, shared services.
#pragma once
#include <M5Cardputer.h>
#include <Arduino.h>
#include <vector>

#ifndef CARDPUTER_OS_VERSION
#define CARDPUTER_OS_VERSION "0.1.0-dev"
#endif

// Screen is 240x135 in landscape (rotation 1).
static const int SCREEN_W = 240;
static const int SCREEN_H = 135;
static const int STATUS_H = 11;   // top status bar
static const int HINT_Y   = 125;  // bottom hint row
static const int BODY_Y   = STATUS_H + 3;
static const int ROW_H    = 10;
static const int BODY_ROWS = 10;
static const int CHARS_PER_LINE = 38;

// A normalized key press. The Cardputer keyboard reports raw chars plus
// modifier flags; arrows live under fn + ; . , /
struct KeyEvent {
    std::vector<char> chars;
    bool enter = false, del = false, tab = false, space = false;
    bool esc = false;                      // the ` key, labelled esc
    bool up = false, down = false, left = false, right = false;
    bool fn = false, ctrl = false, shift = false, opt = false, alt = false;

    // First printable char, or 0.
    char ch() const { return chars.empty() ? 0 : chars[0]; }
    bool is(char c) const {
        for (char x : chars) if (x == c || x == (char)tolower(c)) return true;
        return false;
    }
};

class App {
public:
    virtual ~App() {}
    virtual const char* name() const = 0;
    virtual const char* blurb() const { return ""; }
    // Status-bar title; defaults to name(), override for live detail.
    virtual String title() const { return String(name()); }
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onKey(const KeyEvent&) {}
    virtual void tick() {}          // every loop, ~50Hz
    virtual void draw() = 0;
    // Return false to keep the kernel's global esc-to-launcher behaviour off
    // (apps that need ` as a literal character, e.g. text fields).
    virtual bool escExits() const { return true; }
};

namespace os {

void begin();
void registerApp(App* app);
void run();                       // one iteration of the event loop

void launch(int index);
void launchByName(const char* name);
void home();                      // back to launcher
App* current();
const std::vector<App*>& apps();
int currentIndex();

void invalidate();                // request a redraw
bool consumeDirty();

// Transient one-line message shown in the status bar for a few seconds.
void toast(const String& msg);
const String& toastText();
bool toastActive();

}  // namespace os
