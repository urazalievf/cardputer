#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"
#include "../kernel/ai.h"
#include "../kernel/store.h"
#include "../kernel/bt.h"

// Home: a tile grid. Eight apps fit exactly in 4x2, which is why the grid is
// the shape it is — no scrolling, everything one glance and one keypress away.
class Launcher : public App {
public:
    const char* name() const override { return "Home"; }
    ui::Icon icon() const override { return ui::Icon::Home; }
    String title() const override { return "CardputerOS " CARDPUTER_OS_VERSION; }

    void onEnter() override { os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        int n = (int)os::apps().size() - 1;
        if (n <= 0) return;
        int cols = 4;

        if (k.left  || k.is('h')) { sel_ = (sel_ - 1 + n) % n;    os::invalidate(); return; }
        if (k.right || k.is('l')) { sel_ = (sel_ + 1) % n;        os::invalidate(); return; }
        if (k.up    || k.is('k')) { sel_ = (sel_ - cols + n) % n; os::invalidate(); return; }
        if (k.down  || k.is('j')) { sel_ = (sel_ + cols) % n;     os::invalidate(); return; }
        if (k.enter || k.space)   { os::launch(sel_ + 1); return; }

        for (char ch : k.chars)
            if (ch >= '1' && ch <= '9' && (ch - '0') <= n) { os::launch(ch - '0'); return; }
    }

    void tick() override {
        if (millis() - last_ > 1000) { last_ = millis(); os::invalidate(); }
    }

    void draw() override {
        const auto& all = os::apps();
        int n = (int)all.size() - 1;
        const int cols = 4, tw = 54, th = 45, gap = 4;
        int x0 = (SCREEN_W - (cols * tw + (cols - 1) * gap)) / 2;
        int y0 = BODY_Y + 1;

        for (int i = 0; i < n; i++) {
            App* a = all[i + 1];
            int cx = x0 + (i % cols) * (tw + gap);
            int cy = y0 + (i / cols) * (th + gap);
            bool sel = i == sel_;
            uint16_t acc = a->accent() ? a->accent() : ui::c().accent;

            ui::panel(cx, cy, tw, th, sel ? ui::c().selbg : ui::c().surface, 5);
            if (sel) ui::outline(cx, cy, tw, th, acc, 5);

            ui::icon(cx + tw / 2 - 4, cy + 8, a->icon(), sel ? acc : ui::c().dim);
            String label = a->name();
            int lw = (int)label.length() * 6;
            ui::gfx().setTextSize(1);
            ui::text(cx + (tw - lw) / 2, cy + 24, label, sel ? ui::c().selfg : ui::c().fg);
            String num = String(i + 1);
            ui::text(cx + 4, cy + 4, num, sel ? acc : ui::c().border);
            if (sel) {
                String b = a->blurb();
                int bw = (int)b.length() * 6;
                if (bw <= tw - 4) ui::text(cx + (tw - bw) / 2, cy + 34, b, ui::c().dim);
            }
        }

        // Footer: the three facts that decide whether anything will work.
        String line;
        if (net::connected()) line = net::ssid();
        else                  line = "no wifi";
        line += cloud::hostOnline() ? "  mac" : "";
        line += store::sdReady() ? "  sd" : "";
        line += bt::active() ? "  bt" : "";
        line += String("  ") + ai::spec(ai::preferred()).label;
        ui::hint(line);
    }

private:
    int sel_ = 0;
    uint32_t last_ = 0;
};

App* launcherApp() { static Launcher a; return &a; }
