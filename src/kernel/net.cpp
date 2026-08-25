#include "net.h"
#include "store.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <time.h>
#include <HTTPClient.h>

namespace net {

static std::vector<Network> s_results;
static bool s_scanning = false;
static uint32_t s_lastAutoJoin = 0;
static bool s_timeSynced = false;
static bool s_located = false;

// ---------------- saved networks ----------------
static JsonDocument loadNets() {
    JsonDocument doc;
    String json = store::getStr("wifinets", "[]");
    if (deserializeJson(doc, json)) doc.to<JsonArray>();
    if (!doc.is<JsonArray>()) doc.to<JsonArray>();
    return doc;
}

static void persistNets(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    store::setStr("wifinets", json);
}

std::vector<String> savedNetworks() {
    std::vector<String> out;
    JsonDocument doc = loadNets();
    for (JsonObject o : doc.as<JsonArray>()) out.push_back(o["s"].as<String>());
    return out;
}

bool isKnown(const String& s) {
    JsonDocument doc = loadNets();
    for (JsonObject o : doc.as<JsonArray>()) if (o["s"] == s) return true;
    return false;
}

String passwordFor(const String& s) {
    JsonDocument doc = loadNets();
    for (JsonObject o : doc.as<JsonArray>()) if (o["s"] == s) return o["p"].as<String>();
    return "";
}

void saveNetwork(const String& s, const String& p) {
    JsonDocument doc = loadNets();
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        if (o["s"] == s) { o["p"] = p; persistNets(doc); return; }
    }
    // Cap the list so NVS never fills up; drop the oldest entry.
    while (arr.size() >= 16) arr.remove(0);
    JsonObject o = arr.add<JsonObject>();
    o["s"] = s;
    o["p"] = p;
    persistNets(doc);
}

void forgetNetwork(const String& s) {
    JsonDocument doc = loadNets();
    JsonArray arr = doc.as<JsonArray>();
    for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i]["s"] == s) { arr.remove(i); break; }
    }
    persistNets(doc);
}

// ---------------- status ----------------
bool connected() { return WiFi.status() == WL_CONNECTED; }
String ssid()    { return connected() ? WiFi.SSID() : String(""); }
String ip()      { return connected() ? WiFi.localIP().toString() : String("0.0.0.0"); }
int rssi()       { return connected() ? WiFi.RSSI() : -127; }

int signalBars() {
    int r = rssi();
    return r > -55 ? 4 : r > -67 ? 3 : r > -78 ? 2 : r > -90 ? 1 : 0;
}

String rssiBars() {
    int bars = signalBars();
    String s;
    for (int i = 0; i < 4; i++) s += (i < bars) ? '|' : '.';
    return s;
}

// ---------------- scanning ----------------
void startScan() {
    if (s_scanning) return;
    s_scanning = true;
    s_results.clear();
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, true /* show hidden */);
}

bool scanRunning() {
    if (!s_scanning) return false;
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return true;
    s_scanning = false;
    if (n < 0) return false;
    for (int i = 0; i < n; i++) {
        Network net;
        net.ssid = WiFi.SSID(i);
        net.rssi = WiFi.RSSI(i);
        net.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        if (!net.ssid.length()) net.ssid = "(hidden)";
        net.known = isKnown(net.ssid);
        s_results.push_back(net);
    }
    WiFi.scanDelete();
    std::sort(s_results.begin(), s_results.end(),
              [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
    return false;
}

std::vector<Network> scanResults() { return s_results; }

// ---------------- connecting ----------------
bool connect(const String& s, const String& p, uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    if (p.length()) WiFi.begin(s.c_str(), p.c_str());
    else            WiFi.begin(s.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(150);
    if (connected() && !s_timeSynced) syncTime();
    return connected();
}

void disconnect() { WiFi.disconnect(true); }

bool autoJoin() {
    if (connected()) return true;
    auto saved = savedNetworks();
    if (saved.empty()) return false;

    // Prefer whatever is actually in range and strongest.
    WiFi.mode(WIFI_STA);
    int16_t n = WiFi.scanNetworks(false, true);
    struct Cand { String ssid; int32_t rssi; };
    std::vector<Cand> cands;
    for (int i = 0; i < n; i++) {
        String found = WiFi.SSID(i);
        if (isKnown(found)) cands.push_back({found, WiFi.RSSI(i)});
    }
    WiFi.scanDelete();
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.rssi > b.rssi; });

    for (auto& c : cands)
        if (connect(c.ssid, passwordFor(c.ssid), 12000)) return true;
    return false;
}

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);   // meaningful battery win on the Cardputer
    auto saved = savedNetworks();
    if (!saved.empty()) {
        // Non-blocking first attempt at the most recently saved network;
        // tick() escalates to a full autoJoin() if that fails.
        String s = saved.back();
        WiFi.begin(s.c_str(), passwordFor(s).c_str());
    }
    s_lastAutoJoin = millis();
}

void tick() {
    if (connected()) {
        if (!s_located && store::getInt("geoauto", 1)) {
            s_located = true;             // one attempt per boot, success or not
            autoLocate();
        }
        if (!s_timeSynced) syncTime();
        return;
    }
    // Retry at most every 30s so we don't thrash the radio.
    if (millis() - s_lastAutoJoin < 30000) return;
    s_lastAutoJoin = millis();
    autoJoin();
}

// ---------------- time ----------------
bool located() { return s_located || store::getStr("wxname", "").length() > 0; }
String placeName() { return store::getStr("wxname", ""); }

// ip-api gives city, coordinates and the *current* UTC offset in one plain-HTTP
// call with no key. The offset already includes daylight saving, and this runs
// on every boot, so the clock corrects itself within a day of a DST change
// without needing a POSIX timezone rule or an IANA database on the device.
bool autoLocate() {
    if (!connected()) return false;
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    http.setReuse(false);
    if (!http.begin("http://ip-api.com/json/?fields=status,city,regionName,lat,lon,timezone,offset"))
        return false;
    int code = http.GET();
    String body = code == 200 ? http.getString() : String("");
    http.end();
    if (code != 200) { os::logf("locate: HTTP %d", code); return false; }

    JsonDocument d;
    if (deserializeJson(d, body)) { os::logf("locate: bad json"); return false; }
    if (d["status"].as<String>() != "success") { os::logf("locate: refused"); return false; }

    float lat = d["lat"].as<float>();
    float lon = d["lon"].as<float>();
    long offset = d["offset"].as<long>();
    String city = d["city"].as<String>();
    String region = d["regionName"].as<String>();
    String zone = d["timezone"].as<String>();
    String place = city + (region.length() ? ", " + region : "");

    // Seed Weather so it does not have to geocode anything.
    store::setStr("wxlat", String(lat, 4));
    store::setStr("wxlon", String(lon, 4));
    store::setStr("wxname", place);
    store::setStr("wxfor", store::getStr("wxplace", ""));
    store::setStr("tzname", zone);
    store::setInt("tzoffset", (int)offset);
    s_located = true;

    // Fixed offset rather than a TZ rule: we re-query every boot anyway.
    configTime(offset, 0, "pool.ntp.org", "time.google.com");
    struct tm t;
    if (getLocalTime(&t, 3000)) s_timeSynced = true;
    os::logf("locate: %s (%s, UTC%+ld) clock %s", place.c_str(), zone.c_str(),
             offset / 3600, s_timeSynced ? "set" : "pending");
    return true;
}

void syncTime() {
    if (!connected()) return;
    // A manually entered timezone always wins over the detected offset.
    String tz = store::getStr(store::K_TZ, "");
    if (!tz.length() && store::getStr("tzname", "").length()) {
        configTime(store::getInt("tzoffset", 0), 0, "pool.ntp.org", "time.google.com");
        struct tm t;
        if (getLocalTime(&t, 1500)) s_timeSynced = true;
        return;
    }
    if (!tz.length()) tz = "EST5EDT,M3.2.0,M11.1.0";
    configTzTime(tz.c_str(), "pool.ntp.org", "time.google.com");
    // Don't block the UI: a single short poll, tick() will call again.
    struct tm t;
    if (getLocalTime(&t, 1500)) s_timeSynced = true;
}

bool timeValid() { return s_timeSynced; }

String clockString() {
    if (!s_timeSynced) return "--:--";
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    return String(buf);
}

}  // namespace net
