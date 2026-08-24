# The apps

What each one is for, and the keys that matter. Everywhere: `` ` `` is back,
arrows are the `;` `.` `,` `/` keycaps, `ctrl+H` is home, `ctrl+T` cycles themes.

## Notes
Markdown files, one per note, named `YYYYMMDD-HHMMSS-slug.md`. On an SD card
they are real files you can drop straight into an Obsidian vault; without one
they fall back to NVS, keyed on the first 15 characters of the filename.

`N` new · `E` edit · `A` ask the assistant about this note · `S` push to the
vault over WiFi · `D` delete · `O` cycle sort (newest / oldest / A-Z)

## Voice
`TAB` starts and stops. Capture runs in 100ms chunks from `tick()` so the
waveform stays live rather than freezing the UI, and each chunk contributes
eight envelope points so the trace scrolls smoothly. The RGB LED goes red while
recording, because the screen is not always facing you.

`TAB` record / again · `P` play it back · `S` save as a note · `D` append to
today's daily note · `C` send to the assistant

Recording length depends on free RAM. The UI canvas is released first to make
room, which is what buys ~4 seconds on a board with no PSRAM.

## Ask
`TAB` is the whole point: record, transcribe and send in one press. History
persists across reboots and replays the last twelve turns for context.

`TAB` ask out loud · `Enter` send typed · `ctrl+P` switch assistant ·
`ctrl+D` dictate into the field without sending · `ctrl+L` clear

## Code
Runs a real coding agent in a project directory **on your Mac**. Host-only by
design — an agent without a filesystem is not an agent.

`Enter` run · `TAB` pick CLI (claude / codex / gemini) · `ctrl+P` project dir

## Translate
15 languages. `TAB` speaks, `Enter` translates what you typed.

`TAB` talk · `ctrl+L` change language

## Tasks
Stored as a markdown task list, so the file is already what Obsidian wants and
is directly editable elsewhere.

`N` new · `Enter` tick · `D` delete · `C` clear completed · `S` sync to vault ·
`←`/`→` reorder

## Calc
A recursive-descent parser, not a keypad — this device has a real keyboard.
Supports `+ - * / % ^`, parentheses, unary minus, `sqrt abs ln log sin cos tan
round floor ceil`, and the constants `pi`, `e`, `ans`.

`Enter` evaluate · `↑`/`↓` recall history · `ctrl+D` degrees/radians ·
`ctrl+L` clear history

## Timer
`TAB` cycles stopwatch → countdown → pomodoro. Pomodoro rolls straight from
focus into break and counts rounds.

`Space` start/pause · `R` reset · arrows adjust the countdown (±1m, ±10s)

## Weather
open-meteo for the forecast, ip-api for "where am I" — both over plain HTTP, so
it costs none of the ~45KB a TLS handshake wants. Coordinates are cached, so a
city that has not moved is not geocoded again.

`R` refresh · `L` set location · `U` switch °F/°C

## Remote
Bit-banged infrared. No receiver on this hardware, so there is no learning mode —
instead, published code sets for Samsung, LG/NEC and Sony, plus manual entry.

`Enter` send · `TAB` pick brand · `M` manual `address,command` in hex

If nothing happens, check the IR pin in Settings (G44 by default). A wrong pin
fails silently, since there is nothing to receive the signal back.

## Share
An HTTP server over the SD card. Browse and download from a phone; if there is
no network to join it hosts its own hotspot (`CardputerOS` / `cardputer`).

`Enter` start on the current network · `A` start as a hotspot

## Files
`Enter` open · `F` new folder · `N` new file (drops you into the editor) ·
`E` edit · `R` rename · `X` cut · `V` paste · `D` delete · `O` cycle sort
(name / size / type / newest) · `M` remount

Everything here needs a FAT32 card. exFAT enumerates and then fails to mount.

## Settings
Appearance, assistant, API keys, connectivity, vault, system, diagnostics.
`←`/`→` adjust sliders and enums in place without opening an editor.

Worth knowing about:
- **Edit colours** — twelve palette roles, each with an HSV picker and live
  preview. The first edit clones the active preset into a Custom slot.
- **Keyboard test** — the raw key report, live. Settles "is it me or the device?"
- **Scan Grove port** — I2C addresses with a guess at what each device is
- **Find Mac / Test Mac** — Bonjour discovery and a health check
- **Factory reset** — wipes settings and keys, leaves notes on the card alone

## WiFi and Bluetooth
Inside Settings → Connectivity, not on the launcher.

**WiFi**: `Enter` join · `R` rescan · `D` forget. Saved networks rejoin
automatically, strongest first.

**Bluetooth**: BLE only — the ESP32-S3 has no Classic radio, so no audio or SPP.
Scan nearby devices, or become a keyboard and type into a paired Mac, iPad or
phone. The stack is torn down on exit because it costs ~60KB of heap.
