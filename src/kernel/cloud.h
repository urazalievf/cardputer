// The Mac companion daemon: the one peer that has a filesystem, your CLI
// subscriptions and your Obsidian vault. ai.cpp routes through this for the
// Host provider; the vault and coding-agent paths are host-only by nature.
#pragma once
#include "os.h"
#include <vector>

namespace cloud {

struct Result {
    bool ok = false;
    String text;
    String error;
};

void begin();

String hostBase();
// Online means reachable AND willing to answer: /ping is deliberately open so
// the device can find the daemon, so a reachable daemon with the wrong token
// used to look perfectly configured right up until every real request came
// back 401. These three keep the two facts apart.
bool   hostOnline();                 // cached /ping, refreshed every 8s
bool   hostReachable();              // answered /ping
bool   hostAuthorised();             // ...and accepted our token
bool   pingHost(uint32_t timeoutMs = 1500);
bool   discoverHost();               // mDNS _cardputerd._tcp
String hostFeatures();               // human-readable summary from /ping
// Which agent CLIs the daemon actually found installed. These authenticate
// with your account on the Mac, so "log in to Gemini" means running `gemini`
// once there -- no API key ever reaches the device.
std::vector<String> hostBackends();

Result hostPost(const String& path, const String& jsonBody, uint32_t timeoutMs = 120000);
Result hostTranscribe(const int16_t* pcm, size_t samples);
// Same endpoint, but the WAV is streamed off the card rather than out of RAM.
Result hostTranscribeFile(const String& path);

// A coding agent with real tools, in a real project directory.
Result code(const String& prompt, const String& project = "", const String& backend = "");

Result vaultWrite(const String& path, const String& content, bool append = false);
Result vaultRead(const String& path);
Result vaultDailyAppend(const String& content);
std::vector<String> vaultList(const String& dir = "");

}  // namespace cloud
