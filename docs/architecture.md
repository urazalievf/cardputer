# Architecture

## Layers

```
apps/     notes voice ask code wifi bluetooth files settings
          ────────────────────────────────────────────────
kernel/   os      app registry, nav stack, event loop, global keys
          ui      canvas rendering, widgets, modals
          theme   palettes, accent, density — all user-editable, all in NVS
          ai      provider routing: 7 backends behind one chat()/transcribe()
          cloud   the Mac daemon: agent CLIs, whisper, Obsidian vault
          net     scan / join / autojoin / NTP
          bt      BLE scanner and HID keyboard, started on demand
          store   NVS config + SD filesystem, GPIO40 arbitration
          audio   chunked capture, lazily allocated buffer, WAV framing
```

## The loop

```
loop()
 └─ os::run()
     ├─ M5Cardputer.update()          poll keyboard, battery, buttons
     ├─ net::tick()                   reconnect if dropped; NTP once online
     ├─ translate(keysState) → KeyEvent
     │    └─ esc → os::home(), else app->onKey(k)
     ├─ app->tick()                   app-driven work (audio chunks, timers)
     └─ if dirty:  clear → app->draw() → statusBar
```

Drawing is pull-based and lazy. Nothing repaints until something calls
`os::invalidate()`, so an idle screen costs one keyboard poll per 15 ms.

Long operations paint their own intermediate frame before blocking:

```cpp
mode_ = BUSY;
ui::clear(); draw(); ui::statusBar(title().c_str());   // show "Thinking..."
auto r = cloud::ask(prompt);                            // now block
```

`Voice` avoids blocking entirely — `tick()` grabs one 100 ms chunk per pass, so
the level meter stays live during a five-second recording.

## Writing an app

```cpp
#include "apps.h"
#include "../kernel/ui.h"

class Hello : public App {
public:
    const char* name()  const override { return "Hello"; }
    const char* blurb() const override { return "demo"; }

    void onKey(const KeyEvent& k) override {
        if (k.enter) { n_++; os::invalidate(); }
    }
    void draw() override {
        ui::centered(55, "pressed " + String(n_) + " times", ui::ACCENT);
        ui::hint("Enter count   ` back");
    }
private:
    int n_ = 0;
};

App* helloApp() { static Hello a; return &a; }
```

Declare it in `apps/apps.h`, register it in `main.cpp`. Position in the
registry is its number-key shortcut on the launcher.

Overridables worth knowing:

- `title()` — status-bar text; default is `name()`. Use it for live counts.
- `onBack()` — return `true` if you consumed the back key (closed a sub-view,
  cleared a field). Return `false` and the kernel pops the nav stack for you.
- `tick()` — called every pass whether or not the screen is dirty.

## Memory arbitration

Three things want the same internal RAM on a board with no PSRAM:

| | cost | when |
|---|---|---|
| UI canvas | 64 KB | always, if it fits |
| audio buffer | up to 137 KB | only while recording |
| BLE stack | ~60 KB | only while the Bluetooth app is open |
| TLS handshake | ~45 KB | during any cloud call |

None of them is permanent. `Voice::start()` calls `ui::releaseCanvas()` before
`audio::recordStart()` and re-acquires it after transcription; `ui` tells `audio`
how much it is holding via `setReclaimableBytes()` so the capacity estimate
reflects what will be available, not what is free right now. That is why the mic
reports 4 seconds while only 145 KB is free.

## Provider routing, with fallback

`ai::chat()` and `ai::transcribe()` follow the same shape:

```
preferred provider  ──ok──►  done
      │
   failed / not configured
      │
      └── autoFallback? ──► next configured provider ──► ... ──► original error
```

Each provider contributes a request builder and a response extractor;
`postJson()` is shared. Anthropic, Gemini and Ollama have their own shapes;
OpenAI, Groq and OpenRouter all speak chat-completions, so they share one
builder. Adding a vendor is a row in `SPECS[]` and, usually, no new code.

`cloud::hostOnline()` caches a `/ping` for 8 seconds, so a dead Mac costs one
timeout per 8 s rather than one per request.

`cloud::code()` is the exception: no fallback, because an agent without a
filesystem isn't an agent.

## Storage

| What | Where | Why |
|---|---|---|
| Config, API keys | NVS `cfg` | survives reflash, never in git |
| WiFi networks | NVS `cfg/wifinets`, one JSON array | SSIDs exceed the 15-char NVS key limit |
| Chat history | NVS `chat/hist` | small, wants to survive reboots |
| Theme + toggles | NVS `cfg/th*` | the look survives a reflash |
| Notes | SD `/notes/*.md` | real markdown, Obsidian-ready |
| Notes (no SD) | NVS `notes` | degraded but doesn't lose thoughts mid-trip |

Note filenames are `YYYYMMDD-HHMMSS-slug.md`, so a reverse lexicographic sort is
newest-first and no index is needed.
