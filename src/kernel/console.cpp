#include "console.h"
#include "store.h"
#include "net.h"
#include "theme.h"
#include "ai.h"
#include "audio.h"
#include <SD.h>

namespace console {

static String s_line;

static Print& out() { return os::consoleStream(); }

// Secrets echo back masked. Anyone with the cable can read them out of NVS
// anyway, but a terminal scrollback is a much easier place to leak one from.
static bool isSecret(const String& key) { return key.startsWith("k_"); }

static String mask(const String& v) {
    if (v.length() <= 4) return "****";
    return String("****") + v.substring(v.length() - 4) + "  (" + v.length() + " chars)";
}

static void help() {
    out().println(F("commands:"));
    out().println(F("  set <key> <value>     write a text setting (rest of line is the value)"));
    out().println(F("  seti <key> <number>   write a numeric setting"));
    out().println(F("  use <provider>        claude|chatgpt|gemini|groq|openrouter|ollama|mac"));
    out().println(F("  get <key>             read one setting"));
    out().println(F("  del <key>             remove a setting"));
    out().println(F("  keys                  list known settings and whether they are set"));
    out().println(F("  wifi <ssid> <pass>    save a network (pass may be empty for open)"));
    out().println(F("  nets                  list saved networks"));
    out().println(F("  ask <text>            put a question to the current assistant"));
    out().println(F("  info                  boot report"));
    out().println(F("  ls [dir]              list a directory on the card"));
    out().println(F("  cat <path>            print a file from the card"));
    out().println(F("  reboot"));
}

// The settings worth reaching from a script. Anything else is still writable
// with `set`; this list is what `keys` reports on.
struct Known { const char* key; const char* what; };
static const Known KNOWN[] = {
    {"k_anthropic", "Claude API key"},
    {"k_openai",    "OpenAI API key"},
    {"k_gemini",    "Gemini API key"},
    {"k_groq",      "Groq API key"},
    {"k_openrtr",   "OpenRouter API key"},
    {"ollamahost",  "Ollama host:port"},
    {"host",        "Mac daemon host"},
    {"hosttoken",   "Mac daemon shared secret"},
    {"hostport",    "Mac daemon port (seti)"},
    {"m_host",      "agent CLI (claude/codex/gemini)"},
    {"vault",       "Obsidian subfolder"},
    {"tz",          "timezone override"},
    {"micrate",     "mic sample rate (seti)"},
    {"aiprov",      "assistant, 0=mac 1=claude 2=gpt (use)"},
};

static void listKeys() {
    for (auto& k : KNOWN) {
        String v = store::getStr(k.key, "");
        if (!v.length()) {
            // Numeric settings live in NVS as integers; getStr cannot see them.
            int n = store::getInt(k.key, INT32_MIN);
            if (n != INT32_MIN) v = String(n);
        }
        out().printf("%-14s %-38s %s\n", k.key, k.what,
                     v.length() ? (isSecret(k.key) ? "set" : v.c_str()) : "-");
    }
    out().printf("%-14s %-38s %s\n", "(assistant)", "currently asking",
                 ai::spec(ai::preferred()).label);
}

static void catFile(const String& path) {
    if (!store::sdReady()) { out().println(F("no card")); return; }
    String body = store::readFile(path);
    if (!body.length()) { out().println(F("empty or missing")); return; }
    out().println(body);
}

static void listDir(const String& path) {
    if (!store::sdReady()) { out().println(F("no card")); return; }
    auto entries = store::listDir(path.length() ? path : String("/"));
    if (entries.empty()) { out().println(F("(empty)")); return; }
    for (auto& e : entries)
        out().printf("%s%-30s %u\n", e.isDir ? "d " : "  ", e.name.c_str(), (unsigned)e.size);
}

static void dispatch(String line) {
    line.trim();
    if (!line.length()) return;

    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? String("") : line.substring(sp + 1);
    rest.trim();
    cmd.toLowerCase();

    if (cmd == "help" || cmd == "?") { help(); }
    else if (cmd == "set") {
        int s2 = rest.indexOf(' ');
        if (s2 < 0) { out().println(F("usage: set <key> <value>")); return; }
        String key = rest.substring(0, s2);
        String val = rest.substring(s2 + 1);
        val.trim();
        store::setStr(key.c_str(), val);
        out().printf("ok %s = %s\n", key.c_str(),
                     isSecret(key) ? mask(val).c_str() : val.c_str());
    } else if (cmd == "seti") {
        int s2 = rest.indexOf(' ');
        if (s2 < 0) { out().println(F("usage: seti <key> <number>")); return; }
        String key = rest.substring(0, s2);
        long val = rest.substring(s2 + 1).toInt();
        // NVS is typed: a number written as a string reads back as the default,
        // silently. Numeric settings need their own command.
        store::setInt(key.c_str(), (int)val);
        out().printf("ok %s = %ld\n", key.c_str(), val);
    } else if (cmd == "use") {
        String p = rest;
        p.toLowerCase();
        int idx = p == "mac" || p == "host"   ? 0
                : p == "claude" || p == "anthropic" ? 1
                : p == "chatgpt" || p == "openai"   ? 2
                : p == "gemini"                     ? 3
                : p == "groq"                       ? 4
                : p == "openrouter" || p == "router" ? 5
                : p == "ollama"                     ? 6 : -1;
        if (idx < 0) { out().println(F("usage: use claude|chatgpt|gemini|groq|openrouter|ollama|mac")); return; }
        ai::setPreferred((ai::Provider)idx);
        const ai::Spec& sp = ai::spec((ai::Provider)idx);
        out().printf("ok assistant = %s (%s)%s\n", sp.label, ai::model((ai::Provider)idx).c_str(),
                     ai::configured((ai::Provider)idx) ? "" : "  [not configured yet]");
    } else if (cmd == "get") {
        String v = store::getStr(rest.c_str(), "");
        if (!v.length()) out().printf("%s is not set\n", rest.c_str());
        else out().printf("%s = %s\n", rest.c_str(),
                          isSecret(rest) ? mask(v).c_str() : v.c_str());
    } else if (cmd == "del") {
        store::remove(rest.c_str());
        out().printf("ok removed %s\n", rest.c_str());
    } else if (cmd == "keys") {
        listKeys();
    } else if (cmd == "wifi") {
        int s2 = rest.indexOf(' ');
        String ssid = s2 < 0 ? rest : rest.substring(0, s2);
        String pass = s2 < 0 ? String("") : rest.substring(s2 + 1);
        if (!ssid.length()) { out().println(F("usage: wifi <ssid> <password>")); return; }
        net::saveNetwork(ssid, pass);
        out().printf("ok saved %s\n", ssid.c_str());
    } else if (cmd == "nets") {
        for (auto& n : net::savedNetworks()) out().println(n);
    } else if (cmd == "ask") {
        if (!rest.length()) { out().println(F("usage: ask <text>")); return; }
        out().printf("asking %s...\n", ai::spec(ai::preferred()).label);
        auto r = ai::ask(rest, "Reply in plain text, briefly.", 200);
        if (r.ok) out().printf("[%s, %lums] %s\n", r.usedLabel(),
                               (unsigned long)r.ms, r.text.c_str());
        else out().printf("FAILED: %s\n", r.error.c_str());
    } else if (cmd == "info") {
        os::bootReport();
    } else if (cmd == "ls") {
        listDir(rest);
    } else if (cmd == "cat") {
        catFile(rest);
    } else if (cmd == "reboot") {
        out().println(F("ok rebooting"));
        delay(120);
        ESP.restart();
    } else {
        out().printf("unknown command '%s' - try help\n", cmd.c_str());
    }
    out().println(F("--done--"));       // a marker the host tool can wait on
}

void poll() {
    Stream& in = os::consoleStream();
    while (in.available()) {
        char c = (char)in.read();
        if (c == '\r') continue;
        if (c == '\n') {
            String line = s_line;
            s_line = "";
            dispatch(line);
            continue;
        }
        if (s_line.length() < 512) s_line += c;
    }
}

}  // namespace console
