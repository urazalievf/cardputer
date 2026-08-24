# Architecture

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
- `escExits()` — return `false` while a text field is focused, so `` ` `` reaches
  your `onKey` instead of bouncing to the launcher.
- `tick()` — called every pass whether or not the screen is dirty.

## Host-first, with fallback

`cloud::ask()` and `cloud::transcribe()` both follow the same shape:

```
hostOnline()?  ──yes──►  POST to the daemon  ──ok──►  done
      │                         │
      no                     failed
      │                         │
      └─────────────►  direct API call with the NVS key
```

`hostOnline()` caches a `/ping` for 8 seconds, so a dead Mac costs one 1.2 s
timeout per 8 s rather than one per request. A host that answers but errors
still falls through to the API — a broken daemon shouldn't be worse than no
daemon.

`cloud::code()` is the exception: no fallback, because Claude Code without a
filesystem isn't Claude Code.

## Storage

| What | Where | Why |
|---|---|---|
| Config, API keys | NVS `cfg` | survives reflash, never in git |
| WiFi networks | NVS `cfg/wifinets`, one JSON array | SSIDs exceed the 15-char NVS key limit |
| Chat history | NVS `chat/hist` | small, wants to survive reboots |
| Notes | SD `/notes/*.md` | real markdown, Obsidian-ready |
| Notes (no SD) | NVS `notes` | degraded but doesn't lose thoughts mid-trip |

Note filenames are `YYYYMMDD-HHMMSS-slug.md`, so a reverse lexicographic sort is
newest-first and no index is needed.
