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
today's daily note · `C` send to the assistant · `Q` 16kHz / 8kHz ·
`M` mic check

The key that starts a recording cannot also stop it: the stop is armed only
once every key has been released. Without that, the `TAB` still under your
thumb ends the capture a chunk after it began, and the memo comes back as
"too short".

`M` is the diagnostic. It reads 512 samples straight off the microphone with no
buffer allocated and shows the live level and the running peak, which separates
the three things that used to look identical: the I2S port refusing to start,
the port running but hearing nothing, and a recording that was genuinely brief.

The audio goes to `/recordings/<timestamp>.wav`. With a card present it is
written *as it is captured*, so the length is bounded by the card and a ten
minute cap rather than by free RAM — the ~4 second ceiling was the largest free
heap block, and nothing else. The capture buffer is a ring; each completed chunk
is appended to the open file and its space reused. Transcription then uploads
straight off the filesystem, because a minute of 16kHz audio is roughly 2MB and
never fits in memory at once.

Without a card, capture stays in RAM and stops when the ring is full, which is
the old behaviour. Settings > Keep audio turns the file off entirely and forces
the RAM path.

Saving the transcript as a note adds a link back to the wav. `P` plays either
kind back: a RAM recording straight from the buffer, a streamed one off the card
a block at a time, since it is far too big to load.

## Ask
`TAB` is the whole point: record, transcribe and send in one press. History
persists across reboots and replays the last twelve turns for context.

Dictation here and in Translate goes through `kernel/dictate.cpp`, the same
streaming capture Voice uses — so neither is capped by free RAM any more. The
audio goes to a scratch file that is deleted once the transcript is back, unless
Settings > Keep audio is on, in which case it is renamed into `/recordings`. A
dictation is a means to a transcript, not a memo.

`TAB` ask out loud · `Enter` send typed · `ctrl+P` switch assistant ·
`ctrl+D` dictate into the field without sending · `ctrl+L` clear

## Code
Runs a real coding agent in a project directory **on your Mac**. Host-only by
design — an agent without a filesystem is not an agent.

`Enter` run · `TAB` pick CLI (claude / codex / gemini) · `ctrl+P` project dir

## Translate
15 languages. `TAB` speaks, `Enter` translates what you typed.

`TAB` talk · `Enter` translate typed text · `ctrl+L` change language ·
`ctrl+R` script / romanised · `fn`+`;`/`.` scroll

Speaking uses the same streaming capture as Voice, so a long sentence is no
longer cut off at whatever fitted in RAM.

The built-in glyph set is ASCII, so for most of these languages the reply had
nowhere to be drawn. The firmware now embeds efont (~311KB), which covers
Cyrillic, Greek, kana and 6764 CJK ideographs — Russian, Ukrainian, Uzbek,
Chinese and Japanese render in their own script.

Korean, Arabic and Hindi have no glyphs at this size, so every translation asks
for a romanisation in the same round trip and falls back to it automatically.
Whether a reply can be drawn is decided by asking the font for each codepoint,
not by a table of script ranges, so the answer stays correct if the font is
ever changed. `ctrl+R` switches between the two by hand — useful even for
scripts that do render, since reading a phrase aloud is half the point.

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

Results chain. After `4+5` the answer stays in the input, so typing `/2` gives
`4.5` without retyping the 9. An operator continues from the result; a digit
starts a fresh calculation -- the same rule a physical calculator uses.

## Timer
`TAB` cycles stopwatch → countdown → pomodoro. Pomodoro rolls straight from
focus into break and counts rounds.

`Space` start/pause · `R` reset · arrows adjust the countdown (±1m, ±10s)

## Weather
open-meteo for the forecast, ip-api for "where am I" — both over plain HTTP, so
it costs none of the ~45KB a TLS handshake wants. Coordinates are cached, so a
city that has not moved is not geocoded again.

Conditions are drawn, not just named. WMO codes collapse to nine shapes — clear,
mostly clear, partly cloudy, overcast, fog, drizzle, rain, snow, storm — built
from the same primitives as the rest of the icon set, so there are no bitmaps in
flash, one size parameter serves both the headline and the forecast rows, and
they recolour with the theme. The headline icon follows `is_day` and shows a
crescent after dark; forecast rows always use the daytime shape, since a moon on
a Tuesday would be claiming something the forecast does not say.

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

## Provisioning from a computer

`tools/cardputer` talks to a serial console in the firmware, so settings can be
written from a real keyboard instead of the device's own:

| | |
|---|---|
| `set <key> <value>` | write a setting |
| `get <key>` · `del <key>` | read (secrets masked) / remove |
| `keys` | every known setting and whether it is set |
| `wifi <ssid> <pass>` | save a network |
| `env <file>` | push a whole file of `KEY=VALUE` lines |
| `ls [dir]` · `cat <path>` | browse the card |
| `info` · `reboot` · `shell` | |

Physical USB access is the authentication: if someone has the cable they can
read NVS anyway.

## WiFi and Bluetooth
Inside Settings → Connectivity, not on the launcher.

**WiFi**: `Enter` join · `R` rescan · `D` forget. Saved networks rejoin
automatically, strongest first.

**Bluetooth**: BLE only — the ESP32-S3 has no Classic radio, so no audio or SPP.
Scan nearby devices, or become a keyboard and type into a paired Mac, iPad or
phone. The stack is torn down on exit because it costs ~60KB of heap.
