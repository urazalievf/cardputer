#include "ui.h"
#include "net.h"
#include "bt.h"
#include "audio.h"
#include "hw.h"
#include <M5GFX.h>

namespace ui {

static M5Canvas* s_canvas = nullptr;
static bool s_wantCanvas = true;

// A 16-bit full-screen sprite is 64,800 bytes. That is affordable at rest but
// not while a multi-second audio buffer is live, so it is releasable.
static const size_t CANVAS_BYTES = (size_t)SCREEN_W * SCREEN_H * 2;
static const size_t CANVAS_HEADROOM = 40 * 1024;   // leave room for TLS

lgfx::LGFXBase& gfx() {
    if (s_canvas) return *s_canvas;
    return M5Cardputer.Display;
}
bool canvasActive() { return s_canvas != nullptr; }

void acquireCanvas() {
    if (s_canvas || !s_wantCanvas) return;
    if (ESP.getFreeHeap() < CANVAS_BYTES + CANVAS_HEADROOM) return;
    auto* cv = new M5Canvas(&M5Cardputer.Display);
    cv->setColorDepth(16);
    cv->setPsram(false);
    if (!cv->createSprite(SCREEN_W, SCREEN_H)) { delete cv; return; }
    cv->setTextWrap(false);
    s_canvas = cv;
    audio::setReclaimableBytes(CANVAS_BYTES);
}

void releaseCanvas() {
    if (!s_canvas) return;
    s_canvas->deleteSprite();
    delete s_canvas;
    s_canvas = nullptr;
    audio::setReclaimableBytes(0);
}

void begin() {
    M5Cardputer.Display.setTextWrap(false);
    acquireCanvas();
    if (!s_canvas) os::logf("ui: no canvas (%u KB free) - drawing direct",
                            (unsigned)(ESP.getFreeHeap() / 1024));
}

void beginFrame() {
    auto& g = gfx();
    g.setTextSize(theme::bigText() ? 2 : 1);
    g.fillScreen(c().bg);
}

void endFrame() {
    if (s_canvas) s_canvas->pushSprite(0, 0);
}

// ---------------- primitives ----------------
static int glyphW() { return theme::bigText() ? 12 : 6; }

void text(int x, int y, const String& s, uint16_t color) {
    auto& g = gfx();
    g.setTextColor(color);
    g.setCursor(x, y);
    g.print(s);
}

void textBg(int x, int y, const String& s, uint16_t color, uint16_t bg) {
    auto& g = gfx();
    g.setTextColor(color, bg);
    g.setCursor(x, y);
    g.print(s);
}

// Counted in characters, not bytes: a transcript with an accent in it would
// otherwise measure wider than it draws and centre itself off to the left.
int textW(const String& s) { return utf8Len(s) * glyphW(); }

void centered(int y, const String& s, uint16_t color) {
    int x = (SCREEN_W - textW(s)) / 2;
    text(x < 0 ? 0 : x, y, s, color);
}

void hline(int y, uint16_t color) { gfx().drawFastHLine(0, y, SCREEN_W, color); }

void panel(int x, int y, int w, int h, uint16_t fill, int radius) {
    gfx().fillRoundRect(x, y, w, h, radius, fill);
}

void outline(int x, int y, int w, int h, uint16_t color, int radius) {
    gfx().drawRoundRect(x, y, w, h, radius, color);
}

void progress(int x, int y, int w, int h, float frac, uint16_t color) {
    auto& g = gfx();
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    g.drawRoundRect(x, y, w, h, h / 2, c().border);
    int inner = (int)((w - 2) * frac);
    if (inner > 0) g.fillRoundRect(x + 1, y + 1, inner, h - 2, (h - 2) / 2, color);
}

void badge(int x, int y, const String& label, uint16_t fg, uint16_t bg) {
    int w = textW(label) + 8;
    gfx().fillRoundRect(x, y - 1, w, glyphW() > 6 ? 18 : 11, 3, bg);
    text(x + 4, y + (glyphW() > 6 ? 1 : 1), label, fg);
}

uint16_t batteryColor(int percent) {
    // Below a fifth there is only one useful colour, and it is not a gradient.
    if (percent < 20) return c().bad;
    // Amber at 20 through green at 100. hueColor's wheel puts red at 0, yellow
    // near 43 and green near 85.
    int hue = 25 + (percent - 20) * (85 - 25) / 80;
    return theme::hueColor((uint8_t)hue, 255, 235);
}

void batteryGauge(int x, int y, int w, int h, int percent, bool charging) {
    auto& g = gfx();
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;

    // A charging bolt to the left of the shell rather than over the fill: at
    // nine pixels tall there is no room to punch one through the bar and still
    // have it read as a bolt, and at a low charge it would sit on empty space.
    if (charging) {
        uint16_t bolt = batteryColor(percent);
        int bx = x - 5, by = y + 1;
        g.fillTriangle(bx + 3, by, bx, by + 4, bx + 3, by + 4, bolt);
        g.fillTriangle(bx + 1, by + h - 2, bx + 4, by + 3, bx + 1, by + 3, bolt);
    }

    g.drawRoundRect(x, y, w, h, 2, c().border);
    g.fillRect(x + w, y + (h - 3) / 2, 2, 3, c().border);      // terminal nub

    const int inner = w - 4;
    int fill = (inner * percent + 50) / 100;
    // Keep a sliver at 1-2%, so "nearly flat" still reads as a battery with
    // something in it rather than as an empty outline.
    if (percent > 0 && fill < 1) fill = 1;
    if (fill > 0) g.fillRect(x + 2, y + 2, fill, h - 4, batteryColor(percent));
}

// Icons are drawn from primitives rather than bitmaps: no flash cost, and they
// recolour with the theme for free.
void icon(int x, int y, Icon id, uint16_t col) {
    auto& g = gfx();
    switch (id) {
        case Icon::Home:
            g.fillTriangle(x + 4, y, x, y + 4, x + 8, y + 4, col);
            g.drawRect(x + 1, y + 4, 7, 5, col);
            break;
        case Icon::Note:
            g.drawRect(x + 1, y, 7, 9, col);
            g.drawFastHLine(x + 3, y + 3, 3, col);
            g.drawFastHLine(x + 3, y + 5, 3, col);
            break;
        case Icon::Mic:
            g.fillRoundRect(x + 3, y, 3, 6, 1, col);
            g.drawFastHLine(x + 1, y + 6, 7, col);
            g.drawFastVLine(x + 4, y + 6, 3, col);
            break;
        case Icon::Chat:
            g.drawRoundRect(x, y, 9, 7, 2, col);
            g.fillTriangle(x + 2, y + 7, x + 5, y + 7, x + 2, y + 10, col);
            break;
        case Icon::Code:
            g.drawLine(x + 3, y + 1, x, y + 4, col);
            g.drawLine(x, y + 4, x + 3, y + 7, col);
            g.drawLine(x + 6, y + 1, x + 9, y + 4, col);
            g.drawLine(x + 9, y + 4, x + 6, y + 7, col);
            break;
        case Icon::Wifi:
            for (int i = 0; i < 3; i++) g.drawFastHLine(x + i, y + i * 2, 9 - i * 2, col);
            g.drawPixel(x + 4, y + 7, col);
            break;
        case Icon::Folder:
            g.drawFastHLine(x, y + 1, 4, col);
            g.drawRect(x, y + 2, 9, 7, col);
            break;
        case Icon::Gear:
            g.drawCircle(x + 4, y + 4, 3, col);
            g.drawPixel(x + 4, y, col);     g.drawPixel(x + 4, y + 8, col);
            g.drawPixel(x, y + 4, col);     g.drawPixel(x + 8, y + 4, col);
            break;
        case Icon::Bluetooth:
            g.drawLine(x + 4, y, x + 7, y + 3, col);
            g.drawLine(x + 7, y + 3, x + 1, y + 7, col);
            g.drawLine(x + 4, y, x + 4, y + 9, col);
            g.drawLine(x + 4, y + 9, x + 7, y + 6, col);
            g.drawLine(x + 7, y + 6, x + 1, y + 2, col);
            break;
        case Icon::Check:
            g.drawLine(x + 1, y + 4, x + 3, y + 7, col);
            g.drawLine(x + 3, y + 7, x + 8, y + 1, col);
            break;
        case Icon::Cross:
            g.drawLine(x + 1, y + 1, x + 8, y + 8, col);
            g.drawLine(x + 8, y + 1, x + 1, y + 8, col);
            break;
        case Icon::Lock:
            g.drawRoundRect(x + 2, y, 5, 4, 2, col);
            g.fillRect(x, y + 4, 9, 5, col);
            break;
        case Icon::Cloud:
            g.fillCircle(x + 3, y + 5, 3, col);
            g.fillCircle(x + 6, y + 5, 3, col);
            g.fillRect(x + 3, y + 4, 4, 4, col);
            break;
        case Icon::Chip:
            g.drawRect(x + 1, y + 1, 7, 7, col);
            g.drawRect(x + 3, y + 3, 3, 3, col);
            break;
        case Icon::Clock:
            g.drawCircle(x + 4, y + 4, 4, col);
            g.drawLine(x + 4, y + 4, x + 4, y + 2, col);
            g.drawLine(x + 4, y + 4, x + 6, y + 4, col);
            break;
        case Icon::Battery:
            g.drawRect(x, y + 2, 8, 5, col);
            g.drawFastVLine(x + 8, y + 3, 3, col);
            break;
        case Icon::Arrow:
            g.drawLine(x, y + 4, x + 8, y + 4, col);
            g.drawLine(x + 8, y + 4, x + 5, y + 1, col);
            g.drawLine(x + 8, y + 4, x + 5, y + 7, col);
            break;
        default: break;
    }
}

// ---------------- chrome ----------------
void statusBar(const String& title, Icon id, uint16_t accent) {
    auto& g = gfx();
    uint16_t acc = accent ? accent : c().accent;
    g.fillRect(0, 0, SCREEN_W, STATUS_H, c().surface);
    g.setTextSize(1);   // the bar stays compact even in big-text mode

    if (os::toastActive()) {
        uint16_t tone = os::toastTone() == os::Tone::Bad  ? c().bad
                      : os::toastTone() == os::Tone::Good ? c().good
                                                          : c().warn;
        g.fillRect(0, 0, SCREEN_W, STATUS_H, tone);
        g.setTextColor(c().bg);
        g.setCursor(3, 2);
        g.print(ellipsize(os::toastText(), 39).c_str());
        g.drawFastHLine(0, STATUS_H, SCREEN_W, tone);
        g.setTextSize(theme::bigText() ? 2 : 1);
        return;
    }

    int x = 3;
    if (id != Icon::None) { icon(x, 1, id, acc); x += 12; }
    g.setTextColor(c().fg);
    g.setCursor(x, 2);
    g.print(ellipsize(title, 26).c_str());

    // Right cluster, laid out from the right edge inward.
    int rx = SCREEN_W - 3;
    const hw::Battery& batt = hw::battery();
    if (batt.known) {
        const int bw = 20, bh = 9;
        rx -= bw + 2;                                  // shell plus the nub
        batteryGauge(rx, 1, bw, bh, batt.percent, batt.charging);
        rx -= batt.charging ? 10 : 5;                  // room for the bolt
    }
    if (bt::active()) { rx -= 10; icon(rx, 1, Icon::Bluetooth,
                                      bt::connected() ? c().accent2 : c().dim); }
    if (net::connected()) {
        rx -= 11;
        int bars = net::signalBars();
        for (int i = 0; i < 4; i++)
            g.fillRect(rx + i * 2, 8 - i * 2, 1, 1 + i * 2, i < bars ? c().good : c().border);
    } else {
        rx -= 8;
        icon(rx, 1, Icon::Cross, c().border);
    }
    if (theme::statusClock() && net::timeValid()) {
        String t = net::clockString();
        rx -= (int)t.length() * 6 + 5;
        g.setTextColor(c().dim);
        g.setCursor(rx, 2);
        g.print(t.c_str());
    }

    g.drawFastHLine(0, STATUS_H, SCREEN_W, acc);
    g.setTextSize(theme::bigText() ? 2 : 1);
}

void hint(const String& t) {
    if (!theme::showHints()) return;
    auto& g = gfx();
    g.setTextSize(1);
    g.fillRect(0, HINT_Y - 3, SCREEN_W, SCREEN_H - HINT_Y + 3, c().surface);
    g.drawFastHLine(0, HINT_Y - 3, SCREEN_W, c().border);
    g.setTextColor(c().dim);
    g.setCursor(3, HINT_Y);
    g.print(ellipsize(t, 39).c_str());
    g.setTextSize(theme::bigText() ? 2 : 1);
}

// ---------------- unicode ----------------
// efontCN_16: ~311KB of flash for Cyrillic (66 glyphs), Greek (48), hiragana
// and katakana (170) and 6764 CJK ideographs, on top of ASCII. That covers
// every language Translate offers except Korean, Arabic and Hindi, which fall
// back to a romanisation -- see renderable().
static const lgfx::IFont* unicodeFont() { return &lgfx::fonts::efontCN_16; }

UnicodeScope::UnicodeScope() : prev_(gfx().getFont()), active_(true) {
    auto& g = gfx();
    g.setFont(unicodeFont());
    g.setTextSize(1);              // the efont is already 16px tall
}

UnicodeScope::~UnicodeScope() {
    if (!active_) return;
    auto& g = gfx();
    g.setFont((const lgfx::IFont*)prev_);
    g.setTextSize(theme::bigText() ? 2 : 1);
}

bool canRender(uint32_t cp) {
    if (cp < 0x80) return true;                  // the default font has ASCII
    if (cp > 0xFFFF) return false;               // efont is BMP only
    lgfx::FontMetrics fm;
    const lgfx::IFont* f = unicodeFont();
    f->getDefaultMetric(&fm);
    return f->updateFontMetric(&fm, (uint16_t)cp);
}

bool isAscii(const String& utf8) {
    for (size_t i = 0; i < utf8.length(); i++)
        if ((uint8_t)utf8[i] >= 0x80) return false;
    return true;
}

bool renderable(const String& utf8) {
    int i = 0;
    while (i < (int)utf8.length()) {
        uint32_t cp = utf8Decode(utf8, i);
        // Whitespace and the replacement character are never the reason to
        // give up on a whole translation.
        if (cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xFFFD) continue;
        if (!canRender(cp)) return false;
    }
    return true;
}

uint32_t utf8Decode(const String& s, int& i) {
    const int n = s.length();
    if (i < 0 || i >= n) { i = n; return 0; }
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) { i += 1; return c; }

    int extra;
    uint32_t cp;
    if      ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else { i += 1; return 0xFFFD; }              // stray continuation byte

    if (i + extra >= n) { i = n; return 0xFFFD; }
    for (int k = 1; k <= extra; k++) {
        uint8_t cc = (uint8_t)s[i + k];
        if ((cc & 0xC0) != 0x80) { i += 1; return 0xFFFD; }   // truncated
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += extra + 1;
    return cp;
}

int utf8Len(const String& s) {
    int i = 0, n = 0;
    while (i < (int)s.length()) { utf8Decode(s, i); n++; }
    return n;
}

String utf8Sub(const String& s, int fromCp, int cpCount) {
    int i = 0, cp = 0;
    while (i < (int)s.length() && cp < fromCp) { utf8Decode(s, i); cp++; }
    int start = i;
    int taken = 0;
    while (i < (int)s.length() && taken < cpCount) { utf8Decode(s, i); taken++; }
    return s.substring(start, i);
}

// Proportional fonts cannot be wrapped by counting characters. Measure with
// whatever font is active -- callers wrap this in a UnicodeScope.
std::vector<String> wrapPx(const String& src, int maxPx) {
    std::vector<String> out;
    auto& g = gfx();
    int i = 0;
    const int n = src.length();
    int lineStart = 0, lastBreak = -1, width = 0;

    while (i < n) {
        int charStart = i;
        uint32_t cp = utf8Decode(src, i);
        if (cp == '\n') {
            out.push_back(src.substring(lineStart, charStart));
            lineStart = i; lastBreak = -1; width = 0;
            continue;
        }
        if (cp == '\r') continue;

        int w = g.textWidth(src.substring(charStart, i).c_str());
        // CJK has no spaces, so every character is also a legal break point.
        bool breakable = (cp == ' ') || (cp >= 0x2E80);
        if (width + w > maxPx && charStart > lineStart) {
            int cut = (lastBreak > lineStart) ? lastBreak : charStart;
            out.push_back(src.substring(lineStart, cut));
            // Drop the space we broke on; keep a CJK character.
            lineStart = (cut < n && src[cut] == ' ') ? cut + 1 : cut;
            lastBreak = -1;
            width = 0;
            // Rescan from the new line's start, not from the character that
            // overflowed: breaking at an earlier space leaves everything
            // between it and here unmeasured, and the next line then runs off
            // the panel. lineStart always advances, so this terminates.
            i = lineStart;
            continue;
        }
        width += w;
        if (breakable) lastBreak = (cp == ' ') ? charStart : i;
    }
    if (lineStart < n || out.empty()) out.push_back(src.substring(lineStart));
    return out;
}

int unicodePager(const String& body, int scroll, uint16_t color,
                 int rows, int y0, int lineH) {
    UnicodeScope u;
    auto lines = wrapPx(body, SCREEN_W - 8);
    if (scroll > (int)lines.size() - 1) scroll = max(0, (int)lines.size() - 1);
    if (scroll < 0) scroll = 0;
    auto& g = gfx();
    g.setTextColor(color);
    for (int r = 0; r < rows && scroll + r < (int)lines.size(); r++) {
        g.setCursor(3, y0 + r * lineH);
        g.print(lines[scroll + r].c_str());
    }
    if ((int)lines.size() > rows) {
        int trackH = rows * lineH;
        int barH = max(6, trackH * rows / (int)lines.size());
        int span = (int)lines.size() - rows;
        int barY = y0 - 1 + (span > 0 ? (trackH - barH) * scroll / span : 0);
        g.fillRoundRect(SCREEN_W - 3, barY, 3, barH, 1, c().accent);
    }
    return (int)lines.size();
}

// ---------------- text ----------------
std::vector<String> wrap(const String& src, int maxChars) {
    if (maxChars < 0) maxChars = theme::charsPerLine();
    std::vector<String> out;
    int i = 0, len = src.length();
    while (i < len) {
        int nl = -1;
        for (int j = i; j < i + maxChars && j < len; j++)
            if (src[j] == '\n') { nl = j; break; }
        if (nl >= 0) { out.push_back(src.substring(i, nl)); i = nl + 1; continue; }
        int remaining = len - i;
        int take = remaining < maxChars ? remaining : maxChars;
        if (take == remaining) { out.push_back(src.substring(i)); break; }
        int brk = -1;
        for (int j = i + take; j > i; j--) if (src[j] == ' ') { brk = j; break; }
        if (brk < 0) brk = i + take;
        out.push_back(src.substring(i, brk));
        i = brk;
        while (i < len && src[i] == ' ') i++;
    }
    if (out.empty()) out.push_back("");
    return out;
}

String ellipsize(const String& s, int maxChars) {
    if (maxChars <= 1) return s;
    // substring() on a byte index cuts multi-byte characters in half and turns
    // the tail of a transcript into mojibake.
    int len = utf8Len(s);
    if (len <= maxChars) return s;
    return utf8Sub(s, 0, maxChars - 1) + "~";
}

String firstLine(const String& src, int maxChars) {
    if (maxChars < 0) maxChars = theme::charsPerLine();
    int nl = src.indexOf('\n');
    String first = (nl >= 0) ? src.substring(0, nl) : src;
    first.trim();
    while (first.startsWith("#")) first.remove(0, 1);
    first.trim();
    if (first.length() == 0) first = "(empty)";
    return ellipsize(first, maxChars);
}

// ---------------- widgets ----------------
int listView(const std::vector<Row>& rows, int selected, int scroll,
             int rows_visible, int y0) {
    if (rows_visible < 0) rows_visible = theme::bodyRows();
    if (y0 < 0) y0 = BODY_Y;
    const int rh = theme::rowHeight();

    if (selected < scroll) scroll = selected;
    if (selected >= scroll + rows_visible) scroll = selected - rows_visible + 1;
    if (scroll < 0) scroll = 0;

    bool scrollable = (int)rows.size() > rows_visible;
    int listW = scrollable ? SCREEN_W - 4 : SCREEN_W;

    for (int i = 0; i < rows_visible; i++) {
        int idx = scroll + i;
        int y = y0 + i * rh;
        if (idx >= (int)rows.size()) continue;
        const Row& r = rows[idx];
        bool sel = idx == selected;
        if (sel) {
            panel(1, y - 1, listW - 2, rh, c().selbg, 3);
            gfx().fillRect(1, y - 1, 2, rh, c().accent);
        }
        int x = 6;
        if (r.icon != Icon::None) {
            icon(x, y + (rh - 9) / 2, r.icon, sel ? c().accent : c().dim);
            x += 12;
        }
        uint16_t fg = sel ? c().selfg : (r.tint ? r.tint : c().fg);
        int room = (listW - x - 6) / (theme::bigText() ? 12 : 6);
        if (r.detail.length()) room -= (int)r.detail.length() + 1;
        text(x, y + (rh - (theme::bigText() ? 16 : 8)) / 2, ellipsize(r.label, max(1, room)), fg);
        if (r.detail.length()) {
            int dx = listW - 6 - textW(r.detail);
            text(dx, y + (rh - (theme::bigText() ? 16 : 8)) / 2, r.detail,
                 sel ? c().selfg : c().dim);
        }
    }

    if (scrollable) {
        int trackY = y0 - 1, trackH = rows_visible * rh;
        gfx().fillRect(SCREEN_W - 3, trackY, 3, trackH, c().surface);
        int barH = max(6, trackH * rows_visible / (int)rows.size());
        int span = (int)rows.size() - rows_visible;
        int barY = trackY + (span > 0 ? (trackH - barH) * scroll / span : 0);
        gfx().fillRoundRect(SCREEN_W - 3, barY, 3, barH, 1, c().accent);
    }
    return scroll;
}

int listView(const std::vector<String>& items, int selected, int scroll,
             int rows_visible, int y0) {
    std::vector<Row> rows;
    rows.reserve(items.size());
    for (auto& s : items) rows.push_back({s, "", Icon::None, 0});
    return listView(rows, selected, scroll, rows_visible, y0);
}

int pagerLines(const String& body) { return (int)wrap(body).size(); }

void pager(const String& body, int scroll, uint16_t color, int rows, int y0) {
    if (rows < 0) rows = theme::bodyRows();
    if (y0 < 0) y0 = BODY_Y;
    auto lines = wrap(body);
    const int rh = theme::rowHeight();
    if (scroll > (int)lines.size() - 1) scroll = max(0, (int)lines.size() - 1);
    if (scroll < 0) scroll = 0;
    for (int i = 0; i < rows && scroll + i < (int)lines.size(); i++)
        text(3, y0 + i * rh, lines[scroll + i], color);
    if ((int)lines.size() > rows) {
        int trackH = rows * rh;
        int barH = max(6, trackH * rows / (int)lines.size());
        int span = (int)lines.size() - rows;
        int barY = y0 - 1 + (span > 0 ? (trackH - barH) * scroll / span : 0);
        gfx().fillRoundRect(SCREEN_W - 3, barY, 3, barH, 1, c().accent);
    }
}

void inputLine(int y, const String& prompt, const String& value,
               uint16_t color, bool caret) {
    auto& g = gfx();
    const int h = theme::bigText() ? 20 : 13;
    panel(2, y - 2, SCREEN_W - 4, h, c().surface, 3);
    outline(2, y - 2, SCREEN_W - 4, h, c().border, 3);
    int room = (SCREEN_W - 12) / (theme::bigText() ? 12 : 6) - (int)prompt.length() - 1;
    String show = value;
    if ((int)show.length() > room) show = show.substring(show.length() - room);
    text(6, y, prompt, c().accent);
    text(6 + textW(prompt), y, show, color);
    if (caret && (millis() / 450) % 2 == 0)
        g.fillRect(6 + textW(prompt) + textW(show), y, 2, theme::bigText() ? 16 : 8, c().accent);
}

// A rotating arc on a faint track. Eight discrete dots read as a blocky ring
// on a 240x135 panel; a swept arc reads as motion.
// Re-armed every time a spinner is drawn, and expires shortly after it stops
// being drawn, so an idle screen goes back to costing nothing.
static uint32_t s_animUntil = 0;

bool animating() { return millis() < s_animUntil; }

void spinner(int cx, int cy, uint16_t col) {
    s_animUntil = millis() + 250;
    auto& g = gfx();
    const int rOut = 12, rIn = 8;

    g.fillArc(cx, cy, rIn, rOut, 0, 359.9f, c().surface);   // track
    g.drawArc(cx, cy, rIn, rOut, 0, 359.9f, c().border);    // rim, so it reads as a ring

    float head = fmodf(millis() * 0.30f, 360.0f);           // ~50 rpm

    // A dim lead-in behind the bright head gives the sweep a direction.
    auto arc = [&](float from, float len, uint16_t colour) {
        float a0 = fmodf(from + 360.0f, 360.0f);
        float a1 = a0 + len;
        if (a1 <= 360.0f) { g.fillArc(cx, cy, rIn, rOut, a0, a1, colour); return; }
        g.fillArc(cx, cy, rIn, rOut, a0, 360.0f, colour);   // fillArc will not wrap
        g.fillArc(cx, cy, rIn, rOut, 0.0f, a1 - 360.0f, colour);
    };
    arc(head - 70.0f, 70.0f, c().dim);
    arc(head, 55.0f, col);
}

bool editBuffer(String& buf, const KeyEvent& k, size_t maxLen, bool multiline) {
    if (k.del) {
        if (buf.length() == 0) return false;
        // ctrl+backspace deletes the previous word, which matters a lot on a
        // 56-key thumb keyboard.
        if (k.ctrl) {
            while (buf.length() && buf[buf.length() - 1] == ' ') buf.remove(buf.length() - 1);
            while (buf.length() && buf[buf.length() - 1] != ' ') buf.remove(buf.length() - 1);
        } else {
            buf.remove(buf.length() - 1);
        }
        return true;
    }
    if (k.enter && multiline) {
        if (buf.length() >= maxLen) return false;
        buf += '\n';
        return true;
    }
    bool changed = false;
    for (char ch : k.chars) {
        if (ch == '`') continue;
        if (buf.length() >= maxLen) break;
        buf += ch;
        changed = true;
    }
    return changed;
}

// ---------------- modals ----------------
static KeyEvent waitKey() {
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())
            return os::readKey();
        delay(12);
    }
}

bool confirm(const String& question) {
    while (true) {
        beginFrame();
        auto lines = wrap(question);
        int top = 26;
        panel(6, top - 6, SCREEN_W - 12, (int)lines.size() * theme::rowHeight() + 14,
              c().surface, 5);
        outline(6, top - 6, SCREEN_W - 12, (int)lines.size() * theme::rowHeight() + 14,
                c().warn, 5);
        for (size_t i = 0; i < lines.size() && i < 5; i++)
            text(12, top + i * theme::rowHeight(), lines[i], c().fg);
        statusBar("Confirm", Icon::Cross, c().warn);
        hint("Y confirm     N / ` cancel");
        endFrame();

        KeyEvent k = waitKey();
        if (k.is('y')) return true;
        if (k.is('n') || k.esc) return false;
    }
}

int chooser(const String& title, const std::vector<String>& options, int initial) {
    if (options.empty()) return -1;
    int sel = constrain(initial, 0, (int)options.size() - 1), scroll = 0;
    while (true) {
        beginFrame();
        scroll = listView(options, sel, scroll);
        statusBar(title, Icon::Arrow);
        hint("Enter select     ` cancel");
        endFrame();

        KeyEvent k = waitKey();
        if (k.esc) return -1;
        if (k.enter) return sel;
        if (k.up   || k.is('k')) sel = (sel - 1 + options.size()) % options.size();
        if (k.down || k.is('j')) sel = (sel + 1) % options.size();
        for (char ch : k.chars)
            if (ch >= '1' && ch <= '9' && (ch - '1') < (int)options.size()) return ch - '1';
    }
}

String prompt(const String& title, const String& initial, size_t maxLen) {
    String buf = initial;
    while (true) {
        beginFrame();
        text(6, BODY_Y + 6, ellipsize(title, theme::charsPerLine()), c().dim);
        inputLine(BODY_Y + 30, "> ", buf, c().fg);
        text(6, BODY_Y + 54, String(buf.length()) + "/" + String((int)maxLen), c().dim);
        statusBar(title, Icon::Note);
        hint("Enter save     ` cancel     ctrl+Bksp word");
        endFrame();

        KeyEvent k = waitKey();
        if (k.esc)   return initial;
        if (k.enter) return buf;
        editBuffer(buf, k, maxLen);
    }
}

void splash(const String& line1, const String& line2) {
    beginFrame();
    gfx().setTextSize(2);
    int w = (int)line1.length() * 12;
    text((SCREEN_W - w) / 2, 44, line1, c().accent);
    gfx().setTextSize(1);
    if (line2.length()) centered(74, line2, c().dim);
    endFrame();
}

void busy(const String& message) {
    beginFrame();
    spinner(SCREEN_W / 2, 52, c().accent);
    centered(74, message, c().fg);
    statusBar(message, Icon::Cloud);
    endFrame();
}

namespace {
struct AwaitCtx {
    std::function<void()>* work;
    volatile bool done;
};

void awaitTrampoline(void* arg) {
    AwaitCtx* ctx = (AwaitCtx*)arg;
    (*ctx->work)();
    ctx->done = true;
    vTaskDelete(nullptr);
}
}  // namespace

void await(const String& message, std::function<void()> work) {
    AwaitCtx ctx{&work, false};
    TaskHandle_t handle = nullptr;
    // 16KB: a TLS handshake alone wants ~8-10KB of stack.
    BaseType_t ok = xTaskCreatePinnedToCore(awaitTrampoline, "await", 16384, &ctx,
                                            1, &handle, 0);
    if (ok != pdPASS) { busy(message); work(); return; }

    uint32_t t0 = millis();
    while (!ctx.done) {
        beginFrame();
        spinner(SCREEN_W / 2, 48, c().accent);
        centered(70, message, c().fg);
        uint32_t secs = (millis() - t0) / 1000;
        if (secs >= 2) centered(84, String(secs) + "s", c().dim);
        statusBar(message, Icon::Cloud);
        hint(secs > 20 ? "still working - do not unplug" : "working...");
        endFrame();
        delay(70);
    }
    // Give the worker a moment to actually unwind before its stack is reused.
    delay(20);
}

}  // namespace ui
