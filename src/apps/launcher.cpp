#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"
#include "../kernel/store.h"

// Home screen: pick an app, see at a glance whether the radio, the Mac and
// the SD card are actually there.
class Launcher : public App {
public:
    const char* name() const override { return "Home"; }
    String title() const override { return "CardputerOS " CARDPUTER_OS_VERSION; }

    void onEnter() override { sel_ = 0; scroll_ = 0; os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        int n = (int)os::apps().size() - 1;      // apps()[0] is this launcher
        if (n <= 0) return;

        if (k.up   || k.is('k')) { sel_ = (sel_ - 1 + n) % n; os::invalidate(); return; }
        if (k.down || k.is('j')) { sel_ = (sel_ + 1) % n;     os::invalidate(); return; }
        if (k.enter || k.space)  { os::launch(sel_ + 1); return; }

        // Number keys jump straight to an app.
        for (char c : k.chars) {
            if (c >= '1' && c <= '9') {
                int idx = c - '0';
                if (idx <= n) os::launch(idx);
                return;
            }
        }
    }

    void tick() override {
        // Redraw once a second so the clock and link status stay honest.
        if (millis() - lastTick_ > 1000) { lastTick_ = millis(); os::invalidate(); }
    }

    void draw() override {
        std::vector<String> items;
        const auto& all = os::apps();
        for (size_t i = 1; i < all.size(); i++)
            items.push_back(String((int)i) + "  " + all[i]->name() + "   " + all[i]->blurb());

        scroll_ = ui::drawList(items, sel_, scroll_, 9, BODY_Y);

        String line = net::connected() ? (net::clockString() + "  " + net::ssid())
                                       : String("offline");
        if (cloud::hostOnline()) line += "  [mac]";
        if (store::sdReady())    line += "  [sd]";
        ui::hint(line);
    }

private:
    int sel_ = 0, scroll_ = 0;
    uint32_t lastTick_ = 0;
};

App* launcherApp() { static Launcher a; return &a; }
