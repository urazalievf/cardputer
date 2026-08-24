// CardputerOS — a small app OS for the M5Stack Cardputer (StampS3).
//
// The kernel owns the display, the keyboard and the network; apps are plain
// C++ objects that draw into the 240x135 frame and get normalized key events.
#include "kernel/os.h"
#include "apps/apps.h"

void setup() {
    os::begin();

    // Order matters: apps()[0] is the launcher, and 1..9 map to number keys.
    os::registerApp(launcherApp());
    os::registerApp(notesApp());
    os::registerApp(voiceApp());
    os::registerApp(askApp());
    os::registerApp(codeApp());
    os::registerApp(wifiApp());
    os::registerApp(filesApp());
    os::registerApp(settingsApp());

    os::apps()[0]->onEnter();
    os::invalidate();
}

void loop() {
    os::run();
    delay(15);
}
