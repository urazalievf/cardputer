// Rendering layer. Everything draws into an off-screen canvas that is pushed
// in one DMA transfer, so frames never tear or flash. If the canvas can't be
// allocated (or is released under memory pressure) drawing falls through to
// the panel directly and the OS keeps working, just with visible repaint.
#pragma once
#include "os.h"
#include "theme.h"

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

}  // namespace ui
