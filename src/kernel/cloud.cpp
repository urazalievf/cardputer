#include "cloud.h"
#include "store.h"
#include "net.h"
#include "audio.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

namespace cloud {

static bool     s_hostOnline = false;
static uint32_t s_lastPing = 0;

void begin() { s_lastPing = 0; }

String hostBase() {
    String h = store::getStr(store::K_HOST, "");
    if (!h.length()) return "";
    int port = store::getInt(store::K_HOST_PORT, 8787);
    return "http://" + h + ":" + String(port);
}

bool pingHost(uint32_t timeoutMs) {
    if (!net::connected()) return false;
    String base = hostBase();
    if (!base.length()) return false;
    HTTPClient http;
    http.setConnectTimeout(timeoutMs);
    http.setTimeout(timeoutMs);
    if (!http.begin(base + "/ping")) return false;
    int code = http.GET();
    http.end();
    return code == 200;
}

bool hostOnline() {
    if (millis() - s_lastPing > 8000) {
        s_lastPing = millis();
        s_hostOnline = pingHost();
    }
    return s_hostOnline;
}

bool discoverHost() {
    if (!net::connected()) return false;
    if (!MDNS.begin("cardputer")) return false;
    int n = MDNS.queryService("cardputerd", "tcp");
    if (n <= 0) return false;
    store::setStr(store::K_HOST, MDNS.hostname(0) + ".local");
    store::setInt(store::K_HOST_PORT, MDNS.port(0));
    s_lastPing = 0;
    return true;
}

// ---------------- helpers ----------------
static Result hostPost(const String& path, const String& body, uint32_t timeoutMs = 120000) {
    Result r;
    String base = hostBase();
    if (!base.length()) { r.error = "no host configured"; return r; }
    if (!net::connected()) { r.error = "wifi off"; return r; }

    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(timeoutMs);
    if (!http.begin(base + path)) { r.error = "host unreachable"; return r; }
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code <= 0) { r.error = String("host: ") + http.errorToString(code); http.end(); return r; }
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) { r.error = "host: bad json"; return r; }
    if (code != 200) {
        r.error = doc["error"].is<const char*>() ? doc["error"].as<String>()
                                                 : String("host ") + code;
        return r;
    }
    r.ok = true;
    r.source = Source::Host;
    r.text = doc["response"].is<const char*>() ? doc["response"].as<String>()
                                               : doc["text"].as<String>();
    return r;
}

static Result anthropicAsk(const String& prompt, const String& system, int maxTokens) {
    Result r;
    String key = store::getStr(store::K_ANTHROPIC, "");
    if (!key.length()) { r.error = "no Anthropic key (Settings)"; return r; }
    if (!net::connected()) { r.error = "wifi off"; return r; }

    WiFiClientSecure client;
    client.setInsecure();          // no cert bundle in 8MB; LAN-free but TLS-verified would be better
    HTTPClient http;
    http.setTimeout(45000);
    if (!http.begin(client, "https://api.anthropic.com/v1/messages")) { r.error = "tls init"; return r; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", key);
    http.addHeader("anthropic-version", "2023-06-01");

    JsonDocument req;
    req["model"] = store::getStr(store::K_MODEL, "claude-haiku-4-5-20251001");
    req["max_tokens"] = maxTokens;
    if (system.length()) req["system"] = system;
    JsonArray msgs = req["messages"].to<JsonArray>();
    JsonObject m = msgs.add<JsonObject>();
    m["role"] = "user";
    m["content"] = prompt;

    String body;
    serializeJson(req, body);
    int code = http.POST(body);
    if (code <= 0) { r.error = String("net: ") + http.errorToString(code); http.end(); return r; }
    String payload = http.getString();
    http.end();

    JsonDocument resp;
    if (deserializeJson(resp, payload)) { r.error = "bad json"; return r; }
    if (code != 200) {
        r.error = resp["error"]["message"].is<const char*>()
                      ? resp["error"]["message"].as<String>()
                      : String("http ") + code;
        return r;
    }
    r.ok = true;
    r.source = Source::Api;
    r.text = resp["content"][0]["text"].as<String>();
    return r;
}

// ---------------- public API ----------------
Result ask(const String& prompt, const String& system, int maxTokens) {
    if (hostOnline()) {
        JsonDocument req;
        req["prompt"] = prompt;
        req["system"] = system;
        req["max_tokens"] = maxTokens;
        String body;
        serializeJson(req, body);
        Result r = hostPost("/ask", body);
        if (r.ok) return r;
        // Fall through to the API on host failure rather than dead-ending.
    }
    Result r = anthropicAsk(prompt, system, maxTokens);
    if (!r.ok && !r.error.length()) r.error = "no backend available";
    return r;
}

Result code(const String& prompt, const String& project) {
    Result r;
    if (!hostOnline()) { r.error = "Claude Code needs the Mac daemon"; return r; }
    JsonDocument req;
    req["prompt"] = prompt;
    req["project"] = project;
    String body;
    serializeJson(req, body);
    return hostPost("/code", body, 300000);
}

// ---------------- transcription ----------------
static Result hostTranscribe(const int16_t* pcm, size_t samples) {
    Result r;
    String base = hostBase();
    if (!base.length()) { r.error = "no host"; return r; }

    size_t pcmBytes = samples * sizeof(int16_t);
    uint8_t hdr[44];
    audio::wavHeader(hdr, pcmBytes);

    WiFiClient client;
    String host = store::getStr(store::K_HOST, "");
    int port = store::getInt(store::K_HOST_PORT, 8787);
    if (!client.connect(host.c_str(), port, 3000)) { r.error = "host unreachable"; return r; }

    client.printf("POST /transcribe HTTP/1.1\r\nHost: %s\r\n", host.c_str());
    client.print("Content-Type: audio/wav\r\n");
    client.printf("Content-Length: %u\r\n", (unsigned)(44 + pcmBytes));
    client.print("Connection: close\r\n\r\n");
    client.write(hdr, 44);

    const uint8_t* p = (const uint8_t*)pcm;
    size_t off = 0;
    while (off < pcmBytes) {
        size_t n = min((size_t)2048, pcmBytes - off);
        size_t w = client.write(p + off, n);
        if (!w) { client.stop(); r.error = "upload failed"; return r; }
        off += w;
    }

    uint32_t start = millis();
    while (!client.available() && client.connected() && millis() - start < 120000) delay(20);
    String statusLn = client.readStringUntil('\n');
    int sp = statusLn.indexOf(' ');
    int codeNum = sp >= 0 ? statusLn.substring(sp + 1, sp + 4).toInt() : 0;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 1) break;
    }
    String payload;
    while ((client.connected() || client.available()) && payload.length() < 4000) {
        if (client.available()) payload += (char)client.read();
        else delay(5);
    }
    client.stop();

    if (codeNum != 200) { r.error = String("host ") + codeNum; return r; }
    JsonDocument doc;
    if (!deserializeJson(doc, payload) && doc["text"].is<const char*>()) {
        r.ok = true; r.source = Source::Host; r.text = doc["text"].as<String>();
        return r;
    }
    payload.trim();
    r.ok = payload.length() > 0;
    r.source = Source::Host;
    r.text = payload;
    return r;
}

static Result whisperTranscribe(const int16_t* pcm, size_t samples) {
    Result r;
    String key = store::getStr(store::K_OPENAI, "");
    if (!key.length()) { r.error = "no OpenAI key (Settings)"; return r; }
    if (!net::connected()) { r.error = "wifi off"; return r; }

    size_t pcmBytes = samples * sizeof(int16_t);
    const char* boundary = "----CardputerOSBoundary7MA4YWxkTrZ";

    String pre =
        String("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"r.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String tail = String("\r\n--") + boundary + "--\r\n";

    uint8_t hdr[44];
    audio::wavHeader(hdr, pcmBytes);
    size_t bodyLen = pre.length() + 44 + pcmBytes + tail.length();

    // Streamed by hand: the whole multipart body would never fit in RAM.
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);
    if (!client.connect("api.openai.com", 443)) { r.error = "tls connect failed"; return r; }

    client.print("POST /v1/audio/transcriptions HTTP/1.1\r\n");
    client.print("Host: api.openai.com\r\n");
    client.print("Authorization: Bearer "); client.print(key); client.print("\r\n");
    client.print("Content-Type: multipart/form-data; boundary="); client.print(boundary);
    client.printf("\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)bodyLen);

    client.print(pre);
    client.write(hdr, 44);
    const uint8_t* p = (const uint8_t*)pcm;
    size_t off = 0;
    while (off < pcmBytes) {
        size_t n = min((size_t)2048, pcmBytes - off);
        size_t w = client.write(p + off, n);
        if (!w) { client.stop(); r.error = "upload failed"; return r; }
        off += w;
    }
    client.print(tail);
    client.flush();

    uint32_t start = millis();
    while (!client.available() && client.connected() && millis() - start < 60000) delay(20);
    String statusLn = client.readStringUntil('\n');
    int sp = statusLn.indexOf(' ');
    int codeNum = sp >= 0 ? statusLn.substring(sp + 1, sp + 4).toInt() : 0;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 1) break;
    }
    String payload;
    while ((client.connected() || client.available()) && payload.length() < 3000) {
        if (client.available()) payload += (char)client.read();
        else delay(5);
    }
    client.stop();

    payload.trim();
    if (codeNum != 200) { r.error = String("whisper ") + codeNum + ": " + payload.substring(0, 120); return r; }
    r.ok = true;
    r.source = Source::Api;
    r.text = payload;
    return r;
}

Result transcribe(const int16_t* pcm, size_t samples) {
    if (samples == 0) { Result r; r.error = "nothing recorded"; return r; }
    if (hostOnline()) {
        Result r = hostTranscribe(pcm, samples);
        if (r.ok) return r;
    }
    return whisperTranscribe(pcm, samples);
}

// ---------------- vault ----------------
Result vaultWrite(const String& path, const String& content, bool append) {
    Result r;
    if (!hostOnline()) { r.error = "vault sync needs the Mac daemon"; return r; }
    JsonDocument req;
    req["path"] = path;
    req["content"] = content;
    req["append"] = append;
    String body;
    serializeJson(req, body);
    return hostPost("/vault/note", body, 20000);
}

Result vaultRead(const String& path) {
    Result r;
    if (!hostOnline()) { r.error = "vault needs the Mac daemon"; return r; }
    JsonDocument req;
    req["path"] = path;
    String body;
    serializeJson(req, body);
    return hostPost("/vault/read", body, 20000);
}

Result vaultDailyAppend(const String& content) {
    Result r;
    if (!hostOnline()) { r.error = "vault sync needs the Mac daemon"; return r; }
    JsonDocument req;
    req["content"] = content;
    String body;
    serializeJson(req, body);
    return hostPost("/vault/daily", body, 20000);
}

std::vector<String> vaultList(const String& dir) {
    std::vector<String> out;
    if (!hostOnline()) return out;
    String base = hostBase();
    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(15000);
    if (!http.begin(base + "/vault/list?dir=" + dir)) return out;
    int code = http.GET();
    if (code == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString()))
            for (JsonVariant v : doc["files"].as<JsonArray>()) out.push_back(v.as<String>());
    }
    http.end();
    return out;
}

}  // namespace cloud
