#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"

// Join any network: scan, pick, type the password once, remembered after that.
class WifiApp : public App {
    enum Mode : uint8_t { LIST, PASSWORD };
public:
    const char* name() const override { return "WiFi"; }
    const char* blurb() const override { return "networks"; }
    ui::Icon icon() const override { return ui::Icon::Wifi; }

    String title() const override {
        if (mode_ == PASSWORD) return "Password";
        if (net::connected())  return "WiFi  " + ui::ellipsize(net::ssid(), 18);
        return "WiFi  offline";
    }

    bool onBack() override {
        if (mode_ == PASSWORD) { mode_ = LIST; return true; }
        return false;
    }

    void onEnter() override {
        mode_ = LIST; sel_ = 0; scroll_ = 0; pass_ = "";
        net::startScan();
        os::invalidate();
    }

    void onKey(const KeyEvent& k) override {
        if (mode_ == PASSWORD) {
            if (k.enter) { join(target_, pass_); return; }
            if (ui::editBuffer(pass_, k, 63)) os::invalidate();
            return;
        }
        auto nets = net::scanResults();
        if (k.is('r')) { net::startScan(); os::invalidate(); return; }
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)nets.size() - 1) sel_++; os::invalidate(); return; }
        if (k.is('d') && sel_ < (int)nets.size() && nets[sel_].known) {
            net::forgetNetwork(nets[sel_].ssid);
            os::toast("forgot " + nets[sel_].ssid, os::Tone::Good);
            net::startScan();
            return;
        }
        if (k.enter && sel_ < (int)nets.size()) {
            target_ = nets[sel_].ssid;
            if (nets[sel_].open)  { join(target_, ""); return; }
            if (nets[sel_].known) { join(target_, net::passwordFor(target_)); return; }
            pass_ = "";
            mode_ = PASSWORD;
            os::invalidate();
        }
    }

    void tick() override {
        if (scanning_ != net::scanRunning()) { scanning_ = net::scanRunning(); os::invalidate(); }
        if (net::scanRunning() && millis() - spin_ > 120) { spin_ = millis(); os::invalidate(); }
    }

    void draw() override {
        if (mode_ == PASSWORD) {
            ui::text(4, BODY_Y + 4, ui::ellipsize(target_, 38), ui::c().accent);
            ui::inputLine(BODY_Y + 26, "pw ", pass_, ui::c().fg);
            ui::text(4, BODY_Y + 50, String(pass_.length()) + " characters", ui::c().dim);
            ui::hint("Enter join   ` cancel   ctrl+Bksp word");
            return;
        }
        if (net::scanRunning()) {
            ui::spinner(SCREEN_W / 2, 54, ui::c().accent);
            ui::centered(76, "Scanning", ui::c().dim);
            ui::hint("");
            return;
        }
        auto nets = net::scanResults();
        if (nets.empty()) {
            ui::centered(52, "No networks found", ui::c().dim);
            ui::centered(66, "press R to try again", ui::c().accent);
            ui::hint("R rescan   ` back");
            return;
        }
        std::vector<ui::Row> rows;
        for (auto& n : nets) {
            ui::Row r;
            r.label = n.ssid;
            r.detail = String(n.rssi);
            r.icon = n.open ? ui::Icon::Wifi : ui::Icon::Lock;
            bool here = net::connected() && n.ssid == net::ssid();
            r.tint = here ? ui::c().good : n.known ? ui::c().accent2 : ui::c().fg;
            rows.push_back(r);
        }
        if (sel_ >= (int)rows.size()) sel_ = rows.size() - 1;
        scroll_ = ui::listView(rows, sel_, scroll_);
        ui::hint("Enter join   R rescan   D forget   ` back");
    }

private:
    void join(const String& s, const String& p) {
        ui::busy("Joining " + ui::ellipsize(s, 16));
        bool ok = net::connect(s, p);
        if (ok) {
            net::saveNetwork(s, p);
            net::syncTime();
            cloud::begin();
            os::toast("joined " + s + "  " + net::ip(), os::Tone::Good);
        } else {
            os::toast("could not join " + s, os::Tone::Bad);
        }
        mode_ = LIST;
        net::startScan();
        os::invalidate();
    }

    Mode mode_ = LIST;
    int sel_ = 0, scroll_ = 0;
    bool scanning_ = false;
    uint32_t spin_ = 0;
    String target_, pass_;
};

App* wifiApp() { static WifiApp a; return &a; }
