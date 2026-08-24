#include "ui.h"
#include "net.h"

namespace ui {

M5GFX& gfx() { return M5Cardputer.Display; }

void clear() { gfx().fillScreen(BG); }

void text(int x, int y, const String& s, uint16_t color, uint16_t bg) {
    gfx().setTextColor(color, bg);
    gfx().setCursor(x, y);
    gfx().print(s);
}

void centered(int y, const String& s, uint16_t color) {
    int x = (SCREEN_W - (int)s.length() * 6) / 2;
    if (x < 0) x = 0;
    text(x, y, s, color);
}

void rowHighlight(int y) { gfx().fillRect(0, y - 1, SCREEN_W, ROW_H, SELBG); }

void statusBar(const char* title) {
    gfx().fillRect(0, 0, SCREEN_W, STATUS_H, BG);
    text(2, 1, title, ACCENT);

    // Right side: wifi + battery. Toast text, when live, replaces the title.
    if (os::toastActive()) {
        gfx().fillRect(0, 0, SCREEN_W, STATUS_H - 1, BG);
        text(2, 1, ellipsize(os::toastText(), 30), WARN);
    }

    String right = net::connected() ? String(net::rssiBars()) : String("--");
    int batt = M5Cardputer.Power.getBatteryLevel();
    if (batt >= 0) right += " " + String(batt) + "%";
    int x = SCREEN_W - (int)right.length() * 6 - 2;
    text(x, 1, right, net::connected() ? GOOD : DIM);

    gfx().drawLine(0, STATUS_H, SCREEN_W, STATUS_H, DIM);
}

void hint(const String& t) {
    gfx().drawLine(0, HINT_Y - 3, SCREEN_W, HINT_Y - 3, DIM);
    gfx().fillRect(0, HINT_Y - 2, SCREEN_W, SCREEN_H - HINT_Y + 2, BG);
    text(2, HINT_Y, ellipsize(t, 39), DIM);
}

std::vector<String> wrap(const String& src, int maxChars) {
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

String firstLine(const String& src, int maxChars) {
    int nl = src.indexOf('\n');
    String first = (nl >= 0) ? src.substring(0, nl) : src;
    first.trim();
    if (first.startsWith("# ")) first = first.substring(2);
    if (first.length() == 0) first = "(empty)";
    return ellipsize(first, maxChars);
}

String ellipsize(const String& s, int maxChars) {
    if (maxChars <= 1 || (int)s.length() <= maxChars) return s;
    return s.substring(0, maxChars - 1) + "~";
}

int drawList(const std::vector<String>& items, int selected, int scroll, int rows, int y0) {
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + rows) scroll = selected - rows + 1;
    if (scroll < 0) scroll = 0;

    for (int i = 0; i < rows; i++) {
        int idx = scroll + i;
        int y = y0 + i * ROW_H;
        gfx().fillRect(0, y - 1, SCREEN_W, ROW_H, BG);
        if (idx >= (int)items.size()) continue;
        if (idx == selected) { rowHighlight(y); text(4, y, ellipsize(items[idx], 38), FG, SELBG); }
        else                 { text(4, y, ellipsize(items[idx], 38), FG, BG); }
    }
    // Scrollbar
    if ((int)items.size() > rows) {
        int trackH = rows * ROW_H;
        int barH = trackH * rows / items.size();
        if (barH < 4) barH = 4;
        int barY = y0 - 1 + (trackH - barH) * scroll / max(1, (int)items.size() - rows);
        gfx().fillRect(SCREEN_W - 2, y0 - 1, 2, trackH, BG);
        gfx().fillRect(SCREEN_W - 2, barY, 2, barH, DIM);
    }
    return scroll;
}

void inputLine(int y, const String& prompt, const String& value, uint16_t color, bool caret) {
    gfx().fillRect(0, y - 1, SCREEN_W, ROW_H + 1, BG);
    int room = CHARS_PER_LINE - prompt.length() - 1;
    String show = value;
    if ((int)show.length() > room) show = show.substring(show.length() - room);
    text(2, y, prompt + show + (caret ? "_" : ""), color);
}

void splash(const String& line1, const String& line2) {
    clear();
    centered(55, line1, FG);
    if (line2.length()) centered(70, line2, DIM);
}

bool confirm(const String& question) {
    clear();
    statusBar("Confirm");
    auto lines = wrap(question, CHARS_PER_LINE);
    for (size_t i = 0; i < lines.size() && i < 6; i++)
        text(2, 30 + i * ROW_H, lines[i], FG);
    hint("Y = yes    N / ` = no");
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto ks = M5Cardputer.Keyboard.keysState();
            for (char c : ks.word) {
                if (c == 'y' || c == 'Y') return true;
                if (c == 'n' || c == 'N' || c == '`') return false;
            }
        }
        delay(15);
    }
}

bool editBuffer(String& buf, const KeyEvent& k, size_t maxLen, bool multiline) {
    if (k.del) {
        if (buf.length() == 0) return false;
        buf.remove(buf.length() - 1);
        return true;
    }
    if (k.enter && multiline) {
        if (buf.length() >= maxLen) return false;
        buf += '\n';
        return true;
    }
    bool changed = false;
    for (char c : k.chars) {
        if (c == '`') continue;               // reserved for esc
        if (buf.length() >= maxLen) break;
        buf += c;
        changed = true;
    }
    return changed;
}

}  // namespace ui
