# CardputerOS

A small app OS for the **M5Stack Cardputer** (StampS3 / ESP32-S3, 8&nbsp;MB flash).

Notes, voice memos with speech-to-text, Claude, Claude Code, an Obsidian
bridge and a WiFi manager — on a device that fits in a pocket.

```
┌──────────────────────────────────────┐
│ CardputerOS 0.1.0          |||. 82%  │
├──────────────────────────────────────┤
│ 1  Notes      markdown               │
│ 2  Voice      record + stt           │
│ 3  Ask        chat claude            │
│ 4  Code       claude code            │
│ 5  WiFi       networks               │
│ 6  Files      sd card                │
│ 7  Settings   keys + host            │
├──────────────────────────────────────┤
│ 14:32  home-wifi  [mac] [sd]         │
└──────────────────────────────────────┘
```

## What it does

| App | |
|---|---|
| **Notes** | Markdown notes on the SD card — real `.md` files, so the card drops straight into an Obsidian vault. Or press `S` to push one over WiFi. `A` asks Claude about the note you're reading. |
| **Voice** | `TAB` records with a live level meter, then transcribes. Save as a note, append to today's Obsidian daily note, or send straight to Claude. |
| **Ask** | Conversational Claude with persistent history. `TAB` dictates instead of typing on 56 tiny keys. |
| **Code** | Real Claude Code, with tools, running in a project directory on your Mac. The Cardputer is the keyboard and the screen. |
| **WiFi** | Scan any network, join it, and it's remembered. Rejoins the strongest known network on boot. |
| **Files** | SD browser with a text pager. |
| **Settings** | Host, API keys, model, vault folder, timezone, brightness — all in NVS, none of it in this repo. |

## Architecture

Two halves, and the device works standalone if you only build the first one.

```
       ┌─────────── Cardputer ───────────┐        ┌──────── your Mac ────────┐
       │                                 │        │                          │
       │  apps/   notes voice ask code   │        │   cardputerd.py          │
       │          wifi files settings    │        │     ├─ claude CLI        │
       │  ─────────────────────────────  │  WiFi  │     │   (Max plan, free) │
       │  kernel/ os  ui  net  store     │◄──────►│     ├─ whisper.cpp /     │
       │          audio  cloud           │        │     │   faster-whisper   │
       │  ─────────────────────────────  │        │     └─ Obsidian vault    │
       │  M5Unified / Arduino / ESP-IDF  │        │                          │
       └─────────────────────────────────┘        └──────────────────────────┘
                        │
                        └── falls back to api.anthropic.com + OpenAI Whisper
                            when the Mac isn't reachable
```

**Host-first.** Every network capability tries the Mac daemon before the cloud.
The daemon shells out to your `claude` CLI, so chat and Claude Code cost nothing
beyond your existing subscription, and it can run whisper locally. When you're
away from home the device falls back to direct API calls with keys you type into
Settings. Claude Code is the one host-only feature — it needs a real filesystem.

### Kernel

`src/kernel/` is the whole OS: ~1.5k lines.

- **`os`** — app registry, the event loop, key normalization (arrows live under `fn` + `; . , /`), toasts
- **`ui`** — word wrap, scrolling lists, text input, modals, the status bar
- **`net`** — async scan, join, saved-network store, autojoin, NTP
- **`store`** — NVS config and an SD filesystem, with NVS-backed notes when there's no card
- **`audio`** — chunked mic capture (mic and speaker share one I2S peripheral), WAV framing
- **`cloud`** — the host-first client and its API fallbacks

An app is a class with `draw()` and `onKey()`. Adding one is ~50 lines plus a
line in [main.cpp](src/main.cpp).

## Build

```bash
pio run -e cardputer -t upload    # hold G38/BOOT while plugging in if it won't enter DFU
pio device monitor
```

Toolchain is the [pioarduino](https://github.com/pioarduino/platform-espressif32)
fork on IDF 5.5 — the stock Espressif platform doesn't cover the StampS3 well.
Everything is pinned in [platformio.ini](platformio.ini).

## The Mac daemon

```bash
cd host
cp config.example.json config.json     # point `vault` at your Obsidian folder
pip install -r requirements.txt        # optional: mDNS + local whisper
python3 cardputerd.py
```

Then on the device: **Settings → Find Mac** (Bonjour), or type the hostname by
hand. **Test Mac** confirms the link.

| Endpoint | |
|---|---|
| `GET /ping` | health + which features are live |
| `POST /ask` | `{prompt, system}` → `claude -p` in a scratch dir |
| `POST /code` | `{prompt, project}` → `claude -p` in a project dir, with tools |
| `POST /transcribe` | raw WAV body → whisper.cpp, faster-whisper, or the API |
| `POST /vault/note` | `{path, content, append}` → writes into the vault |
| `POST /vault/daily` | `{content}` → appends to today's daily note |
| `POST /vault/read` | `{path}` → note contents |
| `GET /vault/list` | every `.md` under the vault |

Vault paths are resolved and checked against the vault root, so the device
cannot write outside it.

## Secrets

This repo is public and holds no credentials. WiFi passwords and API keys are
typed on the device and live in NVS; the daemon reads its own `config.json`,
which is gitignored. There is no `secrets.h` to fill in.

## Hardware notes

Stock Cardputer (StampS3, **no PSRAM**) gets roughly a 5-second mic buffer out
of internal heap. Boards with PSRAM get 30 seconds — `audio::begin()` probes and
adapts. SD is on SPI (SCK 40, MISO 39, MOSI 14, CS 12). Mic and speaker share
I2S, so `audio` hands the peripheral back and forth.

## Status

v0.1 — working scaffold, all seven apps functional. See [docs/roadmap.md](docs/roadmap.md).

MIT.
