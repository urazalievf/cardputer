#include "store.h"
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <sd_diskio.h>
#include "audio.h"
#include <time.h>
#include <ArduinoJson.h>
#include <algorithm>

namespace store {

// Cardputer SD slot (shared SPI bus).
static const int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;

static Preferences prefs;
static bool s_mounted = false;      // currently holding GPIO40 with SD mounted
static bool s_cardPresent = false;  // a card mounted successfully at least once
static uint64_t s_sizeMB = 0, s_usedMB = 0;
static uint32_t s_lastFail = 0;
static bool s_usbOwned = false;

void begin() {
    prefs.begin("cfg", false);
    prefs.end();
    sdMount();
    if (s_cardPresent) ensureDir(NOTES_DIR);
}

// isKey() first: Preferences logs an ERROR for every miss, and on first boot
// almost every key is a miss. Suppressing the whole ARDUINO log tag instead
// would also hide the SD mount failures, which are worth seeing.
String getStr(const char* key, const String& def) {
    if (!prefs.begin("cfg", true)) return def;
    String v = prefs.isKey(key) ? prefs.getString(key, def) : def;
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
    int v = prefs.isKey(key) ? prefs.getInt(key, def) : def;
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

void factoryReset() {
    // Only the namespaces the OS owns. Markdown on the SD card is the user's.
    for (const char* ns : {"cfg", "chat", "notes"}) {
        Preferences p;
        if (p.begin(ns, false)) { p.clear(); p.end(); }
    }
}

// ---------------- SD ----------------
bool sdReady() { return s_cardPresent; }

void sdRelease() {
    if (!s_mounted) return;
    SD.end();
    SPI.end();
    s_mounted = false;
}

void setUsbOwned(bool owned) {
    s_usbOwned = owned;
    if (owned) sdRelease();
    else s_lastFail = 0;            // let the next access retry immediately
}
bool usbOwned() { return s_usbOwned; }

bool sdAcquire(bool force) {
    if (s_usbOwned) return false;   // the Mac has it
    if (s_mounted) return true;
    // A failed mount costs two SD.begin attempts, ~200ms. Without a backoff
    // every listNotes() on a card-less device pays that, which reads as lag.
    // Explicit remounts from the UI pass force=true.
    if (!force && s_lastFail && millis() - s_lastFail < 3000) return false;
    // Evict audio: the I2S bit clock and the SD clock are both GPIO40.
    audio::releaseI2S();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    // 25MHz suits most cards; some older ones only enumerate slower.
    s_mounted = SD.begin(SD_CS, SPI, 25000000);
    if (!s_mounted) s_mounted = SD.begin(SD_CS, SPI, 4000000);
    if (s_mounted) {
        if (!s_cardPresent) {
            uint8_t type = SD.cardType();
            const char* t = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC"
                          : type == CARD_SDHC ? "SDHC" : "unknown";
            os::logf("sd: mounted %s, %llu MB (FAT16/FAT32 only - exFAT will not mount)",
                     t, SD.cardSize() / (1024ULL * 1024ULL));
        }
        s_cardPresent = true;
        s_lastFail = 0;
        s_sizeMB = SD.totalBytes() / (1024ULL * 1024ULL);
        s_usedMB = SD.usedBytes() / (1024ULL * 1024ULL);
    } else {
        s_lastFail = millis() ? millis() : 1;
        // The card can enumerate fine and still fail here: the Arduino SD
        // library mounts FAT16/FAT32 only, and any card 64GB+ ships exFAT.
        // Watch the serial log for "no valid FAT volume" to tell them apart.
        os::logf("sd: mount failed - card absent, or not FAT32 (exFAT/NTFS won't mount)");
        SPI.end();
    }
    return s_mounted;
}

bool sdMount(bool force) { return sdAcquire(force); }

// Cached so the status bar can read them without stealing GPIO40 from the mic.
uint64_t sdTotalMB() { return s_sizeMB; }
uint64_t sdUsedMB()  { return s_usedMB; }

bool exists(const String& path) { return sdAcquire() && SD.exists(path); }

bool ensureDir(const String& path) {
    if (!sdAcquire()) return false;
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

bool writeFile(const String& path, const String& content) {
    if (!sdAcquire()) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

bool appendFile(const String& path, const String& content) {
    if (!sdAcquire()) return false;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

String readFile(const String& path) {
    if (!sdAcquire()) return "";
    File f = SD.open(path, FILE_READ);
    if (!f) return "";
    String out;
    out.reserve(f.size() + 1);
    while (f.available()) out += (char)f.read();
    f.close();
    return out;
}

bool removeFile(const String& path) { return sdAcquire() && SD.remove(path); }

bool makeDir(const String& path) {
    if (!sdAcquire()) return false;
    if (SD.exists(path)) return false;
    return SD.mkdir(path);
}

bool removeDir(const String& path) {
    if (!sdAcquire()) return false;
    return SD.rmdir(path);
}

bool rename(const String& from, const String& to) {
    if (!sdAcquire()) return false;
    if (SD.exists(to)) return false;
    return SD.rename(from, to);
}

bool isDir(const String& path) {
    if (!sdAcquire()) return false;
    File f = SD.open(path);
    if (!f) return false;
    bool d = f.isDirectory();
    f.close();
    return d;
}

bool formatSd(String& err) {
    if (s_usbOwned) { err = "the host has the card"; return false; }
    audio::releaseI2S();
    sdRelease();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    // Two cases, and they need opposite treatment.
    //
    // If the card already mounts, format_if_empty will not fire -- the mount
    // succeeds, so nothing gets formatted. Clear the boot sectors through the
    // mounted handle first so the next mount fails on purpose.
    //
    // If it does not mount (exFAT, NTFS, a fresh card), the mount failing is
    // exactly the trigger, and we can go straight to it.
    //
    // Deliberately NOT using a bare sdcard_init() here: it hands back a drive
    // slot even when the card never initialised, and the geometry it then
    // reports is uninitialised memory.
    if (SD.begin(SD_CS, SPI, 20000000, "/sd", 5, /*format_if_empty=*/false)) {
        uint32_t ss = SD.sectorSize();
        if (!ss || ss > 4096) ss = 512;
        uint8_t* zero = (uint8_t*)calloc(1, ss);
        if (!zero) { SD.end(); SPI.end(); err = "out of memory"; return false; }
        bool wiped = true;
        for (uint32_t sector = 0; sector < 64 && wiped; sector++)
            wiped = SD.writeRAW(zero, sector);
        free(zero);
        SD.end();
        if (!wiped) {
            SPI.end();
            s_lastFail = millis() ? millis() : 1;
            err = "card refused writes - is the lock switch on?";
            return false;
        }
        os::logf("format: existing filesystem cleared");
    }

    // The mount now fails, which is what makes format_if_empty build a fresh
    // FAT volume. exFAT is not compiled into this FATFS, so it produces FAT32.
    bool ok = SD.begin(SD_CS, SPI, 20000000, "/sd", 5, /*format_if_empty=*/true);
    if (!ok) {
        SPI.end();
        s_lastFail = millis() ? millis() : 1;
        err = "format failed - no card, or the card is faulty";
        return false;
    }

    s_mounted = true;
    s_cardPresent = true;
    s_lastFail = 0;
    s_sizeMB = SD.totalBytes() / (1024ULL * 1024ULL);
    s_usedMB = SD.usedBytes() / (1024ULL * 1024ULL);
    ensureDir(NOTES_DIR);
    os::logf("format: done, %llu MB as FAT", s_sizeMB);
    return true;
}

std::vector<Entry> listDir(const String& path) {
    std::vector<Entry> out;
    if (!sdAcquire()) return out;
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return out;
    File e = dir.openNextFile();
    while (e) {
        String n = String(e.name());
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (!n.startsWith("."))
            out.push_back({n, e.isDirectory(), (size_t)e.size(), (uint32_t)e.getLastWrite()});
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
    String json = p.isKey("index") ? p.getString("index", "") : String("");
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
    if (sdAcquire()) {
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
    if (sdAcquire()) return readFile(String(NOTES_DIR) + "/" + file);
    Preferences p;
    if (!p.begin(NVS_NOTES_NS, true)) return "";
    String v = p.getString(file.substring(0, 15).c_str(), "");
    p.end();
    return v;
}

bool writeNote(const String& file, const String& body) {
    if (sdAcquire()) {
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
    if (sdAcquire()) return removeFile(String(NOTES_DIR) + "/" + file);
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
