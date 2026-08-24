// Drawing primitives shared by every app.
#pragma once
#include "os.h"

namespace ui {

// Palette (RGB565). Dark, high-contrast, readable on the 1.14" panel.
static const uint16_t BG      = 0x0000;
static const uint16_t FG      = 0xFFFF;
static const uint16_t DIM     = 0x8410;
static const uint16_t ACCENT  = 0x07FF;  // cyan
static const uint16_t GOOD    = 0x07E0;  // green
static const uint16_t WARN    = 0xFFE0;  // yellow
static const uint16_t BAD     = 0xF800;  // red
static const uint16_t SELBG   = 0x03EF;  // dark cyan
static const uint16_t VIOLET  = 0xF81F;

M5GFX& gfx();

void clear();
void statusBar(const char* title);
void hint(const String& text);
void text(int x, int y, const String& s, uint16_t color = FG, uint16_t bg = BG);
void centered(int y, const String& s, uint16_t color = FG);
void rowHighlight(int y);

// Word-wrap `src` to `maxChars`, honouring embedded newlines.
std::vector<String> wrap(const String& src, int maxChars = CHARS_PER_LINE);
String firstLine(const String& src, int maxChars = CHARS_PER_LINE);
String ellipsize(const String& s, int maxChars);

// A scrolling list. Returns the (possibly adjusted) scroll offset.
int drawList(const std::vector<String>& items, int selected, int scroll,
             int rows = BODY_ROWS, int y0 = BODY_Y);

// A one-line text input rendered at `y`, right-scrolled to keep the caret visible.
void inputLine(int y, const String& prompt, const String& value,
               uint16_t color = FG, bool caret = true);

// Blocking modal helpers.
void splash(const String& line1, const String& line2 = "");
bool confirm(const String& question);   // Y / N, blocks until answered

// Feed a key into a text buffer. Returns true if the buffer changed.
bool editBuffer(String& buf, const KeyEvent& k, size_t maxLen, bool multiline = false);

}  // namespace ui
