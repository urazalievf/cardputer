// Host-first network brain.
//
// Every capability is tried against the companion daemon on your Mac first
// (free, has your Claude Max subscription and your real filesystem), and falls
// back to direct cloud APIs when the daemon isn't reachable.
#pragma once
#include "os.h"
#include <vector>

namespace cloud {

enum class Source { None, Host, Api };

struct Result {
    bool ok = false;
    String text;
    Source source = Source::None;
    String error;
    const char* sourceName() const {
        return source == Source::Host ? "mac" : source == Source::Api ? "api" : "-";
    }
};

void begin();

// --- host daemon ---
String hostBase();                 // "http://mac.local:8787", or "" if unset
bool   hostOnline();               // cached, refreshed every few seconds
bool   pingHost(uint32_t timeoutMs = 1200);
bool   discoverHost();             // mDNS lookup for _cardputerd._tcp

// --- language model ---
// `system` may be empty. Host path shells out to the `claude` CLI; API path
// hits api.anthropic.com with the key stored in NVS.
Result ask(const String& prompt, const String& system = "", int maxTokens = 400);

// Full Claude Code: runs in a project directory on the host, with tools.
// Host-only — returns ok=false when the daemon is unreachable.
Result code(const String& prompt, const String& project = "");

// --- speech to text ---
Result transcribe(const int16_t* pcm, size_t samples);

// --- Obsidian vault (host-only) ---
Result vaultWrite(const String& path, const String& content, bool append = false);
Result vaultRead(const String& path);
std::vector<String> vaultList(const String& dir = "");
Result vaultDailyAppend(const String& content);   // append to today's daily note

}  // namespace cloud
