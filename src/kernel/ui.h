// Rendering layer. Everything draws into an off-screen canvas that is pushed
// in one DMA transfer, so frames never tear or flash. If the canvas can't be
// allocated (or is released under memory pressure) drawing falls through to
// the panel directly and the OS keeps working, just with visible repaint.
#pragma once
#include "os.h"
#include "theme.h"
#include <functional>

namespace ui {

void begin();
lgfx::LGFXBase& gfx();          // canvas when available, panel otherwise
bool  canvasActive();
void  releaseCanvas();          // free ~64KB for audio buffers or TLS
void  acquireCanvas();          // take it back when the pressure is off

void beginFrame();              // clear to the page background
void endFrame();                // push the canvas to the panel

inline const theme::Palette& c() { return theme::cur(); }

// ---- primitives ----
void text(int x, int y, const String& s, uint16_t color);
void textBg(int x, int y, const String& s, uint16_t color, uint16_t bg);
void centered(int y, const String& s, uint16_t color);
int  textW(const String& s);
void icon(int x, int y, Icon id, uint16_t color);
void panel(int x, int y, int w, int h, uint16_t fill, int radius = 4);
void outline(int x, int y, int w, int h, uint16_t color, int radius = 4);
void hline(int y, uint16_t color);
void progress(int x, int y, int w, int h, float frac, uint16_t color);
void badge(int x, int y, const String& label, uint16_t fg, uint16_t bg);

// ---- chrome ----
// The status bar shows the app icon and title on the left, and live radio /
// battery / clock state on the right. Toasts temporarily take the whole strip.
void statusBar(const String& title, Icon id = Icon::None, uint16_t accent = 0);
void hint(const String& text);            // footer; honours theme::showHints()

// ---- widgets ----
struct Row {
    String label;
    String detail;                        // right-aligned, dimmed
    Icon   icon = Icon::None;
    uint16_t tint = 0;                    // 0 = default foreground
};
int listView(const std::vector<Row>& rows, int selected, int scroll,
             int rows_visible = -1, int y0 = -1);
int listView(const std::vector<String>& items, int selected, int scroll,
             int rows_visible = -1, int y0 = -1);

void pager(const String& body, int scroll, uint16_t color, int rows = -1, int y0 = -1);
int  pagerLines(const String& body);

void inputLine(int y, const String& prompt, const String& value,
               uint16_t color, bool caret = true);
void spinner(int cx, int cy, uint16_t color);   // animates off millis()

// True while a spinner was drawn recently. The kernel uses this to keep
// repainting on its own: rendering is lazy, so anything animated would
// otherwise freeze on whatever frame it was last drawn in.
bool animating();

// ---- unicode ----
// The built-in glyph set is ASCII and nothing else, so a Russian or Chinese
// translation renders as blanks. The embedded efont covers Cyrillic, Greek,
// kana and ~6.7k CJK ideographs, but it is 16px tall and variable width, so it
// is scoped to the run of text that needs it rather than switched on globally.
//
// Scoped: takes the font on construction, puts the old one back on scope exit,
// so an early return cannot leave the whole OS in a 16px font.
struct UnicodeScope {
    UnicodeScope();
    ~UnicodeScope();
private:
    const void* prev_;
    bool active_;
};

// Does the embedded font have a glyph for this codepoint? Asked of the font
// itself rather than assumed from a range table, so the answer stays correct
// if the font ever changes.
bool canRender(uint32_t cp);

// True when every codepoint in `utf8` can be drawn -- ASCII always can, the
// rest have to be in the efont. Callers use this to decide between showing the
// native script and showing a romanisation.
bool renderable(const String& utf8);

// True if the string is pure 7-bit ASCII, i.e. the default font is enough.
bool isAscii(const String& utf8);

// UTF-8 aware string handling. The Arduino String is a byte array: measuring,
// truncating and wrapping it by byte index cuts multi-byte characters in half
// and turns a valid translation into mojibake.
int      utf8Len(const String& s);                   // in codepoints
uint32_t utf8Decode(const String& s, int& byteIdx);  // advances byteIdx
String   utf8Sub(const String& s, int fromCp, int cpCount);

// Wrap to a pixel width using the font that is active right now, which is the
// only way to lay out a proportional font. Used for translated output.
std::vector<String> wrapPx(const String& src, int maxPx);

// Page through UTF-8 text in the embedded font. Returns how many lines it came
// to, so the caller can bound scrolling.
int unicodePager(const String& body, int scroll, uint16_t color,
                 int rows, int y0, int lineH = 17);

// ---- text helpers ----
std::vector<String> wrap(const String& src, int maxChars = -1);
String firstLine(const String& src, int maxChars = -1);
String ellipsize(const String& s, int maxChars);
bool   editBuffer(String& buf, const KeyEvent& k, size_t maxLen, bool multiline = false);

// ---- modals (block until answered) ----
bool confirm(const String& question);
int  chooser(const String& title, const std::vector<String>& options, int initial = 0);
String prompt(const String& title, const String& initial = "", size_t maxLen = 120);
void  splash(const String& line1, const String& line2 = "");
void  busy(const String& message);         // paints one frame; caller then blocks

// Run `work` on the other core and animate until it finishes. A blocking HTTP
// call on the UI core paints one frozen frame and looks exactly like a crash;
// this keeps the spinner and the elapsed counter alive so you can tell the
// difference. Falls back to running inline if the task cannot be created.
void  await(const String& message, std::function<void()> work);

}  // namespace ui
