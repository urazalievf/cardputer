# CardputerOS

A small app OS for the **M5Stack Cardputer** (StampS3 / ESP32-S3, 8&nbsp;MB flash, no PSRAM).

Notes, voice memos with speech-to-text, a chat assistant you can point at any
vendor, a remote coding agent, an Obsidian bridge, a WiFi manager and BLE — on
a device that fits in a pocket.

```
┌──────────────────────────────────────────┐
│ ⌂ CardputerOS 0.2.0     14:32 ▁▃▅▇ ⌁ 82% │
├──────────────────────────────────────────┤
│ ┌────┐┌────┐┌────┐┌────┐                 │
│ │1 ▤ ││2 ⏺ ││3 ▭ ││4 ‹›│                 │
│ │Note││Voic││Ask ││Code│                 │
│ └────┘└────┘└────┘└────┘                 │
│ ┌────┐┌────┐┌────┐┌────┐                 │
│ │5 ▭ ││6 ✓ ││7 ▣ ││8 ◷ │                 │
│ │Tran││Task││Calc││Time│                 │
│ └────┘└────┘└────┘└────┘                 │
│ ┌────┐┌────┐┌────┐                       │
│ │9 ☁ ││10▢ ││11⚙ │                       │
│ │Weat││File││Sett│                       │
│ └────┘└────┘└────┘                       │
├──────────────────────────────────────────┤
│ home-wifi  mac  sd  Gemini via Mac       │
└──────────────────────────────────────────┘
```

## Apps

| | |
|---|---|
| **Notes** | Markdown on the SD card — real `.md` files, so the card drops into an Obsidian vault. `S` pushes one over WiFi, `A` asks your assistant about the note you're reading. |
| **Voice** | `TAB` records with a live scrolling waveform, then transcribes. `P` plays it back. Save as a note, append to today's Obsidian daily note, or send to the assistant. |
| **Ask** | Conversation with persistent history. `TAB` switches vendor mid-thread, `ctrl+D` dictates instead of typing on 56 tiny keys. |
| **Code** | A real coding agent — `claude`, `codex` or `gemini` CLI — with tools, in a project directory on your Mac. |
| **Translate** | `TAB`, speak, get it back in 15 languages. The best argument for putting a mic and a language model in the same pocket. |
| **Tasks** | A checklist stored as a markdown task list — already the format Obsidian wants. `S` syncs it. |
| **Calc** | A real expression parser: `12*(3+4)/2`, `sqrt`, `sin`, `ln`, `ans`, `pi`, deg/rad, persistent history. |
| **Timer** | Stopwatch, countdown and pomodoro, with an audible finish. |
| **Weather** | Current conditions and three days, over plain HTTP with no API key. Finds you by IP, or name a city. |
| **Remote** | Universal IR remote — Samsung, LG/NEC and Sony code sets, or type a raw address/command. |
| **Share** | Serves the SD card over HTTP so a phone can pull notes off it. Hosts its own hotspot when there's no network. |
| **Files** | SD browser: make folders, make and edit files, rename, sort, and move things with cut/paste. |
| **Settings** | Themes, accent, brightness, every provider and key, connectivity, vault, timezone. |

**WiFi** and **Bluetooth** live inside Settings → Connectivity rather than
taking a launcher tile. Bluetooth does BLE scanning, and keyboard mode where
what you type reaches a paired Mac, iPad or phone.

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

**You do not need an API key.** The `claude`, `codex` and `gemini` CLIs log in
with your *account* on the Mac, so "use Gemini" means running `gemini` once
there — no key is ever typed into, or stored on, the handheld. `ctrl+P` in Ask
lists those account logins first, then the key-based providers.

With **Auto fallback** on, a provider that is down or unconfigured falls through
to the next one that is ready instead of failing at you.

Speech-to-text routes the same way: local whisper on the Mac, then OpenAI or
Groq.

## Make it yours

Settings is the point, not an afterthought.

- **7 themes** — Midnight, Terminal, Amber, Nord, Synthwave, Paper, Mono
- **Every colour editable** — Settings → Edit colours gives each of the twelve
  palette roles its own HSV picker with live preview. Editing one clones the
  active preset into a Custom slot, so presets are never destroyed
- **Accent colour** — a quick hue picker with the real UI drawn underneath it
- **Grid or list** home screen (`V`), with drag-free reorder (`O`) and A-Z sort
- **Brightness**, **big text**, **hints on/off**, **status clock**, **sounds**
- Every API key, every model string, the daemon address, the Bluetooth name,
  the vault folder and the timezone
- **Factory reset** that wipes settings and keys but leaves your notes alone

`ctrl+T` cycles themes from anywhere.

## Getting around

| | |
|---|---|
| `` ` `` | back — the app closes its sub-view first, then the nav stack, then home |
| `;` `.` `,` `/` | up / down / left / right — the arrow glyphs on those keycaps |
| `1`–`9` | launch an app from home |
| `ctrl`+`1`–`9` | launch from anywhere |
| `ctrl+H` | home |
| `ctrl+T` | next theme |
| `fn` + `; . , /` | arrows only — use this when a text field would otherwise type them |
| `ctrl+Backspace` | delete a word |
| `TAB` | the app's main verb — talk (Ask, Translate), record (Voice), switch mode (Timer) |

Settings → System → **Keyboard test** shows exactly what the hardware reports
for any key, which is the fastest way to settle an "is it me or the device?".

## Architecture

```
       ┌──────────── Cardputer ────────────┐      ┌──────── your Mac ────────┐
       │ apps/  notes voice ask code       │      │  cardputerd.py           │
       │        translate tasks calc timer │      │    ├─ claude / codex /   │
       │ ────────────────────────────────  │ WiFi │    │  gemini CLI         │
       │        weather files settings     │◄────►│    │  gemini CLI         │
       │        wifi bluetooth (in settings)│     │    ├─ whisper.cpp /      │
       │ ────────────────────────────────  │      │    │  faster-whisper     │
       │ kernel/ os ui theme ai net bt     │      │    └─ Obsidian vault     │
       │         store audio cloud         │      │                          │
       │ ────────────────────────────────  │      └──────────────────────────┘
       │ M5Unified / Arduino / ESP-IDF 5.5 │
       └───────────────────────────────────┘
                        └── falls back to Anthropic / OpenAI / Google / Groq /
                            OpenRouter directly when the Mac isn't reachable
```

It also uses the hardware nobody's firmware bothers with: the StampS3's RGB LED
on GPIO21 (a recording indicator you can see when the screen is face-down), the
Grove port's I2C bus (Settings → Scan Grove port names what it finds), and the
IR LED.

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

### Tests

`pio run -e cardputer-selftest -t upload` builds the same firmware plus an
on-device battery that runs at boot and prints a PASS/FAIL line per check over
USB serial — 201 checks covering the expression parser, text wrapping, config
and note storage, theme and colour round-trips, the provider table, the WiFi
store, buffer allocation, IR timing, the app registry, and heap behaviour
across a canvas release/acquire cycle. It is not built into the shipping image.

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

v0.2 — thirteen apps, seven AI providers, editable themes, BLE, IR, Grove I2C.
Builds at 56% flash, 19% RAM.
See [docs/roadmap.md](docs/roadmap.md).

MIT.
