#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/hw.h"
#include "../kernel/store.h"

// Universal remote over the IR LED. No receiver on this hardware, so there is
// no learning mode — instead, a few well-known code sets and a manual entry
// for anything else.
class Remote : public App {
    enum Mode : uint8_t { PAD, MANUAL };
public:
    const char* name() const override { return "Remote"; }
    const char* blurb() const override { return "infrared"; }
    ui::Icon icon() const override { return ui::Icon::Arrow; }

    String title() const override {
        if (mode_ == MANUAL) return "Manual code";
        return String("Remote  ") + deviceName();
    }

    bool onBack() override {
        if (mode_ == MANUAL) { mode_ = PAD; return true; }
        return false;
    }

    void onEnter() override { dev_ = store::getInt("irdev", 0); mode_ = PAD; os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (mode_ == MANUAL) {
            if (k.enter) { sendManual(); return; }
            if (ui::editBuffer(manual_, k, 20)) os::invalidate();
            return;
        }
        if (k.tab) { pickDevice(); return; }
        if (k.is('m')) { manual_ = ""; mode_ = MANUAL; os::invalidate(); return; }

        int n = buttonCount();
        if (k.left  || k.is('h')) { sel_ = (sel_ - 1 + n) % n; os::invalidate(); return; }
        if (k.right || k.is('l')) { sel_ = (sel_ + 1) % n;     os::invalidate(); return; }
        if (k.up    || k.is('k')) { sel_ = (sel_ - 3 + n) % n; os::invalidate(); return; }
        if (k.down  || k.is('j')) { sel_ = (sel_ + 3) % n;     os::invalidate(); return; }
        if (k.enter || k.space) { fire(sel_); return; }
    }

    void draw() override {
        if (mode_ == MANUAL) {
            ui::text(4, BODY_Y + 4, String("Protocol: ") + hw::irProtoName(proto()), ui::c().dim);
            ui::text(4, BODY_Y + 16, "Enter as  address,command  in hex", ui::c().dim);
            ui::inputLine(BODY_Y + 38, "> ", manual_, ui::c().fg);
            ui::text(4, BODY_Y + 62, "e.g.  07,02", ui::c().dim);
            ui::hint("Enter send   ` back");
        } else {
            const int cols = 3, gap = 5;
            const int tw = (SCREEN_W - (cols + 1) * gap) / cols, th = 26;
            for (int i = 0; i < buttonCount(); i++) {
                int cx = gap + (i % cols) * (tw + gap);
                int cy = BODY_Y + 2 + (i / cols) * (th + gap);
                bool sel = i == sel_;
                ui::panel(cx, cy, tw, th, sel ? ui::c().selbg : ui::c().surface, 4);
                if (sel) ui::outline(cx, cy, tw, th, ui::c().accent, 4);
                String lab = buttons()[i].label;
                ui::text(cx + (tw - (int)lab.length() * 6) / 2, cy + 9, lab,
                         sel ? ui::c().selfg : ui::c().fg);
            }
            ui::hint(String("Enter send  TAB device  M manual  pin G") + hw::irPin());
        }
    }

private:
    struct Button { const char* label; uint8_t cmd; };
    struct Device { const char* name; hw::IrProto proto; uint16_t addr; Button b[9]; };

    // Published code sets for the three most common TV families.
    static const Device* devices(int& n) {
        static const Device D[] = {
            {"Samsung", hw::IrProto::Samsung, 0x07,
             {{"Power",0x02},{"Vol+",0x07},{"Vol-",0x0B},
              {"Ch+",0x12},{"Mute",0x0F},{"Ch-",0x10},
              {"Src",0x01},{"Menu",0x1A},{"OK",0x68}}},
            {"LG / NEC", hw::IrProto::NEC, 0x04,
             {{"Power",0x08},{"Vol+",0x02},{"Vol-",0x03},
              {"Ch+",0x00},{"Mute",0x09},{"Ch-",0x01},
              {"Src",0x0B},{"Menu",0x43},{"OK",0x44}}},
            {"Sony", hw::IrProto::SonySIRC12, 0x01,
             {{"Power",0x15},{"Vol+",0x12},{"Vol-",0x13},
              {"Ch+",0x10},{"Mute",0x14},{"Ch-",0x11},
              {"Src",0x25},{"Menu",0x60},{"OK",0x65}}},
        };
        n = sizeof(D) / sizeof(D[0]);
        return D;
    }

    const Device& current() const {
        int n;
        auto D = devices(n);
        return D[constrain(dev_, 0, n - 1)];
    }
    String deviceName() const { return current().name; }
    hw::IrProto proto() const { return current().proto; }
    const Button* buttons() const { return current().b; }
    static int buttonCount() { return 9; }

    void pickDevice() {
        int n;
        auto D = devices(n);
        std::vector<String> opts;
        for (int i = 0; i < n; i++)
            opts.push_back(String(D[i].name) + "  (" + hw::irProtoName(D[i].proto) + ")");
        int pick = ui::chooser("TV brand", opts, dev_);
        if (pick >= 0) {
            dev_ = pick;
            store::setInt("irdev", dev_);
            os::toast(String("remote: ") + deviceName(), os::Tone::Good);
        }
        os::invalidate();
    }

    void fire(int i) {
        const Device& d = current();
        hw::irSend(d.proto, d.addr, d.b[i].cmd);
        hw::ledPulse(0, 40, 60, 40);          // visible confirmation; IR is not
        if (theme::sounds()) audioTick();
        os::toast(String(d.b[i].label) + " sent");
    }

    static void audioTick() { /* deliberately silent: a beep next to a TV is noise */ }

    void sendManual() {
        int comma = manual_.indexOf(',');
        if (comma <= 0) { os::toast("use  address,command", os::Tone::Bad); return; }
        uint32_t a = strtoul(manual_.substring(0, comma).c_str(), nullptr, 16);
        uint32_t c = strtoul(manual_.substring(comma + 1).c_str(), nullptr, 16);
        hw::irSend(proto(), a, c);
        hw::ledPulse(0, 40, 60, 40);
        os::toast("sent " + manual_);
    }

    Mode mode_ = PAD;
    int dev_ = 0, sel_ = 0;
    String manual_;
};

App* remoteApp() { static Remote a; return &a; }
