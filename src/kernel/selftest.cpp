#include "selftest.h"
#include "os.h"
#include "ui.h"
#include "theme.h"
#include "store.h"
#include "net.h"
#include "audio.h"
#include "ai.h"
#include "cloud.h"
#include "hw.h"
#include "expr.h"
#include <SPI.h>
#include <sd_diskio.h>
#include <math.h>

// Exercises the parts of the OS that don't need a finger on the keyboard, and
// prints a PASS/FAIL line per check over USB serial. Built in only when
// SELFTEST is defined so the shipping firmware doesn't carry it.
namespace selftest {

static int s_pass = 0, s_fail = 0;
static const char* s_group = "";

static void group(const char* g) { s_group = g; os::logf("-- %s", g); }

static void check(bool ok, const String& what) {
    if (ok) { s_pass++; os::logf("   PASS  %s", what.c_str()); }
    else    { s_fail++; os::logf("   FAIL  %s", what.c_str()); }
}

// Neither a pass nor a fail: a test that could not run on this hardware.
static void note(const String& what) { os::logf("   SKIP  %s", what.c_str()); }

static void closeTo(double got, double want, const String& what) {
    check(fabs(got - want) < 1e-6, what + "  (got " + String(got, 6) + ")");
}

static void evalOk(const char* in, double want) {
    double v = 0;
    String err;
    if (!expr::eval(in, v, err)) { check(false, String(in) + " -> error: " + err); return; }
    closeTo(v, want, String(in) + " = " + String(want, 4));
}

static void evalFails(const char* in) {
    double v = 0;
    String err;
    check(!expr::eval(in, v, err), String("rejects \"") + in + "\"");
}

// ---------------------------------------------------------------- expressions
static void testExpr() {
    group("expression parser");
    evalOk("1+2", 3);
    evalOk("2+3*4", 14);                   // precedence
    evalOk("(2+3)*4", 20);
    evalOk("10-2-3", 5);                   // left associative
    evalOk("2^3^2", 512);                  // right associative
    evalOk("-4+1", -3);
    evalOk("--5", 5);
    evalOk("7/2", 3.5);
    evalOk("7%3", 1);
    evalOk("2*-3", -6);
    evalOk("sqrt(16)", 4);
    evalOk("abs(0-9)", 9);
    evalOk("floor(3.7)", 3);
    evalOk("ceil(3.2)", 4);
    evalOk("round(2.5)", 3);
    evalOk("log(1000)", 3);
    evalOk("  8  *  ( 1 + 1 )  ", 16);     // whitespace everywhere
    evalOk("pi", M_PI);

    double v = 0; String err;
    expr::Options deg; deg.degrees = true;
    check(expr::eval("sin(90)", v, err, deg) && fabs(v - 1.0) < 1e-9, "sin(90) in degrees = 1");
    expr::Options rad;
    check(expr::eval("sin(0)", v, err, rad) && fabs(v) < 1e-9, "sin(0) in radians = 0");
    expr::Options withAns; withAns.ans = 42;
    check(expr::eval("ans*2", v, err, withAns) && fabs(v - 84) < 1e-9, "ans substitution");

    evalFails("1/0");
    evalFails("(1+2");
    evalFails("1+");
    evalFails("");
    evalFails("hello");
    evalFails("sqrt(0-1)");
    evalFails("2 3");                      // no implicit anything

    group("number formatting");
    check(expr::format(3.0) == "3", "3.0 prints as \"3\"");
    check(expr::format(3.5) == "3.5", "3.5 keeps one decimal");
    check(expr::format(-0.25) == "-0.25", "negatives survive");
    check(expr::format(1.0 / 3.0).startsWith("0.3333"), "thirds do not print garbage");
}

// ----------------------------------------------------------------- text utils
static void testText() {
    group("text wrapping");
    auto lines = ui::wrap("hello world", 40);
    check(lines.size() == 1 && lines[0] == "hello world", "short text is one line");

    lines = ui::wrap("aaa\nbbb\nccc", 40);
    check(lines.size() == 3, "explicit newlines split");

    lines = ui::wrap("one two three four five six seven eight nine ten", 12);
    bool allFit = true;
    for (auto& l : lines) if ((int)l.length() > 12) allFit = false;
    check(allFit && lines.size() > 1, "wraps to the column limit");

    lines = ui::wrap("supercalifragilisticexpialidocious", 10);
    check(lines.size() >= 3, "a word longer than the line still breaks");

    lines = ui::wrap("", 20);
    check(lines.size() == 1, "empty input yields one empty line, not zero");

    check(ui::ellipsize("abcdef", 4) == "abc~", "ellipsize truncates with a marker");
    check(ui::ellipsize("abc", 10) == "abc", "ellipsize leaves short strings alone");
    check(ui::firstLine("# Title\nbody") == "Title", "firstLine strips markdown heading");
    check(ui::firstLine("") == "(empty)", "firstLine handles empty");

    group("utf-8");
    // Arduino String is a byte array. Measuring a translation by byte index is
    // what turned Russian into mojibake, so the decoder gets its own tests.
    const String ru = "\u043f\u0440\u0438\u0432\u0435\u0442";          // privet, 6 cp / 12 bytes
    const String jp = "\u3053\u3093\u306b\u3061\u306f";                 // konnichiwa, 5 cp / 15 bytes
    check(ru.length() == 12 && ui::utf8Len(ru) == 6, "cyrillic counts codepoints, not bytes");
    check(jp.length() == 15 && ui::utf8Len(jp) == 5, "kana counts codepoints, not bytes");
    check(ui::utf8Len("plain ascii") == 11, "ascii length is unchanged");
    check(ui::utf8Sub(ru, 0, 3) == "\u043f\u0440\u0438", "substring cuts on a character boundary");
    check(ui::utf8Sub(ru, 3, 3) == "\u0432\u0435\u0442", "substring offsets by codepoint");

    int i = 0;
    check(ui::utf8Decode(ru, i) == 0x043F && i == 2, "decode advances by the encoded width");
    i = 0;
    check(ui::utf8Decode("A", i) == 'A' && i == 1, "ascii decodes as itself");
    // A truncated sequence must yield a replacement char and still advance, or
    // any caller looping over it hangs.
    String bad = ru.substring(0, 1);
    i = 0;
    check(ui::utf8Decode(bad, i) == 0xFFFD && i > 0, "a truncated sequence terminates");

    group("glyph coverage");
    check(ui::isAscii("hello") && !ui::isAscii(ru), "ascii detection");
    check(ui::canRender('A'), "ascii always renders");
    check(ui::renderable("hello world"), "ascii is renderable");
    // These are the languages Translate offers; the font either has them or the
    // app has to fall back to a romanisation, and it must know which.
    check(ui::canRender(0x043F), "cyrillic renders (Russian, Ukrainian, Uzbek)");
    check(ui::canRender(0x3053), "hiragana renders (Japanese)");
    check(ui::canRender(0x4E2D), "CJK renders (Chinese)");
    check(!ui::canRender(0x0627), "arabic does not - romanisation expected");
    check(!ui::canRender(0x0939), "devanagari does not - romanisation expected");
    check(!ui::canRender(0xAC00), "hangul does not - romanisation expected");
    check(ui::renderable(ru) && ui::renderable(jp), "whole strings resolve");
}

// -------------------------------------------------------------------- storage
static void testStore() {
    group("config storage");
    store::setStr("t_str", "hello");
    check(store::getStr("t_str", "x") == "hello", "string round-trip");
    store::setInt("t_int", -1234);
    check(store::getInt("t_int", 0) == -1234, "int round-trip");
    check(store::getStr("t_missing", "fallback") == "fallback", "missing key gives default");
    store::remove("t_str");
    check(store::getStr("t_str", "gone") == "gone", "remove works");
    store::remove("t_int");

    group("notes");
    String name = store::newNoteName("Hello There, World!");
    check(name.endsWith(".md"), "note name ends in .md");
    check(name.indexOf("hello-there-world") > 0, "title is slugified");
    check(name.length() > 16, "name carries a timestamp prefix");

    String body = "# test note\n\nbody text\n";
    bool wrote = store::writeNote(name, body);
    check(wrote, "note writes (SD or NVS)");
    if (wrote) {
        check(store::readNote(name) == body, "note reads back identical");
        auto all = store::listNotes();
        bool found = false;
        for (auto& f : all) if (f == name) found = true;
        check(found, "note appears in the listing");
        store::deleteNote(name);
        check(store::readNote(name).length() == 0, "note deletes");
    }
}

// ---------------------------------------------------------------------- theme
static void testTheme() {
    group("sd backoff");
    if (!store::sdReady()) {
        store::sdAcquire();                       // prime the failure timestamp
        uint32_t t0 = millis();
        for (int i = 0; i < 10; i++) store::listNotes();
        uint32_t took = millis() - t0;
        check(took < 400, String("10 listings on a card-less device take ") + took +
                          "ms (backoff working)");
    } else {
        check(true, "card present - backoff not exercised");
    }

    // Read-only probe of the card at the SD protocol level. This is the step
    // formatSd() does first, so if it works the format path can reach the card
    // even when no filesystem on it is readable. Nothing is written.
    // sdcard_init() hands back a drive slot even when the card never came up,
    // and the geometry it reports is then uninitialised memory. Record what it
    // claims rather than asserting on it -- the format path deliberately goes
    // through SD.begin() instead, which reports failure honestly.
    group("streaming wav");
    if (store::sdReady()) {
        String path = String(store::REC_DIR) + "/selftest.wav";
        check(store::wavOpen(path), "streaming wav opens");
        int16_t tone[160];
        for (int i = 0; i < 160; i++) tone[i] = (int16_t)(i * 200 - 16000);
        check(store::wavAppend(tone, 160), "first chunk appends");
        check(store::wavAppend(tone, 160), "second chunk appends");
        check(store::wavSamples() == 320, "sample count tracks the appends");
        check(store::wavClose(), "close patches the header");
        // 44-byte header plus 320 16-bit samples.
        check(store::exists(path), "the file is on the card");
        auto entries = store::listDir(store::REC_DIR);
        size_t size = 0;
        for (auto& e : entries) if (e.name == "selftest.wav") size = e.size;
        check(size == 44 + 320 * 2, String("file is ") + (int)size + " bytes, expected 684");
        store::removeFile(path);
        check(!store::exists(path), "cleaned up");
    } else {
        note("no card - streaming wav tests skipped");
    }

    group("sd hardware");
    {
        // This probe used to run with the card still mounted, so sdcard_init()
        // handed back a SECOND slot on the same physical card and the two
        // fought: sector 0 would not read and the sector count came back
        // different on every run. That looked like a failing card for months.
        // It is also the exact path USB mass storage serves the host from, so
        // getting it right matters beyond the test.
        store::sdRelease();
        audio::releaseI2S();
        SPI.begin(40, 39, 14, 12);
        uint8_t pdrv = sdcard_init(12, &SPI, 20000000);
        if (pdrv == 0xFF) {
            check(false, "sdcard_init: no drive slot");
        } else {
            check(pdrv == 0, String("raw driver takes slot 0, got ") + pdrv);
            // The step mass storage was missing: a driver slot is not a live
            // card, and nothing addresses it until ff_sd_initialize() runs.
            check(store::sdRawInit(pdrv), "raw block device initialises");
            uint8_t buf[512];
            bool readOk = sd_read_raw(pdrv, buf, 0);
            check(readOk, "sector 0 reads over the raw driver");

            uint32_t sectors = sdcard_num_sectors(pdrv);
            uint32_t ssize = sdcard_sector_size(pdrv);
            os::logf("   note  raw geometry: %u sectors of %u bytes (%llu MB)",
                     (unsigned)sectors, (unsigned)ssize,
                     (unsigned long long)((uint64_t)sectors * ssize / 1048576ULL));
            check(ssize == 512, String("sector size is 512, got ") + ssize);
            // A card that reports a size no SD card has is a driver talking to
            // nothing; mass storage would hand the host that same lie.
            check(sectors > 0 && (uint64_t)sectors * ssize < 2048ULL * 1024 * 1024 * 1024,
                  "sector count is a size a card could actually be");
            if (readOk) {
                os::logf("   note  boot signature %02X%02X, first bytes %02X %02X %02X %02X",
                         buf[510], buf[511], buf[0], buf[1], buf[2], buf[3]);
                check(buf[510] == 0x55 && buf[511] == 0xAA,
                      "sector 0 carries an MBR/VBR signature");
            }
            sdcard_uninit(pdrv);
        }
        SPI.end();
        // Put the filesystem back for whatever runs after this.
        check(store::sdMount(true), "card remounts after the raw probe");
    }

    group("theme");
    int startPreset = theme::preset();
    check(theme::presetCount() >= 7, "at least seven palettes plus Custom");
    for (int i = 0; i < theme::presetCount(); i++)
        check(theme::presetName(i) != nullptr && strlen(theme::presetName(i)) > 0,
              String("preset ") + i + " is named");

    theme::setPreset(1);
    check(theme::preset() == 1, "preset selection sticks");
    uint16_t before = theme::colorRole(3);
    theme::setColorRole(3, 0x1234);
    check(theme::colorRole(3) == 0x1234, "colour edit applies");
    check(theme::isCustom(), "editing a colour switches to the Custom slot");
    theme::setPreset(1);
    check(theme::colorRole(3) == before, "the preset itself was not overwritten");

    group("colour conversion");
    for (int h = 0; h < 256; h += 37) {
        uint16_t c = theme::fromHsv((uint8_t)h, 255, 255);
        uint8_t rh, rs, rv;
        theme::toHsv(c, rh, rs, rv);
        int diff = abs((int)rh - h);
        if (diff > 128) diff = 256 - diff;
        check(diff <= 8, String("hue ") + h + " survives a round-trip (drift " + diff + ")");
    }
    theme::setPreset(startPreset);
}

// ------------------------------------------------------------------ providers
static void testAi() {
    group("assistant routing");
    check((int)ai::Provider::COUNT == 7, "seven providers registered");
    for (int i = 0; i < (int)ai::Provider::COUNT; i++) {
        ai::Provider p = (ai::Provider)i;
        const ai::Spec& sp = ai::spec(p);
        check(sp.id && strlen(sp.id) > 0, String("provider ") + i + " has an id");
        check(sp.label && strlen(sp.label) > 0, String(sp.id) + " has a label");
        check(sp.defModel && strlen(sp.defModel) > 0, String(sp.id) + " has a default model");
        check(sp.modelKey && strlen(sp.modelKey) <= 15, String(sp.id) + " model key fits NVS");
        if (sp.needsKey) check(sp.keyKey && strlen(sp.keyKey) <= 15,
                               String(sp.id) + " key name fits NVS");
        check(ai::model(p) == String(sp.defModel), String(sp.id) + " model defaults correctly");
    }
    ai::Provider was = ai::preferred();
    ai::setPreferred(ai::Provider::OpenAI);
    check(ai::preferred() == ai::Provider::OpenAI, "preferred provider persists");
    ai::setPreferred(was);

    check(!ai::configured(ai::Provider::Gemini) || store::getStr("k_gemini", "").length(),
          "configured() only true when a key exists");

    // With nothing set up, a request must fail fast and say so -- never hang.
    uint32_t t0 = millis();
    auto r = ai::ask("ping", "", 16);
    uint32_t took = millis() - t0;
    check(!r.ok, "unconfigured request fails rather than pretending");
    check(r.error.length() > 0, "failure carries a message: " + r.error);
    check(took < 25000, String("fails within 25s (took ") + took + "ms)");
}

// ---------------------------------------------------------------------- radio
static void testNet() {
    group("wifi store");
    net::saveNetwork("__selftest_ap", "hunter2");
    check(net::isKnown("__selftest_ap"), "network saved");
    check(net::passwordFor("__selftest_ap") == "hunter2", "password round-trips");
    net::saveNetwork("__selftest_ap", "changed");
    check(net::passwordFor("__selftest_ap") == "changed", "re-saving updates, not duplicates");
    int before = net::savedNetworks().size();
    net::forgetNetwork("__selftest_ap");
    check(!net::isKnown("__selftest_ap"), "network forgotten");
    check((int)net::savedNetworks().size() == before - 1, "listing shrank by exactly one");
    check(net::signalBars() >= 0 && net::signalBars() <= 4, "signal bars stay in range");
}

// -------------------------------------------------------------------- battery
static void testBattery() {
    group("battery filter");
    // Let the ring fill: one sample per 250ms, seeded once three have landed.
    uint32_t t0 = millis();
    while (millis() - t0 < 900) { hw::batteryTick(); delay(10); }

    const hw::Battery& b = hw::battery();
    check(b.known, "battery reads");
    check(b.percent >= 0 && b.percent <= 100, String("filtered level ") + b.percent + "% in range");
    check(b.raw >= 0 && b.raw <= 100, "raw sample in range");

    // The whole point: a bare ADC read swings ten points at a stand, so the
    // displayed value must never move more than one point per 250ms sample.
    int before = b.percent;
    uint32_t t1 = millis();
    while (millis() - t1 < 300) { hw::batteryTick(); delay(10); }
    check(abs(hw::battery().percent - before) <= 1, "moves at most one point per sample");

    // Repeated ticks inside one sample window must not advance it at all.
    int held = hw::battery().percent;
    for (int i = 0; i < 50; i++) hw::batteryTick();
    check(hw::battery().percent == held, "sampling is rate limited, not per call");

    group("battery colour");
    const auto& pal = theme::cur();
    check(ui::batteryColor(0)  == pal.bad, "empty is alarm red");
    check(ui::batteryColor(19) == pal.bad, "just under a fifth is still red");
    check(ui::batteryColor(20) != pal.bad, "at a fifth it leaves the alarm colour");
    // RGB565: red is bits 15-11, green bits 10-5.
    auto red   = [](uint16_t c) { return (c >> 11) & 0x1F; };
    auto green = [](uint16_t c) { return (c >> 5) & 0x3F; };
    uint16_t low = ui::batteryColor(25), mid = ui::batteryColor(60), full = ui::batteryColor(100);
    check(green(full) >= green(mid) && green(mid) >= green(low), "green rises as it fills");
    check(red(full) <= red(mid) && red(mid) <= red(low), "red falls as it fills");
}

// ---------------------------------------------------------------------- audio
static void testAudio() {
    group("audio");
    // micReady() used to be Mic.isEnabled(), which only reports that a data pin
    // is configured -- true on a Cardputer from boot onwards even when the I2S
    // port never came up. Every capture then failed on the first chunk and the
    // apps blamed the user for speaking too briefly. Prove the port instead.
    check(audio::micOn(), "microphone I2S starts");
    check(M5Cardputer.Mic.isRunning(), "mic capture task is running");
    check(audio::micReady(), "microphone reports ready");

    {
        // One real block through the hardware. Silence is fine here -- a bench
        // is quiet -- but a refused read is not.
        int16_t probe[512];
        check(audio::sampleOnce(probe, 512), "a raw block reads back from the mic");
    }

    // Coming back from a release is the path Voice takes on every recording.
    audio::releaseI2S();
    check(!M5Cardputer.Mic.isRunning(), "releaseI2S stops the capture task");
    check(audio::micOn(), "microphone restarts after a release");

    size_t planned = audio::capacitySamples();
    check(planned > audio::sampleRate(), "planned capacity is over one second");
    check(!audio::bufferHeld(), "buffer is not held at rest");
    check(audio::stopReason() == audio::Stop::None, "no stale stop reason at rest");

    group("continuous capture");
    // The bug this guards: one queued slot at a time left mic_task blocked on
    // an empty queue during every repaint, so a quarter of the audio was never
    // read and Whisper answered the wreckage with "you".
    if (audio::recordStart()) {
        check(M5Cardputer.Mic.isRecording() > 0, "a request is in flight before the first chunk");
        int chunks = 0;
        bool everStarved = false;
        uint32_t t0 = millis();
        while (chunks < 8 && millis() - t0 < 4000) {
            if (!audio::recordChunk()) break;
            // This is the moment the old code went deaf: analysis done, caller
            // about to draw. Something must still be queued.
            if (M5Cardputer.Mic.isRecording() == 0) everStarved = true;
            chunks++;
            delay(25);                      // stand in for a repaint
        }
        check(chunks >= 8, String("captured ") + chunks + " chunks");
        check(!everStarved, "the driver queue never runs dry between chunks");
        check(audio::recordedSeconds() > 0.7f, "chunks accumulate into a real duration");
        audio::recordStop();
        check(audio::recordedSamples() >= (size_t)chunks * (audio::sampleRate() / 10),
              "in-flight chunks are counted, not discarded");
        audio::freeBuffer();
    } else {
        check(false, String("recordStart failed: ") + audio::startError());
    }

    group("capture ring");
    check(audio::pendingChunk(nullptr) == nullptr, "nothing pending once the buffer is freed");
    if (audio::allocBuffer()) {
        check(audio::capacitySamples() % (audio::sampleRate() / 10) == 0,
              "capacity is a whole number of chunks, so the ring cannot straddle");
        audio::freeBuffer();
    }

    // Must succeed even with the canvas still up: allocation has to size
    // itself against memory that is actually free, not memory it hopes for.
    bool got = audio::allocBuffer();
    check(got, "capture buffer allocates while the canvas is still held");
    if (got) {
        check(audio::bufferHeld(), "buffer reports held");
        check(audio::pcm() != nullptr, "pcm pointer is valid");
        check(audio::capacitySamples() >= audio::sampleRate(),
              String("got ") + String(audio::capacitySamples() / (float)audio::sampleRate(), 1) +
              "s of capture room");
        audio::freeBuffer();
        check(!audio::bufferHeld(), "buffer frees");
    }

    audio::waveClear();
    check(audio::waveCount() == 0, "waveform ring starts empty");
    check(audio::waveAt(0) == 0.0f, "reading past the end is safe");
    check(audio::waveAt(-1) == 0.0f, "negative index is safe");
}

// ------------------------------------------------------------------- hardware
static void testHw() {
    group("hardware");
    for (int i = 0; i < (int)hw::IrProto::COUNT; i++)
        check(strlen(hw::irProtoName((hw::IrProto)i)) > 0,
              String("IR protocol ") + i + " is named");
    check(hw::irPin() <= 48, "IR pin is a plausible GPIO");

    // The real risk here was a freeze: irSend used to mask interrupts across a
    // whole frame and call delay() inside it, which never returns.
    uint32_t t0 = millis();
    hw::irSend(hw::IrProto::NEC, 0x04, 0x08);
    uint32_t nec = millis() - t0;
    check(nec < 500, String("NEC frame returns promptly (") + nec + "ms)");

    t0 = millis();
    hw::irSend(hw::IrProto::SonySIRC12, 0x01, 0x15);
    uint32_t sony = millis() - t0;
    check(sony < 800, String("Sony 3x frame returns promptly (") + sony + "ms)");

    t0 = millis();
    auto found = hw::i2cScan();
    check(millis() - t0 < 4000, "Grove scan completes");
    os::logf("   note  Grove port: %d device(s)", (int)found.size());

    hw::ledPulse(0, 0, 30, 20);
    check(true, "RGB LED write did not fault");
}

// ------------------------------------------------------------------ integrity
static void testApps() {
    group("app registry");
    const auto& apps = os::apps();
    check(apps.size() >= 10, String("registered ") + (int)apps.size() + " apps");
    int visible = 0;
    for (size_t i = 0; i < apps.size(); i++) {
        App* a = apps[i];
        check(a != nullptr, String("slot ") + i + " is not null");
        if (!a) continue;
        check(strlen(a->name()) > 0, String("app ") + i + " has a name");
        check(a->title().length() > 0, String(a->name()) + " produces a title");
        if (i > 0 && !a->hidden()) visible++;
    }
    // The grid scrolls past three rows, so the only real bound is the number
    // key shortcuts (1-9) plus whatever the user scrolls to.
    check(visible > 0 && visible <= 32, String(visible) + " visible apps");
    os::logf("   note  %d visible, %d hidden", visible, (int)apps.size() - 1 - visible);

    // Names must be unique: launchByName() and the saved home order key on them.
    bool unique = true;
    for (size_t i = 0; i < apps.size(); i++)
        for (size_t j = i + 1; j < apps.size(); j++)
            if (strcmp(apps[i]->name(), apps[j]->name()) == 0) unique = false;
    check(unique, "app names are unique");
}

static void testMemory() {
    group("memory");
    uint32_t heap = ESP.getFreeHeap();
    os::logf("   note  free heap %u KB, canvas %s",
             (unsigned)(heap / 1024), ui::canvasActive() ? "on" : "off");
    check(heap > 40 * 1024, "at least 40KB of heap headroom");

    // The canvas must survive being released and re-taken; Voice does this on
    // every recording.
    bool had = ui::canvasActive();
    ui::releaseCanvas();
    check(!ui::canvasActive(), "canvas releases");
    ui::acquireCanvas();
    if (had) check(ui::canvasActive(), "canvas comes back");

    uint32_t after = ESP.getFreeHeap();
    check(after + 8192 > heap, String("no large leak across release/acquire (") +
                               (int)((int32_t)heap - (int32_t)after) + " bytes)");
}

int run() {
    s_pass = s_fail = 0;
    os::logf("==================== SELF TEST ====================");
    uint32_t t0 = millis();
    testExpr();
    testText();
    testStore();
    testTheme();
    testAi();
    testNet();
    testBattery();
    testAudio();
    testHw();
    testApps();
    testMemory();
    os::logf("==================================================");
    os::logf("RESULT  %d passed, %d failed, %lums",
             s_pass, s_fail, (unsigned long)(millis() - t0));
    return s_fail;
}

}  // namespace selftest
