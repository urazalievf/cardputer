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

static Palette s_custom;
static bool s_customLoaded = false;
static Palette s_cur;

// Field order used by the colour editor and by the NVS blob.
static uint16_t Palette::* const ROLES[] = {
    &Palette::bg, &Palette::surface, &Palette::border, &Palette::fg, &Palette::dim,
    &Palette::accent, &Palette::accent2, &Palette::good, &Palette::warn,
    &Palette::bad, &Palette::selbg, &Palette::selfg,
};
static const char* ROLE_NAMES[] = {
    "Background", "Panels", "Lines", "Text", "Dim text",
    "Accent", "Accent 2", "Good", "Warning", "Bad",
    "Selected bg", "Selected text",
};
static const int N_ROLES = sizeof(ROLES) / sizeof(ROLES[0]);
static int  s_preset = 0;
static int  s_hue = 256;
static bool s_sounds = true, s_hints = true, s_clock = true, s_big = false;
static uint8_t s_bright = 120;

static void saveCustom() {
    String blob;
    for (int i = 0; i < N_ROLES; i++) {
        if (i) blob += ",";
        blob += String((uint32_t)(s_custom.*ROLES[i]));
    }
    store::setStr("thcustom", blob);
}

static void loadCustom() {
    if (s_customLoaded) return;
    s_customLoaded = true;
    s_custom = PRESETS[0];
    s_custom.name = "Custom";
    String blob = store::getStr("thcustom", "");
    if (!blob.length()) return;
    int from = 0, i = 0;
    while (from < (int)blob.length() && i < N_ROLES) {
        int comma = blob.indexOf(',', from);
        if (comma < 0) comma = blob.length();
        s_custom.*ROLES[i] = (uint16_t)blob.substring(from, comma).toInt();
        from = comma + 1;
        i++;
    }
}

bool isCustom() { return s_preset == N_PRESETS; }

static void rebuild() {
    loadCustom();
    if (s_preset == N_PRESETS) s_cur = s_custom;
    else s_cur = PRESETS[constrain(s_preset, 0, N_PRESETS - 1)];
    if (s_hue >= 0 && s_hue <= 255) s_cur.accent = hueColor((uint8_t)s_hue);
}

int colorRoleCount() { return N_ROLES; }
const char* colorRoleName(int i) { return ROLE_NAMES[constrain(i, 0, N_ROLES - 1)]; }
uint16_t colorRole(int i) { return s_cur.*ROLES[constrain(i, 0, N_ROLES - 1)]; }

void resetCustomToPreset(int presetIndex) {
    loadCustom();
    s_custom = PRESETS[constrain(presetIndex, 0, N_PRESETS - 1)];
    s_custom.name = "Custom";
    saveCustom();
    rebuild();
}

void setColorRole(int i, uint16_t c) {
    loadCustom();
    // First edit from a preset clones it, so presets stay pristine.
    if (s_preset != N_PRESETS) {
        s_custom = PRESETS[constrain(s_preset, 0, N_PRESETS - 1)];
        s_custom.name = "Custom";
        if (s_hue >= 0 && s_hue <= 255) s_custom.accent = hueColor((uint8_t)s_hue);
        s_preset = N_PRESETS;
        store::setInt("thpreset", s_preset);
        s_hue = 256;                     // the custom accent supersedes the hue knob
        store::setInt("thhue", s_hue);
    }
    s_custom.*ROLES[constrain(i, 0, N_ROLES - 1)] = c;
    saveCustom();
    rebuild();
}

void toHsv(uint16_t c, uint8_t& h, uint8_t& s, uint8_t& v) {
    int r = ((c >> 11) & 0x1F) * 255 / 31;
    int g = ((c >> 5) & 0x3F) * 255 / 63;
    int b = (c & 0x1F) * 255 / 31;
    int mx = max(r, max(g, b)), mn = min(r, min(g, b));
    v = mx;
    s = mx == 0 ? 0 : (mx - mn) * 255 / mx;
    if (mx == mn) { h = 0; return; }
    int d = mx - mn, hue;
    if (mx == r)      hue = 43 * (g - b) / d + (g < b ? 256 : 0);
    else if (mx == g) hue = 43 * (b - r) / d + 85;
    else              hue = 43 * (r - g) / d + 171;
    h = (uint8_t)((hue + 256) % 256);
}

uint16_t fromHsv(uint8_t h, uint8_t s, uint8_t v) { return hueColor(h, s, v); }

void begin() {
    s_preset = constrain(store::getInt("thpreset", 0), 0, N_PRESETS);
    s_hue    = store::getInt("thhue", 256);
    s_sounds = store::getInt("thsound", 1) != 0;
    s_hints  = store::getInt("thhints", 1) != 0;
    s_clock  = store::getInt("thclock", 1) != 0;
    s_big    = store::getInt("thbig", 0) != 0;
    s_bright = (uint8_t)constrain(store::getInt("bright", 120), 10, 255);
    rebuild();
}

const Palette& cur() { return s_cur; }

// The Custom slot is presented as one more preset at the end of the list.
int presetCount() { return N_PRESETS + 1; }
const char* presetName(int i) {
    return i >= N_PRESETS ? "Custom" : PRESETS[constrain(i, 0, N_PRESETS - 1)].name;
}
int preset() { return s_preset; }
void setPreset(int i) {
    int n = N_PRESETS + 1;
    s_preset = ((i % n) + n) % n;
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
