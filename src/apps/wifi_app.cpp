#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/net.h"
#include "../kernel/cloud.h"

// Join any network: scan, pick, type the password once, and it's remembered.
class WifiApp : public App {
    enum Mode { LIST, PASSWORD, BUSY };
public:
    const char* name() const override { return "WiFi"; }
    const char* blurb() const override { return "networks"; }

    String title() const override {
        if (mode_ == PASSWORD) return "Password: " + ui::ellipsize(target_, 18);
        if (net::connected()) return "WiFi  " + ui::ellipsize(net::ssid(), 20);
        return String("WiFi  offline");
    }

    void onEnter() override {
        mode_ = LIST; sel_ = 0; scroll_ = 0; pass_ = "";
        net::startScan();
        os::invalidate();
    }

    bool escExits() const override { return mode_ == LIST; }

    void onKey(const KeyEvent& k) override {
        if (mode_ == BUSY) return;

        if (mode_ == PASSWORD) {
            if (k.esc)   { mode_ = LIST; os::invalidate(); return; }
            if (k.enter) { join(target_, pass_); return; }
            if (ui::editBuffer(pass_, k, 63)) os::invalidate();
            return;
        }

        auto nets = net::scanResults();
        if (k.is('r')) { net::startScan(); os::invalidate(); return; }
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)nets.size() - 1) sel_++; os::invalidate(); return; }
        if (k.is('d')) {
            if (sel_ < (int)nets.size() && nets[sel_].known) {
                net::forgetNetwork(nets[sel_].ssid);
                os::toast("forgot " + nets[sel_].ssid);
                net::startScan();
            }
            return;
        }
        if (k.enter) {
            if (sel_ >= (int)nets.size()) return;
            target_ = nets[sel_].ssid;
            if (nets[sel_].open)      { join(target_, ""); return; }
            if (nets[sel_].known)     { join(target_, net::passwordFor(target_)); return; }
            pass_ = "";
            mode_ = PASSWORD;
            os::invalidate();
        }
    }

    void tick() override {
        if (scanning_ != net::scanRunning()) { scanning_ = net::scanRunning(); os::invalidate(); }
    }

    void draw() override {
        if (mode_ == PASSWORD) {
            ui::text(2, BODY_Y, "Network: " + ui::ellipsize(target_, 28), ui::ACCENT);
            ui::inputLine(BODY_Y + 24, "pw> ", pass_, ui::FG);
            ui::text(2, BODY_Y + 48, String(pass_.length()) + " chars", ui::DIM);
            ui::hint("Enter join   ` cancel");
            return;
        }
        if (mode_ == BUSY) {
            ui::centered(55, busyMsg_, ui::WARN);
            ui::hint("");
            return;
        }

        auto nets = net::scanResults();
        if (net::scanRunning()) {
            ui::centered(55, "Scanning...", ui::WARN);
            ui::hint("");
            return;
        }
        if (nets.empty()) {
            ui::centered(55, "No networks found", ui::DIM);
            ui::hint("R rescan   ` back");
            return;
        }

        std::vector<String> items;
        for (auto& n : nets) {
            String row = n.known ? "* " : (n.open ? "o " : "# ");
            row += ui::ellipsize(n.ssid, 26);
            while (row.length() < 30) row += ' ';
            row += String(n.rssi);
            if (net::connected() && n.ssid == net::ssid()) row = ">" + row.substring(1);
            items.push_back(row);
        }
        scroll_ = ui::drawList(items, sel_, scroll_, 9, BODY_Y);
        ui::hint("Enter join  R rescan  D forget  ` back");
    }

private:
    void join(const String& s, const String& p) {
        mode_ = BUSY;
        busyMsg_ = "Joining " + ui::ellipsize(s, 18) + "...";
        os::invalidate();
        ui::clear(); draw(); ui::statusBar("WiFi");

        bool ok = net::connect(s, p);
        if (ok) {
            net::saveNetwork(s, p);
            net::syncTime();
            cloud::begin();
            os::toast("joined " + s + " " + net::ip());
        } else {
            os::toast("failed: " + s);
        }
        mode_ = LIST;
        net::startScan();
        os::invalidate();
    }

    Mode mode_ = LIST;
    int sel_ = 0, scroll_ = 0;
    bool scanning_ = false;
    String target_, pass_, busyMsg_;
};

App* wifiApp() { static WifiApp a; return &a; }
