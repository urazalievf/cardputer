// CardputerOS — a small app OS for the M5Stack Cardputer (StampS3).
//
// The kernel owns the display, the keyboard, the radios and storage; apps are
// plain C++ objects that draw into a 240x135 frame and get normalized keys.
#include "kernel/os.h"
#include "apps/apps.h"

void setup() {
    os::begin();

    // apps()[0] is the launcher; 1..8 are the number-key shortcuts, and the
    // launcher grid is 4x2, so eight is the natural cap before it scrolls.
    os::registerApp(launcherApp());
    os::registerApp(notesApp());
    os::registerApp(voiceApp());
    os::registerApp(askApp());
    os::registerApp(codeApp());
    os::registerApp(wifiApp());
    os::registerApp(bluetoothApp());
    os::registerApp(filesApp());
    os::registerApp(settingsApp());

    os::apps()[0]->onEnter();
    os::invalidate();
}

void loop() {
    os::run();
    delay(12);
}
