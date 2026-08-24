# Hardware

**M5Stack Cardputer v1.1** — the StampS3 version.

| | |
|---|---|
| SoC | ESP32-S3FN8 (StampS3), dual-core @ 240 MHz |
| Flash | 8 MB |
| PSRAM | none on stock StampS3 |
| Display | 1.14" ST7789, 240×135, landscape at rotation 1 |
| Keyboard | 56 keys, I2C matrix |
| Mic | SPM1423 PDM |
| Speaker | NS4168 amp — **shares I2S with the mic** |
| SD | SPI: SCK 40, MISO 39, MOSI 14, CS 12 |
| Battery | 120 mAh internal, 1400 mAh in the base |
| Extras | Grove port, IR, USB-C |

## Things that bite

**Mic and speaker cannot both be live.** One I2S peripheral. `audio::micOn()`
and `audio::speakerOn()` call `end()` on the other and wait ~60 ms for the
driver to release. Every beep during recording costs you that round trip.

**No PSRAM means no big audio buffer.** `audio::begin()` asks for 30 s of
16 kHz mono, gets nothing from SPIRAM on a stock unit, and falls back to half
the free internal heap capped at 160 KB — about 5 seconds. Leave the rest:
TLS handshakes need ~40 KB of contiguous heap, and a full buffer plus a
`WiFiClientSecure` will OOM.

**Arrow keys are `fn` + `;` `.` `,` `/`.** The glyphs are printed on the
keycaps. `os::translate()` maps them; apps see `k.up`/`k.down`/`k.left`/`k.right`.

**The `` ` `` key is Esc.** The kernel intercepts it for back/home unless an app
returns `false` from `escExits()`. Text fields drop it rather than inserting a
backtick.

**Flashing.** Usually just works over USB CDC. If the port doesn't appear, hold
the G0 button on the StampS3 while plugging in to force download mode.
