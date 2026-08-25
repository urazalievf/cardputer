# Installing CardputerOS

From a device you have never flashed to one that works. Roughly 20 minutes,
most of it downloading a toolchain.

## What you need

- An **M5Stack Cardputer** (the StampS3 version; the ADV is not supported yet)
- A **USB-C cable that carries data**. A charge-only cable is the single most
  common reason "the port never appears"
- A Mac or Linux machine
- A **microSD card**, optional but strongly recommended — without one, notes fall
  back to internal storage and recordings are not kept at all

## 1. Install PlatformIO

```bash
python3 -m pip install --user platformio pyserial
```

`pyserial` is not optional: the provisioning tool in step 6 needs it.

## 2. Get the source

```bash
git clone https://github.com/urazalievf/cardputer.git
cd cardputer
```

## 3. Plug in and find the device

```bash
ls /dev/cu.usbmodem*        # macOS
ls /dev/ttyACM*             # Linux
```

Nothing listed? Work through these in order:

1. Try a different USB-C cable — most cheap ones are charge-only
2. Make sure the Cardputer is switched on (the side switch)
3. Hold the **G0 / BOOT** button on the StampS3 module while plugging the cable
   in, then release. That forces the ROM bootloader, which always enumerates

## 4. Build and flash

```bash
pio run -e cardputer -t upload
```

The first build downloads about 2.8 GB of toolchain and takes several minutes.
Later builds take about 20 seconds. When it finishes the device reboots into the
launcher.

## 5. Check it came up

```bash
pio device monitor
```

You should see a boot report. It reprints whenever you attach, so it is the
fastest way to see what the device thinks is true:

```
CardputerOS 0.2.0
chip     ESP32-S3 rev2, 2 cores @ 240MHz
heap     141 KB free
canvas   on (64KB sprite)
mic      ready, buffer 4s (allocated on demand)
sd       mounted, 0/243986 MB used, 0 notes
wifi     1 saved network(s)
ready
```

Leave with `ctrl+]`.

## 6. Get on WiFi

On the device: **Settings → Connectivity → WiFi networks**, pick yours, type the
password once. It is remembered and rejoined automatically.

Or from your computer, which is far less painful:

```bash
./tools/cardputer wifi "My Network" "my-password"
./tools/cardputer info
```

Once connected the device finds its own location, sets the clock, and seeds
Weather — no timezone to configure.

## 7. Format the SD card

Skip this if the boot report already says `sd mounted`.

The card must be **FAT32**. Cards of 64 GB and larger ship as exFAT, which this
device cannot read — the boot report will say so.

**Settings → Notes & vault → Format SD card.** It asks twice, then formats.

> A large card takes a **long** time. The FAT tables have to be zeroed with a
> 4 KB work buffer, so a 256 GB card takes around a minute. The screen shows an
> elapsed counter — as long as that is moving it is working. **Do not unplug**;
> interrupting it leaves the card exactly as unusable as before.

Afterwards, notes land in `/notes/*.md` and recordings in `/recordings/*.wav`.

## 8. Give it an assistant

Two routes. You can set up both — if one fails the device falls through to the
other by itself.

### Free, at home: your existing subscription

The daemon runs the `claude` CLI on your computer, so questions cost nothing
beyond the subscription you already pay for.

```bash
cd host
cp config.example.json config.json
# edit config.json: point "vault" at your Obsidian folder
python3 -m pip install --user zeroconf     # optional: lets the device find it
python3 cardputerd.py
```

It prints an auth token on startup. **The daemon has no other protection**, and
it can run a coding agent and read your vault, so the token is what stops anyone
else on your network using it:

```
token:   xxxxxxxx-your-token-here-xxxxxxxx
         set it on the device:  tools/cardputer set hosttoken <token>
```

Then, from another terminal:

```bash
./tools/cardputer set host <your-computer-ip>
./tools/cardputer seti hostport 8787
./tools/cardputer set hosttoken <the token it printed>
./tools/cardputer use mac
./tools/cardputer ask "does this work"
```

The daemon only works while your computer is awake and on the same network.

### Anywhere: an API key

```bash
./tools/cardputer key claude        # prompts, hidden, stays out of shell history
```

Also accepts `openai`, `gemini`, `groq`, `openrouter`. This bills your API
credits, which are separate from any subscription.

## 9. Try it

| | |
|---|---|
| `1` Notes | `N`, type, `` ` `` to save. Real markdown on the card |
| `2` Voice | `TAB`, talk, `TAB`. Audio saved, transcript shown |
| `3` Ask | `TAB` asks out loud; `ctrl+P` switches assistant |
| `11` Share | serves the card to any browser on your network |

Arrows are the `;` `.` `,` `/` keycaps. `` ` `` is back. `ctrl+H` is home.

## When something is wrong

**Run the tests.** They catch far more than staring at the screen:

```bash
pio run -e cardputer-selftest -t upload && pio device monitor
```

201 checks with a PASS/FAIL line each, then the device boots normally.

**Ask the device.** `pio device monitor`, or `./tools/cardputer info`. If the
card will not mount, the boot report runs a step-by-step probe and prints the
real reason instead of a guess.

**Upload fails with "No serial data received".** The app is hung and holding
USB. Knock it into the bootloader:

```bash
python3 -c "import serial,time; s=serial.Serial('/dev/cu.usbmodem2101',1200); s.dtr=False; time.sleep(0.4); s.close()"
```

Then upload again. If that does not work, hold **G0** while plugging in.

**Keys do nothing.** Settings → System → **Keyboard test** shows exactly what the
hardware reports for every key.
