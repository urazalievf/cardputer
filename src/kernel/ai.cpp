#include "ai.h"
#include "store.h"
#include "net.h"
#include "cloud.h"
#include "audio.h"
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace ai {

static const Spec SPECS[] = {
    // id            label        keyKey        modelKey      defModel
    {"host",        "Mac daemon", nullptr,      "m_host",     "claude",
     nullptr, "/ask", false, false},
    {"anthropic",   "Claude",     "k_anthropic","m_anthropic","claude-haiku-4-5-20251001",
     "api.anthropic.com", "/v1/messages", true, true},
    {"openai",      "ChatGPT",    "k_openai",   "m_openai",   "gpt-4o-mini",
     "api.openai.com", "/v1/chat/completions", true, true},
    {"gemini",      "Gemini",     "k_gemini",   "m_gemini",   "gemini-2.0-flash",
     "generativelanguage.googleapis.com", "/v1beta/models", true, true},
    {"groq",        "Groq",       "k_groq",     "m_groq",     "llama-3.3-70b-versatile",
     "api.groq.com", "/openai/v1/chat/completions", true, true},
    {"openrouter",  "OpenRouter", "k_openrtr",  "m_openrtr",  "anthropic/claude-3.5-haiku",
     "openrouter.ai", "/api/v1/chat/completions", true, true},
    {"ollama",      "Ollama",     nullptr,      "m_ollama",   "llama3.2",
     nullptr, "/api/chat", false, false},
};

const Spec& spec(Provider p) {
    int i = (int)p;
    if (i < 0 || i >= (int)Provider::COUNT) i = 0;
    return SPECS[i];
}
const char* label(Provider p) { return spec(p).label; }

const char* setupHint(Provider p) {
    switch (p) {
        case Provider::Host:
            // Reachable but refusing us means the shared secret is missing:
            // the daemon prints it at startup and it has to be pushed over USB.
            return cloud::hostReachable() && !cloud::hostAuthorised()
                 ? "daemon needs its token - Settings > Mac"
                 : "start cardputerd, then Settings > Find Mac";
        case Provider::Ollama: return "set the Ollama host in Settings";
        default:               return "add an API key in Settings > AI";
    }
}

void begin() {}

Provider preferred() {
    int v = store::getInt("aiprov", (int)Provider::Host);
    if (v < 0 || v >= (int)Provider::COUNT) v = 0;
    return (Provider)v;
}
void setPreferred(Provider p) { store::setInt("aiprov", (int)p); }

bool autoFallback() { return store::getInt("aifall", 1) != 0; }
void setAutoFallback(bool v) { store::setInt("aifall", v ? 1 : 0); }

String model(Provider p) {
    const Spec& s = spec(p);
    return store::getStr(s.modelKey, s.defModel);
}
void setModel(Provider p, const String& m) { store::setStr(spec(p).modelKey, m); }

static String apiKey(Provider p) {
    const Spec& s = spec(p);
    return s.keyKey ? store::getStr(s.keyKey, "") : String("");
}

static String ollamaHost() { return store::getStr("ollamahost", ""); }

bool configured(Provider p) {
    switch (p) {
        case Provider::Host:   return cloud::hostOnline();
        case Provider::Ollama: return ollamaHost().length() > 0;
        default:               return apiKey(p).length() > 0;
    }
}

std::vector<Provider> available() {
    std::vector<Provider> out;
    for (int i = 0; i < (int)Provider::COUNT; i++)
        if (configured((Provider)i)) out.push_back((Provider)i);
    return out;
}

// ---------------- transport ----------------
struct HttpResp { int code = 0; String body; String err; };

static HttpResp postJson(const String& url, const std::vector<String>& headers,
                         const String& body, bool tls, uint32_t timeoutMs) {
    HttpResp r;
    if (!net::connected()) { r.err = "wifi off"; return r; }

    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(timeoutMs);
    http.setReuse(false);

    bool begun;
    WiFiClientSecure secure;
    if (tls) {
        // No CA bundle fits alongside everything else in 8MB; see docs/roadmap.
        secure.setInsecure();
        secure.setHandshakeTimeout(20);
        begun = http.begin(secure, url);
    } else {
        begun = http.begin(url);
    }
    if (!begun) { r.err = "connect failed"; return r; }

    http.addHeader("Content-Type", "application/json");
    for (size_t i = 0; i + 1 < headers.size(); i += 2)
        http.addHeader(headers[i], headers[i + 1]);

    r.code = http.POST(body);
    if (r.code <= 0) {
        r.err = String("net: ") + http.errorToString(r.code);
        http.end();
        return r;
    }
    r.body = http.getString();
    http.end();
    return r;
}

// Pull an error message out of whatever shape the vendor returned.
static String errorFrom(const HttpResp& hr) {
    if (hr.err.length()) return hr.err;
    JsonDocument d;
    if (!deserializeJson(d, hr.body)) {
        if (d["error"]["message"].is<const char*>()) return d["error"]["message"].as<String>();
        if (d["error"].is<const char*>())            return d["error"].as<String>();
        if (d["message"].is<const char*>())          return d["message"].as<String>();
    }
    String snippet = hr.body.substring(0, 90);
    snippet.replace('\n', ' ');
    return String("HTTP ") + hr.code + (snippet.length() ? ": " + snippet : "");
}

// ---------------- per-provider request shapes ----------------
static Result viaAnthropic(const std::vector<Msg>& msgs, const String& system, int maxTok) {
    Result r; r.used = Provider::Anthropic;
    const Spec& s = spec(Provider::Anthropic);
    String key = apiKey(Provider::Anthropic);
    if (!key.length()) { r.error = "no Claude key"; return r; }

    JsonDocument req;
    req["model"] = model(Provider::Anthropic);
    req["max_tokens"] = maxTok;
    if (system.length()) req["system"] = system;
    JsonArray arr = req["messages"].to<JsonArray>();
    for (auto& m : msgs) {
        JsonObject o = arr.add<JsonObject>();
        o["role"] = m.role;
        o["content"] = m.content;
    }
    String body; serializeJson(req, body);

    auto hr = postJson(String("https://") + s.host + s.path,
                       {"x-api-key", key, "anthropic-version", "2023-06-01"},
                       body, true, 45000);
    if (hr.code != 200) { r.error = errorFrom(hr); return r; }
    JsonDocument d;
    if (deserializeJson(d, hr.body)) { r.error = "bad json"; return r; }
    r.text = d["content"][0]["text"].as<String>();
    r.ok = r.text.length() > 0;
    if (!r.ok) r.error = "empty reply";
    return r;
}

// OpenAI, Groq and OpenRouter all speak the same chat-completions shape.
static Result viaOpenAiShaped(Provider p, const std::vector<Msg>& msgs,
                              const String& system, int maxTok) {
    Result r; r.used = p;
    const Spec& s = spec(p);
    String key = apiKey(p);
    if (!key.length()) { r.error = String("no ") + s.label + " key"; return r; }

    JsonDocument req;
    req["model"] = model(p);
    req["max_tokens"] = maxTok;
    JsonArray arr = req["messages"].to<JsonArray>();
    if (system.length()) {
        JsonObject o = arr.add<JsonObject>();
        o["role"] = "system";
        o["content"] = system;
    }
    for (auto& m : msgs) {
        JsonObject o = arr.add<JsonObject>();
        o["role"] = m.role;
        o["content"] = m.content;
    }
    String body; serializeJson(req, body);

    std::vector<String> headers = {"Authorization", String("Bearer ") + key};
    if (p == Provider::OpenRouter) {
        headers.push_back("HTTP-Referer");
        headers.push_back("https://github.com/urazalievf/cardputer");
        headers.push_back("X-Title");
        headers.push_back("CardputerOS");
    }

    auto hr = postJson(String("https://") + s.host + s.path, headers, body, true, 45000);
    if (hr.code != 200) { r.error = errorFrom(hr); return r; }
    JsonDocument d;
    if (deserializeJson(d, hr.body)) { r.error = "bad json"; return r; }
    r.text = d["choices"][0]["message"]["content"].as<String>();
    r.ok = r.text.length() > 0;
    if (!r.ok) r.error = "empty reply";
    return r;
}

static Result viaGemini(const std::vector<Msg>& msgs, const String& system, int maxTok) {
    Result r; r.used = Provider::Gemini;
    const Spec& s = spec(Provider::Gemini);
    String key = apiKey(Provider::Gemini);
    if (!key.length()) { r.error = "no Gemini key"; return r; }

    JsonDocument req;
    if (system.length())
        req["system_instruction"]["parts"][0]["text"] = system;
    JsonArray contents = req["contents"].to<JsonArray>();
    for (auto& m : msgs) {
        JsonObject o = contents.add<JsonObject>();
        o["role"] = (m.role == "assistant") ? "model" : "user";
        o["parts"][0]["text"] = m.content;
    }
    req["generationConfig"]["maxOutputTokens"] = maxTok;
    String body; serializeJson(req, body);

    String url = String("https://") + s.host + s.path + "/" + model(Provider::Gemini) +
                 ":generateContent?key=" + key;
    auto hr = postJson(url, {}, body, true, 45000);
    if (hr.code != 200) { r.error = errorFrom(hr); return r; }
    JsonDocument d;
    if (deserializeJson(d, hr.body)) { r.error = "bad json"; return r; }
    r.text = d["candidates"][0]["content"]["parts"][0]["text"].as<String>();
    r.ok = r.text.length() > 0;
    if (!r.ok) r.error = "empty reply";
    return r;
}

static Result viaOllama(const std::vector<Msg>& msgs, const String& system, int maxTok) {
    Result r; r.used = Provider::Ollama;
    String hostPort = ollamaHost();
    if (!hostPort.length()) { r.error = "no Ollama host"; return r; }
    if (hostPort.indexOf(':') < 0) hostPort += ":11434";

    JsonDocument req;
    req["model"] = model(Provider::Ollama);
    req["stream"] = false;
    req["options"]["num_predict"] = maxTok;
    JsonArray arr = req["messages"].to<JsonArray>();
    if (system.length()) {
        JsonObject o = arr.add<JsonObject>();
        o["role"] = "system";
        o["content"] = system;
    }
    for (auto& m : msgs) {
        JsonObject o = arr.add<JsonObject>();
        o["role"] = m.role;
        o["content"] = m.content;
    }
    String body; serializeJson(req, body);

    // Plain HTTP on the LAN: no TLS heap cost, and a local model costs nothing.
    auto hr = postJson("http://" + hostPort + "/api/chat", {}, body, false, 180000);
    if (hr.code != 200) { r.error = errorFrom(hr); return r; }
    JsonDocument d;
    if (deserializeJson(d, hr.body)) { r.error = "bad json"; return r; }
    r.text = d["message"]["content"].as<String>();
    r.ok = r.text.length() > 0;
    if (!r.ok) r.error = "empty reply";
    return r;
}

static Result viaHost(const std::vector<Msg>& msgs, const String& system, int maxTok) {
    Result r; r.used = Provider::Host;
    if (!cloud::hostOnline()) { r.error = "daemon offline"; return r; }

    JsonDocument req;
    req["system"] = system;
    req["max_tokens"] = maxTok;
    req["backend"] = model(Provider::Host);      // "claude", "codex", ...
    JsonArray arr = req["messages"].to<JsonArray>();
    for (auto& m : msgs) {
        JsonObject o = arr.add<JsonObject>();
        o["role"] = m.role;
        o["content"] = m.content;
    }
    // Older daemons only understand a flat prompt; send both.
    req["prompt"] = msgs.empty() ? String("") : msgs.back().content;
    String body; serializeJson(req, body);

    auto res = cloud::hostPost("/ask", body, 180000);
    r.ok = res.ok;
    r.text = res.text;
    r.error = res.error;
    return r;
}

Result chatWith(Provider p, const std::vector<Msg>& msgs, const String& system, int maxTok) {
    uint32_t t0 = millis();
    Result r;
    switch (p) {
        case Provider::Host:       r = viaHost(msgs, system, maxTok); break;
        case Provider::Anthropic:  r = viaAnthropic(msgs, system, maxTok); break;
        case Provider::Gemini:     r = viaGemini(msgs, system, maxTok); break;
        case Provider::Ollama:     r = viaOllama(msgs, system, maxTok); break;
        default:                   r = viaOpenAiShaped(p, msgs, system, maxTok); break;
    }
    r.ms = millis() - t0;
    os::logf("ai %s: %s (%lums)", spec(p).label,
             r.ok ? String(String(r.text.length()) + " chars").c_str() : r.error.c_str(),
             (unsigned long)r.ms);
    return r;
}

Result chat(const std::vector<Msg>& msgs, const String& system, int maxTok) {
    Provider first = preferred();
    Result r = chatWith(first, msgs, system, maxTok);
    if (r.ok || !autoFallback()) return r;

    // The preferred provider is down or unconfigured. Try the others in the
    // declared order rather than failing in the user's face.
    for (int i = 0; i < (int)Provider::COUNT; i++) {
        Provider p = (Provider)i;
        if (p == first || !configured(p)) continue;
        Result alt = chatWith(p, msgs, system, maxTok);
        if (alt.ok) return alt;
    }
    return r;
}

Result ask(const String& prompt, const String& system, int maxTok) {
    std::vector<Msg> msgs;
    msgs.push_back({"user", prompt});
    return chat(msgs, system, maxTok);
}

// ---------------- speech to text ----------------
const char* sttLabel(Stt s) {
    switch (s) {
        case Stt::Host:   return "Mac (local whisper)";
        case Stt::OpenAI: return "OpenAI Whisper";
        case Stt::Groq:   return "Groq Whisper";
        default:          return "?";
    }
}

const char* sttSetupHint(Stt s) {
    switch (s) {
        case Stt::Host:
            return cloud::hostReachable() && !cloud::hostAuthorised()
                 ? "daemon needs its token - Settings > Mac"
                 : "start cardputerd, then Settings > Find Mac";
        case Stt::OpenAI: return "add an OpenAI key in Settings > AI";
        case Stt::Groq:   return "add a Groq key in Settings > AI";
        default:          return "pick an engine in Settings";
    }
}

Stt preferredStt() {
    int v = store::getInt("aistt", 0);
    if (v < 0 || v >= (int)Stt::COUNT) v = 0;
    return (Stt)v;
}
void setPreferredStt(Stt s) { store::setInt("aistt", (int)s); }

bool sttConfigured(Stt s) {
    switch (s) {
        case Stt::Host:   return cloud::hostOnline();
        case Stt::OpenAI: return store::getStr("k_openai", "").length() > 0;
        case Stt::Groq:   return store::getStr("k_groq", "").length() > 0;
        default:          return false;
    }
}

// Multipart upload streamed by hand: the whole body would never fit in RAM.
// The audio is either the live capture buffer (which needs a WAV header put in
// front of it) or a file already on the card that has one -- a streamed memo is
// bigger than the heap, so it never passes through RAM in one piece.
static Result cloudWhisper(Stt which, const int16_t* pcm, size_t samples,
                           const String& wavPath = String()) {
    Result r;
    const bool groq = which == Stt::Groq;
    const char* host = groq ? "api.groq.com" : "api.openai.com";
    const char* path = groq ? "/openai/v1/audio/transcriptions" : "/v1/audio/transcriptions";
    const char* mdl  = groq ? "whisper-large-v3-turbo" : "whisper-1";
    String key = store::getStr(groq ? "k_groq" : "k_openai", "");
    if (!key.length()) { r.error = String("no ") + (groq ? "Groq" : "OpenAI") + " key"; return r; }
    if (!net::connected()) { r.error = "wifi off"; return r; }

    File f;
    size_t fileBytes = 0;
    if (wavPath.length()) {
        if (!store::sdAcquire()) { r.error = "no card"; return r; }
        f = SD.open(wavPath, FILE_READ);
        if (!f) { r.error = "cannot read " + wavPath; return r; }
        fileBytes = f.size();
        if (fileBytes <= 44) { f.close(); r.error = "recording is empty"; return r; }
    }

    size_t pcmBytes = samples * sizeof(int16_t);
    const char* boundary = "----CardputerOSBoundary7MA4YWxkTrZ";
    String pre = String("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" + mdl + "\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"r.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String tail = String("\r\n--") + boundary + "--\r\n";

    uint8_t hdr[44];
    audio::wavHeader(hdr, pcmBytes);
    size_t audioLen = wavPath.length() ? fileBytes : 44 + pcmBytes;
    size_t bodyLen = pre.length() + audioLen + tail.length();

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(20);
    client.setTimeout(60);
    if (!client.connect(host, 443)) {
        if (f) f.close();
        r.error = "tls connect failed";
        return r;
    }

    client.printf("POST %s HTTP/1.1\r\nHost: %s\r\n", path, host);
    client.print("Authorization: Bearer "); client.print(key); client.print("\r\n");
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
    client.printf("Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)bodyLen);
    client.print(pre);

    if (wavPath.length()) {
        // Straight off the card, 2KB at a time: the header is already the first
        // 44 bytes of the file.
        // Heap, not .bss: this lives only for the upload, and a permanent
        // buffer is permanently unavailable to the capture ring.
        uint8_t* io = (uint8_t*)malloc(2048);
        if (!io) { f.close(); client.stop(); r.error = "no room to upload"; return r; }
        size_t sent = 0;
        while (sent < fileBytes) {
            int got = f.read(io, 2048);
            if (got <= 0) break;
            if (sent + got > fileBytes) got = fileBytes - sent;
            int off = 0;
            while (off < got) {
                size_t w = client.write(io + off, got - off);
                if (!w) { free(io); f.close(); client.stop(); r.error = "upload failed"; return r; }
                off += w;
            }
            sent += got;
        }
        free(io);
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
    client.print(tail);

    uint32_t start = millis();
    while (!client.available() && client.connected() && millis() - start < 90000) delay(20);
    String statusLn = client.readStringUntil('\n');
    int sp = statusLn.indexOf(' ');
    int code = sp >= 0 ? statusLn.substring(sp + 1, sp + 4).toInt() : 0;
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

    if (code != 200) { r.error = String("stt ") + code + ": " + payload.substring(0, 100); return r; }
    r.ok = payload.length() > 0;
    r.text = payload;
    r.used = groq ? Provider::Groq : Provider::OpenAI;
    if (!r.ok) r.error = "empty transcript";
    return r;
}

static Result transcribeSrc(const int16_t* pcm, size_t samples, const String& path) {
    Result r;
    if (!path.length() && samples == 0) { r.error = "nothing recorded"; return r; }

    Stt first = preferredStt();
    auto attempt = [&](Stt s) -> Result {
        if (s == Stt::Host) {
            auto hostRes = path.length() ? cloud::hostTranscribeFile(path)
                                         : cloud::hostTranscribe(pcm, samples);
            Result out;
            out.ok = hostRes.ok;
            out.text = hostRes.text;
            out.error = hostRes.error;
            out.used = Provider::Host;
            return out;
        }
        return cloudWhisper(s, pcm, samples, path);
    };

    if (sttConfigured(first)) {
        r = attempt(first);
        if (r.ok) return r;
    }
    for (int i = 0; i < (int)Stt::COUNT; i++) {
        Stt s = (Stt)i;
        if (s == first || !sttConfigured(s)) continue;
        Result alt = attempt(s);
        if (alt.ok) return alt;
    }
    if (!r.error.length()) r.error = "no speech-to-text configured";
    return r;
}

Result transcribe(const int16_t* pcm, size_t samples) {
    return transcribeSrc(pcm, samples, String());
}

Result transcribeFile(const String& path) {
    return transcribeSrc(nullptr, 0, path);
}

}  // namespace ai
