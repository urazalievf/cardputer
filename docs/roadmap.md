# Roadmap

## v0.2 — shipped
- **Any assistant**: Claude, ChatGPT, Gemini, Groq, OpenRouter, local Ollama and
  the Mac daemon behind one `ai::chat()`, with automatic fallback
- **Design system**: 7 themes, a live accent-hue picker, big-text mode, icons
  drawn from primitives, rounded panels, block level meters, spinners, toasts
  with tone
- **Flicker-free rendering**: a 64KB off-screen sprite pushed in one DMA
  transfer, released automatically when audio needs the RAM
- **Navigation**: a real back stack — apps close their sub-view first, then the
  kernel pops — plus `ctrl+H` home, `ctrl+1..8` jump, `ctrl+T` theme
- **Bluetooth**: BLE scanner, and HID keyboard mode that types into a paired
  Mac, iPad or phone
- **Lazy audio buffer**: allocated only while recording, so idle heap stays high
- Settings grew from 17 entries to 45, covering every key, model and toggle;
  WiFi and Bluetooth moved inside it rather than taking launcher tiles
- **Five more apps**: Translate (speak, get another language back), Tasks
  (markdown checklist that syncs to Obsidian), Calc (recursive-descent
  expression parser with history and ans), Timer (stopwatch / countdown /
  pomodoro), Weather (open-meteo + ip-api, plain HTTP, no key)
- **Files** can now create folders and files, edit text in place, rename, and
  move things between folders with cut/paste
- **Arrow keys fixed**: `; . , /` set the arrow flags directly, the way every
  other Cardputer firmware behaves, while still typing literally in text
  fields. `ctrl`+digit was broken too — with ctrl held the keyboard reports the
  shifted glyph, so `ctrl+1` arrived as `!`
- Settings → Keyboard test shows the raw key report, so the next input bug is
  one screen away instead of four reflashes
- **Remote**: IR universal remote (Samsung / LG-NEC / Sony), bit-banged so it
  needs no library; the IR pin is a setting because a wrong pin fails silently
- **Share**: HTTP file server over the SD card, with a self-hosted access point
  for when there is no network to join
- **Live waveform** in Voice — a scrolling mirrored envelope built from eight
  sub-blocks per 100ms chunk — plus `P` to play the recording back
- **Every palette role editable** with an HSV picker; edits clone the active
  preset into a Custom slot rather than overwriting it
- Home screen does grid or list, reorder, and A-Z sort; Files and Notes cycle
  sort order; Files can create folders and files, edit and rename them
- RGB LED (GPIO21) as a recording indicator, Grove I2C scanner with a
  device-name table

### Fixed in testing
- **IR froze the device.** `irSend()` masked interrupts across a whole frame and
  `sendSony()` called `delay()` inside that window — with the tick interrupt off,
  `delay()` never returns. Critical sections are now per-mark, and the inter-frame
  gap is a real delay outside them.
- **Network calls looked like crashes.** Every one painted a single static frame
  and blocked for up to 30s. `ui::await()` now runs the work on the other core
  and animates a spinner with an elapsed counter.
- **The capture buffer refused to allocate** whenever the UI canvas was still
  held: it sized itself against memory it *expected* to be freed rather than
  memory that was actually free, and ignored fragmentation. It now works from
  the largest contiguous block and backs off instead of failing outright.
- **Card-less devices paid ~200ms per note operation** retrying the SD mount.
  A 3-second backoff cut ten listings from ~470ms to 11ms.

## v0.3 — next
- **Text cursor.** The editor still only appends and backspaces. Needs a caret
  with `fn`+arrows and word motions. This is the biggest remaining papercut.
- **Streaming replies.** Every provider supports SSE; today a long answer means
  a spinner and then a wall of text. `claude -p --output-format stream-json` on
  the host path, `stream: true` elsewhere.
- **Background jobs.** Move transcription and chat onto core 0 with a job queue
  so the UI never blocks. The status bar already has room for a spinner.
- **Two-way Obsidian sync.** `vaultList`/`vaultRead` exist on both sides and no
  app calls them yet — pull notes down, not just push up.
- **Sleep.** Light sleep on idle, wake on keypress. The radio is the biggest
  draw and the battery is 120mAh.

## v0.4 — later
- **On-device wake word** for a handful of verbs, full transcription still remote
- **VAD** so recording auto-stops on silence instead of at buffer capacity
- **Encrypted NVS.** API keys are plaintext in flash today; ESP-IDF supports NVS
  encryption with a key in eFuse.
- **Apps from SD** instead of reflashing
- **Cardputer ADV** — different SoC with PSRAM and a different pinout; the
  hardware constants need to move behind a board header
- **BLE central mode** so the Cardputer can drive other peripherals, not just
  present itself as one

## Known limitations
- TLS uses `setInsecure()`. No CA bundle fits the current partition layout; a
  pinned root CA for the handful of API hosts is the right fix.
- The SD card must be FAT32. exFAT enumerates and then fails to mount.
- Without an SD card, notes fall back to NVS and are keyed on the first 15
  characters of the filename.
- BLE HID sends one keycode at a time with a 5ms release; fine for typing,
  not for held keys or chords.
- The launcher grid is fixed at 4x2, so a ninth app would need it to scroll.
