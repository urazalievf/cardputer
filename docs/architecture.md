# Architecture

## Layers

```
apps/     notes voice ask code translate tasks calc timer weather
          files settings   (+ wifi, bluetooth: hidden, opened from settings)
          ────────────────────────────────────────────────
kernel/   os        app registry, nav stack, event loop, global keys
          ui        canvas rendering, widgets, modals, await()
          theme     palettes, per-role colours, density — user-editable, in NVS
          ai        provider routing: 7 backends behind one chat()/transcribe()
          cloud     the Mac daemon: agent CLIs, whisper, Obsidian vault
          net       scan / join / autojoin / NTP
          bt        BLE scanner and HID keyboard, started on demand
          hw        RGB LED, Grove I2C, infrared transmit
          store     NVS config + SD filesystem, GPIO40 arbitration, mount backoff
          audio     chunked capture, lazy buffer, waveform envelope, WAV framing
          expr      the arithmetic parser, split out so it can be tested
          selftest  201 on-device checks (built only into cardputer-selftest)
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

## Long operations

A blocking network call on the UI core paints one frozen frame and looks exactly
like a crash. `ui::await()` runs the work on core 0 and animates until it
finishes:

```cpp
ai::Result r;
ui::await("Asking Claude", [&] { r = ai::chat(convo, SYSTEM, 400); });
```

The worker gets a 16KB stack, because a TLS handshake alone wants 8–10KB. If the
task cannot be created it falls back to running inline, so the call never fails
just because memory is tight. Every AI, transcription, vault, weather, mDNS and
WiFi-join call site goes through it.

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
- `hidden()` — return `true` to stay off the launcher grid while remaining
  reachable via `os::launchByName()`. Connectivity uses this.

## Keys

The Cardputer prints arrow glyphs on the `;` `.` `,` `/` keycaps. `os::readKey()`
sets `k.up/down/left/right` from those keys directly — that is what every other
firmware on this hardware does, and requiring `fn` makes the arrows feel dead.
The character is *also* kept in `k.chars` unless `fn` was held, so:

- navigation screens read the flags and work with a bare keypress
- text screens read `k.chars` first and type the character
- `fn` + the key is unambiguously an arrow, for text screens that need both

Two gotchas live in the M5 library. `isChange()` compares only the *number* of
keys held, not which ones. And with `ctrl` or `shift` down it substitutes
`value_second`, so `ctrl+1` arrives as `!` — `unshiftDigit()` maps the top row
back.
- `tick()` — called every pass whether or not the screen is dirty.

## Memory arbitration

Three things want the same internal RAM on a board with no PSRAM:

| | cost | when |
|---|---|---|
| UI canvas | 64 KB | always, if it fits |
| audio buffer | up to 200 KB | only while recording |
| BLE stack | ~60 KB | only while the Bluetooth app is open |
| TLS handshake | ~45 KB | during any cloud call |
| await() worker | 16 KB | for the duration of one network call |

None of them is permanent. `Voice::start()` calls `ui::releaseCanvas()` before
`audio::recordStart()` and re-acquires it after transcription; `ui` tells `audio`
how much it is holding via `setReclaimableBytes()` so the *advertised* capacity
reflects what will be available, not what is free right now. That is why the mic
reports 4 seconds while only 141 KB is free.

Advertising and allocating are deliberately different calculations, and
conflating them was a real bug. `capacitySamples()` may count memory the caller
is expected to hand back; `allocBuffer()` may not. It sizes from
`heap_caps_get_largest_free_block()` — the total being sufficient means nothing
if it is fragmented — and backs off in 25% steps rather than failing outright,
because a shorter memo beats no memo.

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

A failed SD mount costs two `SD.begin()` attempts, roughly 200ms. Without a
backoff every `listNotes()` on a card-less device pays that, which reads as lag
rather than as a missing card. `sdAcquire()` refuses to retry for 3 seconds
after a failure; remounts driven from the UI pass `force=true` to skip it.

## Testing

`kernel/selftest.cpp` is compiled only into the `cardputer-selftest` environment,
runs from `setup()` after waiting up to 4s for a serial monitor to attach, and
prints one line per check. It exists because the interesting failures on this
device are not compile errors — they are a parser that mishandles `2^3^2`, an
allocation that only fails when the canvas is up, or an interrupt mask that
hangs the board. Those need to run on real hardware to show up.

The arithmetic parser lives in `kernel/expr` rather than inside the Calc app
specifically so the suite can drive it directly.
