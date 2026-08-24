# Roadmap

## v0.1 — shipped
- App kernel: registry, event loop, normalized keys, toasts
- Notes, Voice, Ask, Code, WiFi, Files, Settings
- Host-first cloud layer with API fallback
- Mac daemon: claude CLI, Claude Code, whisper, Obsidian vault
- On-device credential storage (nothing secret in the repo)

## v0.2 — next
- **Text cursor.** The editor only appends and backspaces today. Needs a real
  caret with `fn` + arrows, plus word-delete.
- **Two-way Obsidian sync.** `vaultList`/`vaultRead` exist on both sides but no
  app uses them yet — pull notes down, not just push up.
- **Streaming responses.** `claude -p --output-format stream-json` so long
  answers appear as they're generated instead of after a 30s blank screen.
- **Background jobs.** Move transcription and long Claude calls onto core 0 so
  the UI never blocks; a small job queue with a status-bar spinner.
- **Sleep.** Light sleep on idle, wake on keypress. The Cardputer battery is
  small and the radio is the biggest draw.

## v0.3 — later
- **Voice-activated capture.** VAD so a long press isn't required; auto-stop on
  silence rather than at buffer capacity.
- **App store-ish.** Load extra apps from SD instead of reflashing.
- **Offline STT.** A tiny on-device wake/command model for a handful of verbs,
  with full transcription still going to the host.
- **Encrypted NVS.** API keys are currently plaintext in flash; enable ESP-IDF
  NVS encryption with a key in eFuse.
- **BLE keyboard mode.** Cardputer as a dictation input device for the Mac.
- **Cardputer ADV support.** Different SoC (PSRAM, different pinout) — the
  hardware constants need to move behind a board header.

## Known limitations
- TLS uses `setInsecure()`. There's no cert bundle in the 8&nbsp;MB partition
  layout; a pinned root CA for the two API hosts would be the right fix.
- Notes without an SD card are capped by NVS size and keyed on the first 15
  characters of the filename.
- Chat history replays as plain text rather than structured turns, so the API
  path loses some of the role separation the host path keeps.
