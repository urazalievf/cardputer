// User-owned look and feel. Everything here is editable from Settings and
// persisted in NVS, so the OS can be recoloured without a reflash.
#pragma once
#include <Arduino.h>

namespace theme {

struct Palette {
    const char* name;
    uint16_t bg;        // page background
    uint16_t surface;   // panels, cards, the status bar
    uint16_t border;    // hairlines
    uint16_t fg;        // primary text
    uint16_t dim;       // secondary text, hints
    uint16_t accent;    // selection, focus, the app's identity colour
    uint16_t accent2;   // secondary accent (assistant replies, links)
    uint16_t good;
    uint16_t warn;
    uint16_t bad;
    uint16_t selbg;     // selected row background
    uint16_t selfg;     // selected row text
};

inline constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void begin();                      // load from NVS

const Palette& cur();

int   presetCount();
const char* presetName(int i);
int   preset();
void  setPreset(int i);

// A custom accent overrides the preset's. hue is 0-255 around the colour wheel;
// 256 means "use the preset's own accent".
int   accentHue();                 // 0-255, or 256 for preset default
void  setAccentHue(int hue);
uint16_t hueColor(uint8_t hue, uint8_t sat = 255, uint8_t val = 255);

// Toggles
bool sounds();      void setSounds(bool v);
bool showHints();   void setShowHints(bool v);
bool statusClock(); void setStatusClock(bool v);
bool bigText();     void setBigText(bool v);   // 8px rows -> 10px, fewer of them
uint8_t brightness(); void setBrightness(uint8_t v);

// --- per-role colour editing ---
// Editing any colour clones the active preset into a "Custom" slot and
// switches to it, so a preset is never destroyed by experimenting.
int         colorRoleCount();
const char* colorRoleName(int i);
uint16_t    colorRole(int i);
void        setColorRole(int i, uint16_t c);
bool        isCustom();
void        resetCustomToPreset(int presetIndex);

// RGB565 <-> HSV, for the picker.
void     toHsv(uint16_t c, uint8_t& h, uint8_t& s, uint8_t& v);
uint16_t fromHsv(uint8_t h, uint8_t s, uint8_t v);

int  rowHeight();      // follows bigText
int  bodyRows();       // how many list rows fit
int  charsPerLine();

}  // namespace theme
