// Bluetooth Low Energy. The ESP32-S3 has no Classic BT, so there is no A2DP or
// SPP here — but BLE covers the two things worth having on a pocket keyboard:
// acting as a keyboard for another machine, and seeing what's around you.
//
// The stack is only initialised while the Bluetooth app is open. Bluedroid
// costs ~60KB of heap, which is most of the headroom on a board with no PSRAM.
#pragma once
#include "os.h"
#include <vector>

namespace bt {

enum class Mode : uint8_t { Off, Scanning, Keyboard };

struct Device {
    String name;
    String address;
    int rssi = 0;
};

void begin(Mode m);          // starts the stack in the requested role
void end();                  // tears it down and gives the heap back
void tick();
bool active();
Mode mode();
String status();

// --- scanning ---
void startScan(uint32_t seconds = 5);
bool scanning();
std::vector<Device> results();

// --- HID keyboard: this device types into a paired Mac, iPad or phone ---
bool connected();
String peerName();
void sendChar(char c);
void sendText(const String& s);
void sendEnter();
void sendBackspace();
void sendArrow(uint8_t which);   // 0 up, 1 down, 2 left, 3 right
void setDeviceName(const String& n);
String deviceName();

}  // namespace bt
