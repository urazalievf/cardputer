// The Mac companion daemon: the one peer that has a filesystem, your CLI
// subscriptions and your Obsidian vault. ai.cpp routes through this for the
// Host provider; the vault and Claude Code paths are host-only by nature.
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
bool   hostOnline();                 // cached /ping, refreshed every 8s
bool   pingHost(uint32_t timeoutMs = 1500);
bool   discoverHost();               // mDNS _cardputerd._tcp
String hostFeatures();               // human-readable summary from /ping

Result hostPost(const String& path, const String& jsonBody, uint32_t timeoutMs = 120000);
Result hostTranscribe(const int16_t* pcm, size_t samples);

// Claude Code / Codex with real tools, in a real project directory.
Result code(const String& prompt, const String& project = "", const String& backend = "");

Result vaultWrite(const String& path, const String& content, bool append = false);
Result vaultRead(const String& path);
Result vaultDailyAppend(const String& content);
std::vector<String> vaultList(const String& dir = "");

}  // namespace cloud
