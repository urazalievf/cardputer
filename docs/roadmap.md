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
- Settings grew from 17 entries to 45, covering every key, model and toggle

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
