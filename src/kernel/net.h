// WiFi manager: scan any network, join it, remember it, rejoin on boot.
#pragma once
#include "os.h"
#include <vector>

namespace net {

struct Network {
    String ssid;
    int32_t rssi = 0;
    bool open = false;
    bool known = false;   // we have a saved password
};

void begin();                     // starts STA mode and a background autojoin
void tick();                      // keeps autojoin alive; call from the kernel loop

bool connected();
String ssid();
String ip();
int rssi();
String rssiBars();                // "....", "|...", "||..", "|||." style meter
int    signalBars();              // 0-4, for drawing a meter

// Scanning is async so the UI never freezes.
void startScan();
bool scanRunning();
std::vector<Network> scanResults();

bool connect(const String& ssid, const String& password, uint32_t timeoutMs = 15000);
void disconnect();

// Saved networks (NVS namespace "wifi", one JSON blob).
std::vector<String> savedNetworks();
void  saveNetwork(const String& ssid, const String& password);
void  forgetNetwork(const String& ssid);
bool  isKnown(const String& ssid);
String passwordFor(const String& ssid);

// Try every saved network in signal order. Returns true once joined.
bool autoJoin();

void syncTime();                  // NTP, using the configured TZ
bool timeValid();
String clockString();             // "14:32" or "--:--"

}  // namespace net
