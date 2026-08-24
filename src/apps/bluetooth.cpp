#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/bt.h"
#include "../kernel/store.h"

// BLE. Two useful roles on a device like this: be a keyboard for something
// bigger, or look at what's nearby. The stack is started on demand and torn
// down on exit, because Bluedroid costs ~60KB and this board has no PSRAM.
class Bluetooth : public App {
    enum Mode : uint8_t { MENU, SCAN, KEYBOARD };
public:
    const char* name() const override { return "Bluetooth"; }
    const char* blurb() const override { return "ble"; }
    ui::Icon icon() const override { return ui::Icon::Bluetooth; }
    bool hidden() const override { return true; }   // reached via Settings
    uint16_t accent() const override { return ui::c().accent2; }

    String title() const override {
        switch (mode_) {
            case SCAN:     return bt::scanning() ? "Scanning..."
                                                 : "Nearby (" + String((int)devs_.size()) + ")";
            case KEYBOARD: return bt::connected() ? "Keyboard - connected"
                                                  : "Keyboard - pairing";
            default:       return "Bluetooth";
        }
    }

    bool onBack() override {
        if (mode_ != MENU) { bt::end(); mode_ = MENU; return true; }
        return false;
    }

    void onEnter() override { mode_ = MENU; sel_ = 0; os::invalidate(); }
    void onExit()  override { bt::end(); }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case MENU:     return keyMenu(k);
            case SCAN:     return keyScan(k);
            case KEYBOARD: return keyKeyboard(k);
        }
    }

    void tick() override {
        if (mode_ == SCAN) {
            bool now = bt::scanning();
            if (now != wasScanning_) {
                wasScanning_ = now;
                if (!now) devs_ = bt::results();
                os::invalidate();
            }
            if (now && millis() - spin_ > 120) { spin_ = millis(); os::invalidate(); }
        } else if (mode_ == KEYBOARD) {
            bool now = bt::connected();
            if (now != wasConnected_) { wasConnected_ = now; os::invalidate(); }
        }
    }

    void draw() override {
        switch (mode_) {
            case MENU:     return drawMenu();
            case SCAN:     return drawScan();
            case KEYBOARD: return drawKeyboard();
        }
    }

private:
    void keyMenu(const KeyEvent& k) {
        if (k.up   || k.is('k')) { sel_ = (sel_ + 1) % 2; os::invalidate(); return; }
        if (k.down || k.is('j')) { sel_ = (sel_ + 1) % 2; os::invalidate(); return; }
        if (!(k.enter || k.space)) return;
        if (sel_ == 0) {
            ui::busy("Starting BLE radio");
            bt::begin(bt::Mode::Scanning);
            bt::startScan(5);
            wasScanning_ = true;
            mode_ = SCAN;
        } else {
            ui::busy("Advertising as a keyboard");
            bt::begin(bt::Mode::Keyboard);
            wasConnected_ = false;
            typed_ = "";
            mode_ = KEYBOARD;
        }
        os::invalidate();
    }

    void drawMenu() {
        std::vector<ui::Row> rows = {
            {"Scan nearby devices", "", ui::Icon::Wifi, 0},
            {"Be a keyboard",       "", ui::Icon::Chat, 0},
        };
        ui::listView(rows, sel_, 0, 2, BODY_Y + 6);

        ui::text(4, BODY_Y + 34, "Keyboard mode types what you press", ui::c().dim);
        ui::text(4, BODY_Y + 45, "into a paired Mac, iPad or phone.", ui::c().dim);
        ui::text(4, BODY_Y + 60, String("Advertises as: ") + bt::deviceName(), ui::c().accent2);
        ui::text(4, BODY_Y + 71, "ESP32-S3 is BLE only - no audio/SPP.", ui::c().dim);
        ui::hint("Enter start   ` back   (name in Settings)");
    }

    void keyScan(const KeyEvent& k) {
        if (bt::scanning()) return;
        if (k.is('r')) { bt::startScan(5); wasScanning_ = true; os::invalidate(); return; }
        if (devs_.empty()) return;
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)devs_.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter) {
            const auto& d = devs_[sel_];
            os::toast(d.address + "  " + String(d.rssi) + "dBm");
        }
    }

    void drawScan() {
        if (bt::scanning()) {
            ui::spinner(SCREEN_W / 2, 54, ui::c().accent2);
            ui::centered(76, "Listening for advertisements", ui::c().dim);
            ui::hint("");
            return;
        }
        if (devs_.empty()) {
            ui::centered(52, "Nothing nearby", ui::c().dim);
            ui::centered(66, "press R to scan again", ui::c().accent2);
            ui::hint("R rescan   ` back");
            return;
        }
        std::vector<ui::Row> rows;
        for (auto& d : devs_) {
            ui::Row r;
            r.label = d.name;
            r.detail = String(d.rssi);
            r.icon = ui::Icon::Bluetooth;
            r.tint = d.rssi > -60 ? ui::c().good : d.rssi > -80 ? ui::c().fg : ui::c().dim;
            rows.push_back(r);
        }
        if (sel_ >= (int)rows.size()) sel_ = rows.size() - 1;
        scroll_ = ui::listView(rows, sel_, scroll_);
        ui::hint("Enter address   R rescan   ` back");
    }

    void keyKeyboard(const KeyEvent& k) {
        if (!bt::connected()) return;
        if (k.enter)  { bt::sendEnter();     append('\n'); return; }
        if (k.del)    { bt::sendBackspace(); if (typed_.length()) typed_.remove(typed_.length()-1);
                        os::invalidate(); return; }
        if (k.space)  { bt::sendChar(' ');   append(' '); return; }
        // Bare ; . , / are literal characters here; fn makes them arrows.
        if (k.chars.empty()) {
            if (k.up)    { bt::sendArrow(0); return; }
            if (k.down)  { bt::sendArrow(1); return; }
            if (k.left)  { bt::sendArrow(2); return; }
            if (k.right) { bt::sendArrow(3); return; }
        }
        for (char ch : k.chars) { bt::sendChar(ch); append(ch); }
    }

    void append(char ch) {
        typed_ += ch;
        if (typed_.length() > 200) typed_.remove(0, 100);
        os::invalidate();
    }

    void drawKeyboard() {
        if (!bt::connected()) {
            ui::spinner(SCREEN_W / 2, 46, ui::c().accent2);
            ui::centered(68, "Pair from the other device", ui::c().fg);
            ui::centered(80, bt::deviceName(), ui::c().accent2);
            ui::centered(94, "Bluetooth settings > pair", ui::c().dim);
            ui::hint("` stop advertising");
            return;
        }
        ui::panel(3, BODY_Y + 2, SCREEN_W - 6, 74, ui::c().surface, 4);
        auto lines = ui::wrap(typed_.length() ? typed_ : String("(start typing)"));
        int start = (int)lines.size() > 7 ? lines.size() - 7 : 0;
        for (int i = 0; start + i < (int)lines.size() && i < 7; i++)
            ui::text(7, BODY_Y + 6 + i * 10, lines[start + i],
                     typed_.length() ? ui::c().fg : ui::c().dim);
        ui::hint("typing goes to the paired device   ` stop");
    }

    Mode mode_ = MENU;
    int sel_ = 0, scroll_ = 0;
    bool wasScanning_ = false, wasConnected_ = false;
    uint32_t spin_ = 0;
    String typed_;
    std::vector<bt::Device> devs_;
};

App* bluetoothApp() { static Bluetooth a; return &a; }
