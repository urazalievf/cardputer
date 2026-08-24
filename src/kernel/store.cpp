#include "store.h"
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <ArduinoJson.h>
#include <algorithm>

namespace store {

// Cardputer SD slot (shared SPI bus).
static const int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;

static Preferences prefs;
static bool s_sd = false;

void begin() {
    prefs.begin("cfg", false);
    prefs.end();
    sdMount();
    if (s_sd) ensureDir(NOTES_DIR);
}

String getStr(const char* key, const String& def) {
    if (!prefs.begin("cfg", true)) return def;
    String v = prefs.getString(key, def);
    prefs.end();
    return v;
}
void setStr(const char* key, const String& value) {
    if (!prefs.begin("cfg", false)) return;
    prefs.putString(key, value);
    prefs.end();
}
int getInt(const char* key, int def) {
    if (!prefs.begin("cfg", true)) return def;
    int v = prefs.getInt(key, def);
    prefs.end();
    return v;
}
void setInt(const char* key, int value) {
    if (!prefs.begin("cfg", false)) return;
    prefs.putInt(key, value);
    prefs.end();
}
void remove(const char* key) {
    if (!prefs.begin("cfg", false)) return;
    prefs.remove(key);
    prefs.end();
}

// ---------------- SD ----------------
bool sdReady() { return s_sd; }

bool sdMount() {
    if (s_sd) return true;
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_sd = SD.begin(SD_CS, SPI, 25000000);
    return s_sd;
}

uint64_t sdTotalMB() { return s_sd ? SD.totalBytes() / (1024ULL * 1024ULL) : 0; }
uint64_t sdUsedMB()  { return s_sd ? SD.usedBytes()  / (1024ULL * 1024ULL) : 0; }

bool exists(const String& path) { return s_sd && SD.exists(path); }

bool ensureDir(const String& path) {
    if (!s_sd) return false;
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

bool writeFile(const String& path, const String& content) {
    if (!s_sd) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

bool appendFile(const String& path, const String& content) {
    if (!s_sd) return false;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

String readFile(const String& path) {
    if (!s_sd) return "";
    File f = SD.open(path, FILE_READ);
    if (!f) return "";
    String out;
    out.reserve(f.size() + 1);
    while (f.available()) out += (char)f.read();
    f.close();
    return out;
}

bool removeFile(const String& path) { return s_sd && SD.remove(path); }

std::vector<Entry> listDir(const String& path) {
    std::vector<Entry> out;
    if (!s_sd) return out;
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return out;
    File e = dir.openNextFile();
    while (e) {
        String n = String(e.name());
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (!n.startsWith(".")) out.push_back({n, e.isDirectory(), (size_t)e.size()});
        e.close();
        e = dir.openNextFile();
    }
    dir.close();
    return out;
}

// ---------------- Notes ----------------
// With an SD card, each note is a real markdown file so it can be dropped
// straight into an Obsidian vault. Without one, notes fall back to a single
// JSON blob in NVS (a few KB, enough to not lose thoughts mid-trip).

static const char* NVS_NOTES_NS = "notes";

static std::vector<String> nvsNoteNames() {
    std::vector<String> out;
    Preferences p;
    if (!p.begin(NVS_NOTES_NS, true)) return out;
    String json = p.getString("index", "");
    p.end();
    if (!json.length()) return out;
    JsonDocument doc;
    if (deserializeJson(doc, json)) return out;
    for (JsonVariant v : doc.as<JsonArray>()) out.push_back(v.as<String>());
    return out;
}

static void nvsSaveIndex(const std::vector<String>& names) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto& n : names) arr.add(n);
    String json;
    serializeJson(doc, json);
    Preferences p;
    if (!p.begin(NVS_NOTES_NS, false)) return;
    p.putString("index", json);
    p.end();
}

std::vector<String> listNotes() {
    std::vector<String> names;
    if (s_sd) {
        for (auto& e : listDir(NOTES_DIR))
            if (!e.isDir && e.name.endsWith(".md")) names.push_back(e.name);
    } else {
        names = nvsNoteNames();
    }
    // Filenames start with a sortable timestamp, so reverse-sort = newest first.
    std::sort(names.begin(), names.end(), [](const String& a, const String& b) { return a > b; });
    return names;
}

String readNote(const String& file) {
    if (s_sd) return readFile(String(NOTES_DIR) + "/" + file);
    Preferences p;
    if (!p.begin(NVS_NOTES_NS, true)) return "";
    String v = p.getString(file.substring(0, 15).c_str(), "");
    p.end();
    return v;
}

bool writeNote(const String& file, const String& body) {
    if (s_sd) {
        ensureDir(NOTES_DIR);
        return writeFile(String(NOTES_DIR) + "/" + file, body);
    }
    Preferences p;
    if (!p.begin(NVS_NOTES_NS, false)) return false;
    // NVS keys cap at 15 chars; the timestamp prefix is unique enough.
    p.putString(file.substring(0, 15).c_str(), body);
    p.end();
    auto names = nvsNoteNames();
    if (std::find(names.begin(), names.end(), file) == names.end()) {
        names.push_back(file);
        nvsSaveIndex(names);
    }
    return true;
}

bool deleteNote(const String& file) {
    if (s_sd) return removeFile(String(NOTES_DIR) + "/" + file);
    Preferences p;
    if (p.begin(NVS_NOTES_NS, false)) { p.remove(file.substring(0, 15).c_str()); p.end(); }
    auto names = nvsNoteNames();
    names.erase(std::remove(names.begin(), names.end(), file), names.end());
    nvsSaveIndex(names);
    return true;
}

String newNoteName(const String& title) {
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char stamp[24];
    // Pre-NTP the clock reads 1970; still unique and still sorts.
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);

    String slug;
    for (size_t i = 0; i < title.length() && slug.length() < 24; i++) {
        char c = title[i];
        if (isalnum((int)c))      slug += (char)tolower(c);
        else if (slug.length() && slug[slug.length() - 1] != '-') slug += '-';
    }
    while (slug.endsWith("-")) slug.remove(slug.length() - 1);
    if (!slug.length()) slug = "note";
    return String(stamp) + "-" + slug + ".md";
}

}  // namespace store
