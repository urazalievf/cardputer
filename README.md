# CardputerOS

A small app OS for the **M5Stack Cardputer** (StampS3 / ESP32-S3, 8&nbsp;MB flash, no PSRAM).

Notes, voice memos with speech-to-text, a chat assistant you can point at any
vendor, a remote coding agent, an Obsidian bridge, a WiFi manager and BLE — on
a device that fits in a pocket.

```
┌──────────────────────────────────────────┐
│ ⌂ CardputerOS 0.2.0     14:32 ▁▃▅▇ ⌁ 82% │
├──────────────────────────────────────────┤
│  ┌──────┐┌──────┐┌──────┐┌──────┐        │
│  │1  ▤  ││2  ⏺  ││3  ▭  ││4  ‹› │        │
│  │Notes ││Voice ││ Ask  ││ Code │        │
│  └──────┘└──────┘└──────┘└──────┘        │
│  ┌──────┐┌──────┐┌──────┐┌──────┐        │
│  │5  ≋  ││6  ᛒ  ││7  ▢  ││8  ⚙  │        │
│  │ WiFi ││  BT  ││Files ││ Set  │        │
│  └──────┘└──────┘└──────┘└──────┘        │
├──────────────────────────────────────────┤
│ home-wifi  mac  sd  Claude               │
└──────────────────────────────────────────┘
```

## Apps

| | |
|---|---|
| **Notes** | Markdown on the SD card — real `.md` files, so the card drops into an Obsidian vault. `S` pushes one over WiFi, `A` asks your assistant about the note you're reading. |
| **Voice** | `TAB` records with a live level meter, then transcribes. Save as a note, append to today's Obsidian daily note, or send to the assistant. |
| **Ask** | Conversation with persistent history. `TAB` switches vendor mid-thread, `ctrl+D` dictates instead of typing on 56 tiny keys. |
| **Code** | A real coding agent — `claude`, `codex` or `gemini` CLI — with tools, in a project directory on your Mac. |
| **WiFi** | Scan any network, join, remembered. Rejoins the strongest known one on boot. |
| **Bluetooth** | Scan nearby BLE devices, or become a keyboard: what you type goes to a paired Mac, iPad or phone. |
| **Files** | SD browser with a text pager. |
| **Settings** | Themes, accent colour, brightness, every provider and key, models, daemon, vault, timezone. |

## Any assistant, not just one

Seven backends behind one interface. Nothing above `kernel/ai.cpp` knows which
one answered.

| Provider | Needs | Notes |
|---|---|---|
| **Mac daemon** | the companion running | `claude` / `codex` / `gemini` CLI on your existing subscription — no per-token cost |
| **Claude** | API key | `api.anthropic.com` |
| **ChatGPT** | API key | `api.openai.com` |
| **Gemini** | API key | Google AI |
| **Groq** | API key | fast open models |
| **OpenRouter** | API key | anything, one key |
| **Ollama** | a LAN address | local model, plain HTTP, free, no TLS heap cost |

Pick a default in **Settings → Provider**, or `TAB` inside Ask to switch for one
question. With **Auto fallback** on, a provider that is down or unconfigured
falls through to the next one that is ready instead of failing at you.

Speech-to-text routes the same way: local whisper on the Mac, then OpenAI or
Groq.

## Make it yours

Settings is the point, not an afterthought.

- **7 themes** — Midnight, Terminal, Amber, Nord, Synthwave, Paper, Mono
- **Accent colour** — a live hue picker with the real UI drawn underneath it
- **Brightness**, **big text**, **hints on/off**, **status clock**, **sounds**
- Every API key, every model string, the daemon address, the Bluetooth name,
  the vault folder and the timezone
- **Factory reset** that wipes settings and keys but leaves your notes alone

`ctrl+T` cycles themes from anywhere.

## Getting around

| | |
|---|---|
| `` ` `` | back — the app closes its sub-view first, then the nav stack, then home |
| `1`–`8` | launch an app from home |
| `ctrl`+`1`–`8` | launch from anywhere |
| `ctrl+H` | home |
| `ctrl+T` | next theme |
| `fn` + `; . , /` | arrows (the glyphs on those keycaps) |
| `ctrl+Backspace` | delete a word |

## Architecture

```
       ┌──────────── Cardputer ────────────┐      ┌──────── your Mac ────────┐
       │ apps/  notes voice ask code wifi  │      │  cardputerd.py           │
       │        bluetooth files settings   │      │    ├─ claude / codex /   │
       │ ────────────────────────────────  │ WiFi │    │  gemini CLI         │
       │ kernel/ os    ui    theme   ai    │◄────►│    ├─ whisper.cpp /      │
       │         net   bt    store  audio  │      │    │  faster-whisper     │
       │         cloud                     │      │    └─ Obsidian vault     │
       │ ────────────────────────────────  │      └──────────────────────────┘
       │ M5Unified / Arduino / ESP-IDF 5.5 │
       └───────────────────────────────────┘
                        └── falls back to Anthropic / OpenAI / Google / Groq /
                            OpenRouter directly when the Mac isn't reachable
```

Rendering goes through a 64KB off-screen sprite pushed in one DMA transfer, so
frames never tear. Drawing is lazy — nothing repaints until something calls
`os::invalidate()`, so an idle screen costs one keyboard poll per 12&nbsp;ms.

An app is a class with `draw()` and `onKey()`. Adding one is ~50 lines plus a
line in [main.cpp](src/main.cpp). See [docs/architecture.md](docs/architecture.md).

## Build

```bash
pio run -e cardputer -t upload
pio device monitor          # boot diagnostics re-print whenever you attach
```

## The Mac daemon

```bash
cd host
cp config.example.json config.json     # point `vault` at your Obsidian folder
pip install -r requirements.txt        # optional: mDNS + local whisper
python3 cardputerd.py
```

Then **Settings → Find Mac** (Bonjour), or type the address. **Test Mac** confirms.

| Endpoint | |
|---|---|
| `GET /ping` | health, vault status, which agent CLIs are actually installed |
| `POST /ask` | `{messages, system, backend}` → the chosen CLI, in a scratch dir |
| `POST /code` | `{prompt, project, backend}` → the chosen CLI, with tools |
| `POST /transcribe` | raw WAV → whisper.cpp, faster-whisper, or the API |
| `POST /vault/note` · `/vault/daily` · `/vault/read` · `GET /vault/list` | Obsidian |

Vault paths are resolved against the vault root, so the device cannot write
outside it. `vault_exclude` hides notes from the handheld entirely — it defaults
to `["*.secret.md", ".*", ".*/*"]`, because a pocket device is easy to lose and
your vault is bigger than what belongs on it.

## Secrets

This repo is public and holds no credentials. WiFi passwords and API keys are
typed on the device and live in NVS; the daemon reads its own gitignored
`config.json`. There is no `secrets.h` to fill in.

## Hardware notes

No PSRAM, so RAM is the real constraint. The audio buffer is allocated only
while recording and the UI canvas is released to make room, which is what buys
a ~4&nbsp;second memo on a board with 145&nbsp;KB free. BLE is started only
while the Bluetooth app is open.

Two Cardputer traps are documented in [docs/hardware.md](docs/hardware.md): the
SD clock and the I2S bit clock are **the same pin (GPIO40)**, and the SD card
must be **FAT32** — exFAT enumerates fine and then fails to mount.

## Status

v0.2 — nine apps, seven AI providers, themes, BLE. Builds at 52% flash, 19% RAM.
See [docs/roadmap.md](docs/roadmap.md).

MIT.
