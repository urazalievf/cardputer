#define US_KEYBOARD 1
#include "bt.h"
#include "store.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>
#include <HIDKeyboardTypes.h>

namespace bt {

static Mode s_mode = Mode::Off;
static bool s_connected = false;
static String s_peer = "";
static std::vector<Device> s_results;
static bool s_scanning = false;
static uint32_t s_scanUntil = 0;

static BLEServer*    s_server = nullptr;
static BLEHIDDevice* s_hid = nullptr;
static BLECharacteristic* s_input = nullptr;
static BLEScan* s_scan = nullptr;

// Standard 8-byte boot keyboard: modifiers, reserved, six concurrent keycodes.
static const uint8_t REPORT_MAP[] = {
    USAGE_PAGE(1), 0x01,        // Generic Desktop
    USAGE(1),      0x06,        // Keyboard
    COLLECTION(1), 0x01,        // Application
    REPORT_ID(1),  0x01,
    USAGE_PAGE(1), 0x07,        // Key codes
    USAGE_MINIMUM(1), 0xE0,
    USAGE_MAXIMUM(1), 0xE7,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1), 0x01,
    REPORT_COUNT(1), 0x08,
    HIDINPUT(1),   0x02,        // modifier byte
    REPORT_COUNT(1), 0x01,
    REPORT_SIZE(1), 0x08,
    HIDINPUT(1),   0x01,        // reserved
    REPORT_COUNT(1), 0x06,
    REPORT_SIZE(1), 0x08,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x65,
    USAGE_PAGE(1), 0x07,
    USAGE_MINIMUM(1), 0x00,
    USAGE_MAXIMUM(1), 0x65,
    HIDINPUT(1),   0x00,        // six keycodes
    END_COLLECTION(0),
};

class ServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        s_connected = true;
        s_peer = "paired host";
        os::logf("bt: host connected");
        os::invalidate();
    }
    void onDisconnect(BLEServer* srv) override {
        s_connected = false;
        s_peer = "";
        os::logf("bt: host disconnected, advertising again");
        srv->getAdvertising()->start();
        os::invalidate();
    }
};
static ServerCB s_serverCB;

String deviceName() { return store::getStr("btname", "CardputerOS"); }
void setDeviceName(const String& n) { store::setStr("btname", n); }

static void startKeyboardRole() {
    s_server = BLEDevice::createServer();
    s_server->setCallbacks(&s_serverCB);

    s_hid = new BLEHIDDevice(s_server);
    s_input = s_hid->inputReport(1);
    s_hid->manufacturer(String("M5Stack"));
    s_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
    s_hid->hidInfo(0x00, 0x01);
    s_hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    s_hid->startServices();
    s_hid->setBatteryLevel(constrain(M5Cardputer.Power.getBatteryLevel(), 0, 100));

    BLEAdvertising* adv = s_server->getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(s_hid->hidService()->getUUID());
    adv->setScanResponse(true);
    adv->start();
    os::logf("bt: advertising as \"%s\" (HID keyboard)", deviceName().c_str());
}

void begin(Mode m) {
    if (s_mode == m) return;
    if (s_mode != Mode::Off) end();

    BLEDevice::init(deviceName());
    // Lowest usable TX power: BLE and WiFi share one radio, and range past a
    // desk isn't the point here.
    BLEDevice::setPower(ESP_PWR_LVL_P3);

    if (m == Mode::Keyboard) {
        startKeyboardRole();
    } else if (m == Mode::Scanning) {
        s_scan = BLEDevice::getScan();
        s_scan->setActiveScan(true);
        s_scan->setInterval(100);
        s_scan->setWindow(80);
    }
    s_mode = m;
    os::logf("bt: stack up in %s mode, %u KB heap free",
             m == Mode::Keyboard ? "keyboard" : "scan",
             (unsigned)(ESP.getFreeHeap() / 1024));
}

void end() {
    if (s_mode == Mode::Off) return;
    if (s_scan) { s_scan->stop(); s_scan->clearResults(); s_scan = nullptr; }
    s_hid = nullptr;          // owned by the server, freed with the stack
    s_input = nullptr;
    s_server = nullptr;
    BLEDevice::deinit(true);
    s_mode = Mode::Off;
    s_connected = false;
    s_scanning = false;
    s_results.clear();
    os::logf("bt: stack down, %u KB heap free", (unsigned)(ESP.getFreeHeap() / 1024));
}

bool active() { return s_mode != Mode::Off; }
Mode mode() { return s_mode; }
bool connected() { return s_connected; }
String peerName() { return s_peer; }

String status() {
    switch (s_mode) {
        case Mode::Off:      return "off";
        case Mode::Scanning: return s_scanning ? "scanning" : (String(s_results.size()) + " found");
        case Mode::Keyboard: return s_connected ? "connected" : "advertising";
    }
    return "?";
}

// --- scanning ---
void startScan(uint32_t seconds) {
    if (s_mode != Mode::Scanning || !s_scan) return;
    s_results.clear();
    s_scanning = true;
    s_scanUntil = millis() + seconds * 1000;
    s_scan->start(seconds, /*is_continue=*/false);
}

bool scanning() { return s_scanning; }
std::vector<Device> results() { return s_results; }

void tick() {
    if (s_mode == Mode::Scanning && s_scanning && millis() > s_scanUntil) {
        s_scanning = false;
        BLEScanResults* res = s_scan->getResults();
        s_results.clear();
        if (res) {
            for (int i = 0; i < res->getCount(); i++) {
                BLEAdvertisedDevice d = res->getDevice(i);
                Device out;
                out.name = d.haveName() ? d.getName() : String("");
                out.address = d.getAddress().toString();
                out.rssi = d.getRSSI();
                if (!out.name.length()) out.name = "(unnamed)";
                s_results.push_back(out);
            }
        }
        std::sort(s_results.begin(), s_results.end(),
                  [](const Device& a, const Device& b) { return a.rssi > b.rssi; });
        s_scan->clearResults();
        os::logf("bt: scan finished, %d device(s)", (int)s_results.size());
        os::invalidate();
    }
}

// --- keyboard ---
static void sendReport(uint8_t modifier, uint8_t keycode) {
    if (!s_input || !s_connected) return;
    uint8_t msg[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
    s_input->setValue(msg, sizeof(msg));
    s_input->notify();
    delay(5);
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    s_input->setValue(release, sizeof(release));
    s_input->notify();
    delay(5);
}

void sendChar(char ch) {
    if ((uint8_t)ch >= KEYMAP_SIZE) return;
    KEYMAP k = keymap[(uint8_t)ch];
    if (!k.usage) return;
    sendReport(k.modifier, k.usage);
}

void sendText(const String& s) {
    for (size_t i = 0; i < s.length(); i++) sendChar(s[i]);
}

void sendEnter()     { sendReport(0, 0x28); }
void sendBackspace() { sendReport(0, 0x2A); }
void sendArrow(uint8_t which) {
    static const uint8_t codes[4] = {0x52, 0x51, 0x50, 0x4F};   // up down left right
    sendReport(0, codes[which & 3]);
}

}  // namespace bt
