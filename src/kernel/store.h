// Persistence: NVS-backed config + key/value, and the SD card filesystem.
// Nothing secret ever lives in the repo — API keys are typed on-device and
// kept in NVS under the "cfg" namespace.
#pragma once
#include "os.h"
#include <vector>

namespace store {

void begin();

// --- Config (NVS namespace "cfg") ---
String getStr(const char* key, const String& def = "");
void   setStr(const char* key, const String& value);
int    getInt(const char* key, int def = 0);
void   setInt(const char* key, int value);
void   remove(const char* key);
void   factoryReset();          // wipes cfg, wifi, chat; leaves SD notes alone

// While the host has the card over USB, nothing here may touch it: two owners
// of one filesystem is corruption.
void   setUsbOwned(bool owned);
bool   usbOwned();

// Reformat the card to FAT32, erasing everything on it. Blocking and slow on a
// large card, so callers should wrap it in ui::await(). Returns false with a
// reason in `err`.
bool   formatSd(String& err);

// Verbose step-by-step probe written to the serial log. store::begin() runs
// before the console exists, so a mount failure at boot is otherwise silent.
void   diagnose();

// Well-known config keys.
static const char* K_HOST        = "host";       // companion daemon, e.g. "mac.local"
static const char* K_HOST_PORT   = "hostport";
static const char* K_ANTHROPIC   = "anthkey";
static const char* K_OPENAI      = "oaikey";
static const char* K_MODEL       = "model";
static const char* K_BRIGHT      = "bright";
static const char* K_VAULT       = "vault";      // Obsidian subfolder for device notes
static const char* K_TZ          = "tz";         // POSIX TZ string for NTP

// --- SD card ---
bool sdReady();          // a working card was seen, whether or not it's mounted now
bool sdMounted();        // ...and it is mounted right now
bool sdMount(bool force = false);     // safe to call repeatedly
bool sdAcquire(bool force = false);  // claim GPIO40 from audio; every SD op calls this
void sdRelease();        // unmount so audio can have GPIO40 back
uint64_t sdTotalMB();
uint64_t sdUsedMB();

bool  writeFile(const String& path, const String& content);
bool  appendFile(const String& path, const String& content);
String readFile(const String& path);
bool  removeFile(const String& path);
bool  exists(const String& path);
bool  ensureDir(const String& path);
bool  makeDir(const String& path);
bool  removeDir(const String& path);          // must already be empty
bool  rename(const String& from, const String& to);
bool  isDir(const String& path);

struct Entry { String name; bool isDir; size_t size; uint32_t mtime = 0; };
std::vector<Entry> listDir(const String& path);

// --- Notes (markdown on SD, NVS mirror when no card) ---
static const char* NOTES_DIR = "/notes";
static const char* REC_DIR   = "/recordings";
std::vector<String> listNotes();                 // filenames, newest first
String readNote(const String& file);
bool   writeNote(const String& file, const String& body);
bool   deleteNote(const String& file);
String newNoteName(const String& title);         // 2026-08-24-1432-title.md

// Write captured audio to the card as a playable 16-bit mono WAV. Called after
// the microphone has let go of GPIO40, so the card can be mounted again.
bool   writeWav(const String& path, const int16_t* pcm, size_t samples);
String newRecordingName();                       // 20260824-143210.wav

// Streaming WAV: open once, append each chunk as it is captured, patch the
// header on close. A memo is then bounded by the card and by patience rather
// than by the ~140KB largest free block, which is what held it to four seconds.
bool   wavOpen(const String& path);
bool   wavAppend(const int16_t* pcm, size_t samples);
bool   wavClose();                               // patches RIFF/data sizes
void   wavAbort();                               // close and delete
bool   wavOpenNow();
size_t wavSamples();

// Whether this board actually needs audio evicted before the card will mount.
// The Cardputer's SD bus (SCK 40, MISO 39, MOSI 14, CS 12) shares no pin with
// the microphone (PDM: data 46, clock 43) or the speaker (41/43/42), so they
// should coexist -- but the card is not worth gambling on a pinout read, so
// sdAcquire() tries coexisting first and latches this if the board disagrees.
bool   audioConflicts();

}  // namespace store
