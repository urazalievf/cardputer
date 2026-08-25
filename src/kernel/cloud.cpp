#include "cloud.h"
#include "store.h"
#include "net.h"
#include "audio.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <SD.h>

namespace cloud {

static bool     s_online = false;
static bool     s_reachable = false;
static bool     s_authorised = false;
static uint32_t s_lastPing = 0;
static String   s_features = "";
static std::vector<String> s_backends;

void begin() { s_lastPing = 0; s_features = ""; }

String hostBase() {
    String h = store::getStr(store::K_HOST, "");
    if (!h.length()) return "";
    return "http://" + h + ":" + String(store::getInt(store::K_HOST_PORT, 8787));
}

bool pingHost(uint32_t timeoutMs) {
    s_reachable = false;
    s_authorised = false;
    if (!net::connected()) return false;
    String base = hostBase();
    if (!base.length()) return false;
    HTTPClient http;
    http.setConnectTimeout(timeoutMs);
    http.setTimeout(timeoutMs);
    if (!http.begin(base + "/ping")) return false;
    // Sent even though /ping does not require it: the daemon echoes back
    // whether it recognised us, which is the only cheap way to find out that
    // the token is wrong before a real request fails.
    String token = store::getStr("hosttoken", "");
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    int code = http.GET();
    if (code == 200) {
        s_reachable = true;
        JsonDocument d;
        if (!deserializeJson(d, http.getString())) {
            s_backends.clear();
            for (JsonVariant v : d["backends"].as<JsonArray>())
                s_backends.push_back(v.as<String>());
            s_features = String("v") + d["version"].as<String>() +
                         (d["claude"].as<bool>() ? " claude" : "") +
                         (d["vault"].as<bool>()  ? " vault"  : "") +
                         " stt:" + d["stt"].as<String>();
            // Daemons that predate the field do not report it; assume the best
            // rather than declaring a working setup broken.
            s_authorised = d["authorised"].is<bool>() ? d["authorised"].as<bool>() : true;
            if (!s_authorised) s_features += " (token rejected)";
        } else {
            s_authorised = true;      // answered, but not in JSON we understand
        }
    }
    http.end();
    // Direct callers (Settings > Test Mac) refresh the cache too, so the very
    // next hostOnline() does not fire a second round trip for the same answer.
    s_lastPing = millis();
    s_online = (code == 200) && s_authorised;
    return code == 200;
}

bool hostReachable() { hostOnline(); return s_reachable; }
bool hostAuthorised() { hostOnline(); return s_authorised; }

bool hostOnline() {
    if (millis() - s_lastPing > 8000) {
        s_lastPing = millis();
        s_online = pingHost();
    }
    // A daemon that will not accept our token cannot answer a single useful
    // request, so counting it as a configured provider only means the fallback
    // chain never runs and the user sees 401 instead of the next provider.
    return s_online && s_authorised;
}

String hostFeatures() { return s_features; }

std::vector<String> hostBackends() {
    if (s_backends.empty() && hostOnline()) pingHost();
    return s_backends;
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

Result hostPost(const String& path, const String& body, uint32_t timeoutMs) {
    Result r;
    String base = hostBase();
    if (!base.length()) { r.error = "no host configured"; return r; }
    if (!net::connected()) { r.error = "wifi off"; return r; }

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    if (!http.begin(base + path)) { r.error = "host unreachable"; return r; }
    http.addHeader("Content-Type", "application/json");
    String token = store::getStr("hosttoken", "");
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    int code = http.POST(body);
    if (code <= 0) { r.error = String("host: ") + http.errorToString(code); http.end(); return r; }
    String payload = http.getString();
    http.end();

    JsonDocument d;
    if (deserializeJson(d, payload)) { r.error = "host: bad json"; return r; }
    if (code != 200) {
        r.error = d["error"].is<const char*>() ? d["error"].as<String>()
                                               : String("host ") + code;
        return r;
    }
    r.text = d["response"].is<const char*>() ? d["response"].as<String>()
                                             : d["text"].as<String>();
    r.ok = true;
    return r;
}

static Result hostTranscribeSrc(const int16_t* pcm, size_t samples, const String& path) {
    Result r;
    String host = store::getStr(store::K_HOST, "");
    if (!host.length()) { r.error = "no host"; return r; }
    int port = store::getInt(store::K_HOST_PORT, 8787);

    File f;
    size_t fileBytes = 0;
    if (path.length()) {
        if (!store::sdAcquire()) { r.error = "no card"; return r; }
        f = SD.open(path, FILE_READ);
        if (!f) { r.error = "cannot read " + path; return r; }
        fileBytes = f.size();
        if (fileBytes <= 44) { f.close(); r.error = "recording is empty"; return r; }
    }

    size_t pcmBytes = samples * sizeof(int16_t);
    uint8_t hdr[44];
    audio::wavHeader(hdr, pcmBytes);

    WiFiClient client;
    if (!client.connect(host.c_str(), port, 4000)) {
        if (f) f.close();
        r.error = "host unreachable";
        return r;
    }

    client.printf("POST /transcribe HTTP/1.1\r\nHost: %s\r\n", host.c_str());
    String token = store::getStr("hosttoken", "");
    if (token.length()) {
        client.print("Authorization: Bearer ");
        client.print(token);
        client.print("\r\n");
    }
    client.print("Content-Type: audio/wav\r\n");
    size_t bodyLen = path.length() ? fileBytes : 44 + pcmBytes;
    client.printf("Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)bodyLen);

    if (path.length()) {
        static uint8_t io[2048];
        size_t sent = 0;
        while (sent < fileBytes) {
            int got = f.read(io, sizeof(io));
            if (got <= 0) break;
            if (sent + got > fileBytes) got = fileBytes - sent;
            int off = 0;
            while (off < got) {
                size_t w = client.write(io + off, got - off);
                if (!w) { f.close(); client.stop(); r.error = "upload failed"; return r; }
                off += w;
            }
            sent += got;
        }
        f.close();
        if (sent != fileBytes) { client.stop(); r.error = "short read from card"; return r; }
    } else {
        client.write(hdr, 44);
        const uint8_t* p = (const uint8_t*)pcm;
        size_t off = 0;
        while (off < pcmBytes) {
            size_t n = min((size_t)2048, pcmBytes - off);
            size_t w = client.write(p + off, n);
            if (!w) { client.stop(); r.error = "upload failed"; return r; }
            off += w;
        }
    }

    uint32_t start = millis();
    while (!client.available() && client.connected() && millis() - start < 120000) delay(20);
    String statusLn = client.readStringUntil('\n');
    int sp = statusLn.indexOf(' ');
    int code = sp >= 0 ? statusLn.substring(sp + 1, sp + 4).toInt() : 0;
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

    if (code != 200) { r.error = String("host stt ") + code; return r; }
    JsonDocument d;
    if (!deserializeJson(d, payload) && d["text"].is<const char*>()) {
        r.ok = true;
        r.text = d["text"].as<String>();
        return r;
    }
    payload.trim();
    r.ok = payload.length() > 0;
    r.text = payload;
    if (!r.ok) r.error = "empty transcript";
    return r;
}

Result hostTranscribe(const int16_t* pcm, size_t samples) {
    return hostTranscribeSrc(pcm, samples, String());
}

Result hostTranscribeFile(const String& path) {
    return hostTranscribeSrc(nullptr, 0, path);
}

Result code(const String& prompt, const String& project, const String& backend) {
    Result r;
    if (!hostOnline()) { r.error = "agent mode needs the Mac daemon"; return r; }
    JsonDocument req;
    req["prompt"] = prompt;
    req["project"] = project;
    if (backend.length()) req["backend"] = backend;
    String body; serializeJson(req, body);
    return hostPost("/code", body, 600000);
}

Result vaultWrite(const String& path, const String& content, bool append) {
    Result r;
    if (!hostOnline()) { r.error = "vault sync needs the Mac daemon"; return r; }
    JsonDocument req;
    req["path"] = path;
    req["content"] = content;
    req["append"] = append;
    String body; serializeJson(req, body);
    return hostPost("/vault/note", body, 20000);
}

Result vaultRead(const String& path) {
    Result r;
    if (!hostOnline()) { r.error = "vault needs the Mac daemon"; return r; }
    JsonDocument req;
    req["path"] = path;
    String body; serializeJson(req, body);
    return hostPost("/vault/read", body, 20000);
}

Result vaultDailyAppend(const String& content) {
    Result r;
    if (!hostOnline()) { r.error = "vault sync needs the Mac daemon"; return r; }
    JsonDocument req;
    req["content"] = content;
    String body; serializeJson(req, body);
    return hostPost("/vault/daily", body, 20000);
}

std::vector<String> vaultList(const String& dir) {
    std::vector<String> out;
    if (!hostOnline()) return out;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(20000);
    if (!http.begin(hostBase() + "/vault/list?dir=" + dir)) return out;
    String token = store::getStr("hosttoken", "");
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    if (http.GET() == 200) {
        JsonDocument d;
        if (!deserializeJson(d, http.getString()))
            for (JsonVariant v : d["files"].as<JsonArray>()) out.push_back(v.as<String>());
    }
    http.end();
    return out;
}

}  // namespace cloud
