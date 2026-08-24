#include "theme.h"
#include "store.h"

namespace theme {

// Presets are ordered from "what a terminal person expects" outward.
static const Palette PRESETS[] = {
    {"Midnight",
     rgb(8,10,16),   rgb(20,24,34),  rgb(48,56,74),  rgb(232,236,244),
     rgb(122,132,152), rgb(0,214,224), rgb(126,170,255),
     rgb(64,214,132), rgb(246,196,84), rgb(240,92,92),
     rgb(24,58,76),  rgb(236,252,255)},

    {"Terminal",
     rgb(0,0,0),     rgb(8,20,8),    rgb(0,84,0),    rgb(180,255,180),
     rgb(0,150,60),  rgb(0,255,90),  rgb(140,255,170),
     rgb(0,255,90),  rgb(220,240,80), rgb(255,80,60),
     rgb(0,72,28),   rgb(220,255,220)},

    {"Amber",
     rgb(12,8,0),    rgb(30,20,4),   rgb(96,64,8),   rgb(255,206,120),
     rgb(168,120,40), rgb(255,170,40), rgb(255,220,150),
     rgb(180,230,90), rgb(255,200,60), rgb(255,90,50),
     rgb(84,52,4),   rgb(255,232,180)},

    {"Nord",
     rgb(46,52,64),  rgb(59,66,82),  rgb(76,86,106), rgb(236,239,244),
     rgb(143,153,173), rgb(136,192,208), rgb(129,161,193),
     rgb(163,190,140), rgb(235,203,139), rgb(191,97,106),
     rgb(67,86,110), rgb(236,239,244)},

    {"Synthwave",
     rgb(16,8,32),   rgb(34,16,60),  rgb(88,40,140), rgb(240,230,255),
     rgb(150,110,200), rgb(255,60,180), rgb(90,220,255),
     rgb(80,240,180), rgb(255,210,90), rgb(255,70,110),
     rgb(72,24,110), rgb(255,240,255)},

    {"Paper",
     rgb(248,246,240), rgb(255,255,255), rgb(206,200,188), rgb(28,28,32),
     rgb(120,116,110), rgb(0,110,190), rgb(150,60,180),
     rgb(24,140,80), rgb(176,120,0), rgb(196,40,40),
     rgb(206,226,248), rgb(16,20,28)},

    {"Mono",
     rgb(0,0,0),     rgb(24,24,24),  rgb(72,72,72),  rgb(240,240,240),
     rgb(140,140,140), rgb(255,255,255), rgb(200,200,200),
     rgb(220,220,220), rgb(200,200,200), rgb(255,255,255),
     rgb(64,64,64),  rgb(255,255,255)},
};
static const int N_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

static Palette s_cur;
static int  s_preset = 0;
static int  s_hue = 256;
static bool s_sounds = true, s_hints = true, s_clock = true, s_big = false;
static uint8_t s_bright = 120;

static void rebuild() {
    s_cur = PRESETS[constrain(s_preset, 0, N_PRESETS - 1)];
    if (s_hue >= 0 && s_hue <= 255) s_cur.accent = hueColor((uint8_t)s_hue);
}

void begin() {
    s_preset = store::getInt("thpreset", 0);
    s_hue    = store::getInt("thhue", 256);
    s_sounds = store::getInt("thsound", 1) != 0;
    s_hints  = store::getInt("thhints", 1) != 0;
    s_clock  = store::getInt("thclock", 1) != 0;
    s_big    = store::getInt("thbig", 0) != 0;
    s_bright = (uint8_t)constrain(store::getInt("bright", 120), 10, 255);
    rebuild();
}

const Palette& cur() { return s_cur; }

int presetCount() { return N_PRESETS; }
const char* presetName(int i) { return PRESETS[constrain(i, 0, N_PRESETS - 1)].name; }
int preset() { return s_preset; }
void setPreset(int i) {
    s_preset = ((i % N_PRESETS) + N_PRESETS) % N_PRESETS;
    store::setInt("thpreset", s_preset);
    rebuild();
}

int accentHue() { return s_hue; }
void setAccentHue(int hue) {
    s_hue = (hue < 0 || hue > 255) ? 256 : hue;
    store::setInt("thhue", s_hue);
    rebuild();
}

// Cheap integer HSV -> RGB565. Good enough for a colour wheel picker.
uint16_t hueColor(uint8_t hue, uint8_t sat, uint8_t val) {
    uint8_t region = hue / 43;
    uint8_t rem = (hue - region * 43) * 6;
    uint8_t p = (val * (255 - sat)) >> 8;
    uint8_t q = (val * (255 - ((sat * rem) >> 8))) >> 8;
    uint8_t t = (val * (255 - ((sat * (255 - rem)) >> 8))) >> 8;
    switch (region) {
        case 0:  return rgb(val, t, p);
        case 1:  return rgb(q, val, p);
        case 2:  return rgb(p, val, t);
        case 3:  return rgb(p, q, val);
        case 4:  return rgb(t, p, val);
        default: return rgb(val, p, q);
    }
}

bool sounds() { return s_sounds; }
void setSounds(bool v) { s_sounds = v; store::setInt("thsound", v); }
bool showHints() { return s_hints; }
void setShowHints(bool v) { s_hints = v; store::setInt("thhints", v); }
bool statusClock() { return s_clock; }
void setStatusClock(bool v) { s_clock = v; store::setInt("thclock", v); }
bool bigText() { return s_big; }
void setBigText(bool v) { s_big = v; store::setInt("thbig", v); }

uint8_t brightness() { return s_bright; }
void setBrightness(uint8_t v) {
    s_bright = constrain((int)v, 10, 255);
    store::setInt("bright", s_bright);
}

int rowHeight()    { return s_big ? 16 : 10; }
int bodyRows()     { return s_big ? 6 : 9; }
int charsPerLine() { return s_big ? 19 : 38; }

}  // namespace theme
