// Provider-agnostic language model and speech-to-text access.
//
// Nothing above this layer knows which vendor answered. Each provider gets a
// request builder and a response extractor; routing tries your preferred
// provider, then falls back through whatever else is configured.
#pragma once
#include "os.h"
#include <vector>

namespace ai {

enum class Provider : uint8_t {
    Host = 0,     // the Mac daemon: claude/codex CLI on your subscription
    Anthropic,    // Claude
    OpenAI,       // ChatGPT
    Gemini,       // Google
    Groq,         // fast open models, OpenAI-shaped
    OpenRouter,   // anything, OpenAI-shaped
    Ollama,       // local model on your LAN, no key, no cost
    COUNT
};

struct Spec {
    const char* id;
    const char* label;
    const char* keyKey;      // NVS key for the API key; nullptr if none needed
    const char* modelKey;    // NVS key for the model override
    const char* defModel;
    const char* host;        // nullptr when the host is user-configured
    const char* path;
    bool  tls;
    bool  needsKey;
};

const Spec& spec(Provider p);
const char* label(Provider p);

// What to actually do about an unconfigured provider, short enough for one
// line on a 240px screen. "not configured" on its own has never helped anyone.
const char* setupHint(Provider p);

struct Msg { String role; String content; };   // role: "user" | "assistant"

struct Result {
    bool ok = false;
    String text;
    Provider used = Provider::Host;
    String error;
    uint32_t ms = 0;
    const char* usedLabel() const { return label(used); }
};

void begin();

Provider preferred();
void setPreferred(Provider p);
bool configured(Provider p);                  // has a key / host / daemon
std::vector<Provider> available();
String model(Provider p);
void   setModel(Provider p, const String& m);
bool   autoFallback();
void   setAutoFallback(bool v);

// Chat. `msgs` is the conversation in order; the last entry should be the user.
Result chat(const std::vector<Msg>& msgs, const String& system, int maxTokens = 400);
Result chatWith(Provider p, const std::vector<Msg>& msgs, const String& system,
                int maxTokens = 400);
Result ask(const String& prompt, const String& system = "", int maxTokens = 400);

// Speech to text. Same idea: daemon first, then whichever cloud key exists.
enum class Stt : uint8_t { Host = 0, OpenAI, Groq, COUNT };
const char* sttLabel(Stt s);
Stt  preferredStt();
void setPreferredStt(Stt s);
bool sttConfigured(Stt s);
const char* sttSetupHint(Stt s);
Result transcribe(const int16_t* pcm, size_t samples);

}  // namespace ai
