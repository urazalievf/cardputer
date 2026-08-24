// CardputerOS — a small app OS for the M5Stack Cardputer (StampS3).
//
// The kernel owns the display, the keyboard, the radios and storage; apps are
// plain C++ objects that draw into a 240x135 frame and get normalized keys.
#include "kernel/os.h"
#include "kernel/hw.h"
#include "apps/apps.h"
#ifdef SELFTEST
#include "kernel/selftest.h"
#endif

void setup() {
    os::begin();
    hw::ledBegin();

    // apps()[0] is the launcher. Number keys address the *visible* ones, so
    // WiFi and Bluetooth (hidden, reached from Settings) don't consume a slot.
    os::registerApp(launcherApp());
    os::registerApp(notesApp());
    os::registerApp(voiceApp());
    os::registerApp(askApp());
    os::registerApp(codeApp());
    os::registerApp(translateApp());
    os::registerApp(tasksApp());
    os::registerApp(calcApp());
    os::registerApp(timerApp());
    os::registerApp(weatherApp());
    os::registerApp(remoteApp());
    os::registerApp(shareApp());
    os::registerApp(wifiApp());
    os::registerApp(bluetoothApp());
    os::registerApp(filesApp());
    os::registerApp(settingsApp());

#ifdef SELFTEST
    // Wait for a monitor to attach so the results are not written into the void.
    for (uint32_t t = millis(); !Serial && millis() - t < 4000;) delay(50);
    selftest::run();
#endif

    os::apps()[0]->onEnter();
    os::invalidate();
}

void loop() {
    os::run();
    delay(12);
}
