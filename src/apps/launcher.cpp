#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"
#include "../kernel/ai.h"
#include "../kernel/store.h"
#include "../kernel/bt.h"
#include <algorithm>

// Home: a tile grid over the visible apps. Connectivity lives under Settings,
// so the grid stays one glance and one keypress deep.
class Launcher : public App {
public:
    const char* name() const override { return "Home"; }
    ui::Icon icon() const override { return ui::Icon::Home; }
    String title() const override { return "CardputerOS " CARDPUTER_OS_VERSION; }

    void onEnter() override {
        grid_ = store::getInt("homegrid", 1) != 0;
        visible_.clear();
        rebuild();
        os::invalidate();
    }

    bool onBack() override {
        if (reorder_) { reorder_ = false; os::toast("order saved", os::Tone::Good); return true; }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        rebuild();
        int n = (int)visible_.size();
        if (n <= 0) return;
        int step = grid_ ? columns(n) : 1;

        if (reorder_) {
            // Arrows carry the selected tile with them.
            if (k.left  || k.is('h')) return move(-1);
            if (k.right || k.is('l')) return move(+1);
            if (k.up    || k.is('k')) return move(-step);
            if (k.down  || k.is('j')) return move(+step);
            if (k.enter || k.space || k.is('o')) {
                reorder_ = false;
                os::toast("order saved", os::Tone::Good);
                os::invalidate();
            }
            return;
        }

        if (k.left  || k.is('h')) { sel_ = (sel_ - 1 + n) % n;    os::invalidate(); return; }
        if (k.right || k.is('l')) { sel_ = (sel_ + 1) % n;        os::invalidate(); return; }
        if (k.up    || k.is('k')) { sel_ = (sel_ - step + n) % n; os::invalidate(); return; }
        if (k.down  || k.is('j')) { sel_ = (sel_ + step) % n;     os::invalidate(); return; }
        if (k.enter || k.space)   { os::launch(visible_[sel_]);   return; }

        if (k.is('v')) {
            grid_ = !grid_;
            store::setInt("homegrid", grid_);
            os::toast(grid_ ? "grid view" : "list view");
            return;
        }
        if (k.is('o')) {
            reorder_ = true;
            os::toast("move with arrows, Enter when done");
            os::invalidate();
            return;
        }
        if (k.is('s')) { sortAlpha(); return; }

        for (char ch : k.chars)
            if (ch >= '1' && ch <= '9' && (ch - '1') < n) { os::launch(visible_[ch - '1']); return; }
    }

    void tick() override {
        if (millis() - last_ > 1000) { last_ = millis(); os::invalidate(); }
    }

    void draw() override {
        rebuild();
        int n = (int)visible_.size();
        if (!n) return;
        if (!grid_) return drawList(n);
        const int cols = columns(n);
        const int rows = (n + cols - 1) / cols;
        // At most three rows fit between the status bar and the hint strip, so
        // beyond that the grid scrolls by row rather than shrinking further.
        const int visRows = min(rows, 3);
        const int gap = visRows > 2 ? 4 : 6;
        const int tw = (SCREEN_W - (cols + 1) * gap) / cols;
        const int th = visRows > 2 ? 30 : 45;
        int x0 = gap, y0 = BODY_Y + 1;
        if (sel_ >= n) sel_ = n - 1;

        int selRow = sel_ / cols;
        if (selRow < rowScroll_) rowScroll_ = selRow;
        if (selRow >= rowScroll_ + visRows) rowScroll_ = selRow - visRows + 1;
        if (rowScroll_ > rows - visRows) rowScroll_ = max(0, rows - visRows);

        for (int i = rowScroll_ * cols; i < n && i < (rowScroll_ + visRows) * cols; i++) {
            App* a = os::apps()[visible_[i]];
            int cx = x0 + (i % cols) * (tw + gap);
            int cy = y0 + (i / cols - rowScroll_) * (th + gap);
            bool sel = i == sel_;
            uint16_t acc = a->accent() ? a->accent() : ui::c().accent;
            if (sel && reorder_) acc = ui::c().warn;

            ui::panel(cx, cy, tw, th, sel ? ui::c().selbg : ui::c().surface, 5);
            if (sel) ui::outline(cx, cy, tw, th, acc, 5);

            ui::gfx().setTextSize(1);
            int iconY = th > 40 ? cy + 7 : cy + 4;
            ui::icon(cx + tw / 2 - 4, iconY, a->icon(), sel ? acc : ui::c().dim);
            String label = ui::ellipsize(a->name(), (tw - 4) / 6);
            ui::text(cx + (tw - (int)label.length() * 6) / 2, iconY + 15, label,
                     sel ? ui::c().selfg : ui::c().fg);
            ui::text(cx + 3, cy + 2, String(i + 1), sel ? acc : ui::c().border);
            if (sel && th > 40) {
                String b = a->blurb();
                int bw = (int)b.length() * 6;
                if (bw <= tw - 4) ui::text(cx + (tw - bw) / 2, cy + 33, b, ui::c().dim);
            }
        }

        if (rows > visRows) {
            int trackH = visRows * (th + gap) - gap;
            int barH = max(8, trackH * visRows / rows);
            int span = rows - visRows;
            int barY = y0 + (span > 0 ? (trackH - barH) * rowScroll_ / span : 0);
            ui::gfx().fillRoundRect(SCREEN_W - 3, barY, 3, barH, 1, ui::c().accent);
        }
        footer();
    }

    void drawList(int n) {
        std::vector<ui::Row> rows;
        for (int i = 0; i < n; i++) {
            App* a = os::apps()[visible_[i]];
            ui::Row r;
            r.label = String(i + 1) + "  " + a->name();
            r.detail = a->blurb();
            r.icon = a->icon();
            rows.push_back(r);
        }
        if (sel_ >= n) sel_ = n - 1;
        scroll_ = ui::listView(rows, sel_, scroll_);
        footer();
    }

    void footer() {
        if (reorder_) {
            ui::hint("REORDER: arrows move   Enter done");
            return;
        }
        String line = net::connected() ? net::ssid() : String("no wifi");
        if (cloud::hostOnline()) line += "  mac";
        if (store::sdReady())    line += "  sd";
        if (bt::active())        line += "  bt";
        line += String("  V ") + (grid_ ? "list" : "grid") + "  O order";
        ui::hint(line);
    }

private:
    static int columns(int n) { return n <= 4 ? (n < 1 ? 1 : n) : (n <= 6 ? 3 : 4); }

    // Saved order is a list of app names, so adding or removing an app in a
    // later firmware doesn't scramble what the user arranged.
    void rebuild() {
        if (!visible_.empty()) return;
        String saved = store::getStr("homeorder", "");
        std::vector<int> rest;
        for (size_t i = 1; i < os::apps().size(); i++)
            if (!os::apps()[i]->hidden()) rest.push_back((int)i);

        int from = 0;
        while (from < (int)saved.length()) {
            int comma = saved.indexOf(',', from);
            if (comma < 0) comma = saved.length();
            String want = saved.substring(from, comma);
            from = comma + 1;
            for (size_t j = 0; j < rest.size(); j++) {
                if (String(os::apps()[rest[j]]->name()) != want) continue;
                visible_.push_back(rest[j]);
                rest.erase(rest.begin() + j);
                break;
            }
        }
        for (int i : rest) visible_.push_back(i);      // anything new lands at the end
    }

    void saveOrder() {
        String out;
        for (size_t i = 0; i < visible_.size(); i++) {
            if (i) out += ",";
            out += os::apps()[visible_[i]]->name();
        }
        store::setStr("homeorder", out);
    }

    void move(int delta) {
        int n = (int)visible_.size();
        int dest = sel_ + delta;
        if (dest < 0 || dest >= n) return;
        std::swap(visible_[sel_], visible_[dest]);
        sel_ = dest;
        saveOrder();
        os::invalidate();
    }

    void sortAlpha() {
        std::sort(visible_.begin(), visible_.end(), [](int a, int b) {
            return String(os::apps()[a]->name()) < String(os::apps()[b]->name());
        });
        saveOrder();
        os::toast("sorted A-Z", os::Tone::Good);
        os::invalidate();
    }

    std::vector<int> visible_;
    int sel_ = 0, scroll_ = 0, rowScroll_ = 0;
    bool grid_ = true, reorder_ = false;
    uint32_t last_ = 0;
};

App* launcherApp() { static Launcher a; return &a; }
