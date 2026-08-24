#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
#include "../kernel/expr.h"
#include <math.h>

// Calculator. A recursive-descent parser rather than a keypad, because this
// device has a real keyboard — typing "12*(3+4)/2" beats pressing buttons.
class Calc : public App {
public:
    const char* name() const override { return "Calc"; }
    const char* blurb() const override { return "math"; }
    ui::Icon icon() const override { return ui::Icon::Chip; }

    String title() const override { return deg_ ? "Calc  deg" : "Calc  rad"; }

    bool onBack() override {
        if (expr_.length()) { expr_ = ""; os::invalidate(); return true; }
        return false;
    }

    void onEnter() override { load(); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (k.enter) { evaluate(); return; }
        if (k.ctrl && k.is('l')) { hist_.clear(); save(); os::invalidate(); return; }
        if (k.ctrl && k.is('d')) { deg_ = !deg_; store::setInt("calcdeg", deg_);
                                   os::toast(deg_ ? "degrees" : "radians"); return; }
        if (expr_.length() == 0) {
            if (k.up)   { if (histSel_ < (int)hist_.size() - 1) histSel_++; recall(); return; }
            if (k.down) { if (histSel_ > 0) { histSel_--; recall(); } return; }
        }
        if (ui::editBuffer(expr_, k, 80)) { err_ = ""; os::invalidate(); }
    }

    void draw() override {
        // History, oldest at top, newest just above the input.
        int y = BODY_Y + 2;
        int shown = min((int)hist_.size(), 4);
        for (int i = shown - 1; i >= 0; i--) {
            const auto& h = hist_[i];
            ui::text(4, y, ui::ellipsize(h.expr, 22), ui::c().dim);
            String v = h.value;
            ui::text(SCREEN_W - 6 - ui::textW(v), y, v, ui::c().accent2);
            y += 11;
        }

        // The answer gets the space it deserves.
        ui::gfx().setTextSize(2);
        String big = err_.length() ? err_ : last_;
        int w = (int)big.length() * 12;
        ui::text(max(4, SCREEN_W - 6 - w), 74, big, err_.length() ? ui::c().bad : ui::c().fg);
        ui::gfx().setTextSize(1);

        ui::inputLine(104, "= ", expr_, ui::c().fg);
        ui::hint("Enter evaluate  ans  ctrl+D deg/rad  ctrl+L clear");
    }

private:
    struct Item { String expr; String value; };

    // Parsing lives in kernel/expr so the self-test can exercise it directly.
    void evaluate() {
        String e = expr_;
        e.trim();
        if (!e.length()) return;

        expr::Options opt;
        opt.degrees = deg_;
        opt.ans = ansValue_;
        double v = 0;
        err_ = "";
        if (!expr::eval(e, v, err_, opt)) { last_ = ""; os::invalidate(); return; }

        ansValue_ = v;
        last_ = expr::format(v);
        hist_.insert(hist_.begin(), {e, last_});
        while (hist_.size() > 12) hist_.pop_back();
        histSel_ = -1;
        expr_ = "";
        save();
        os::invalidate();
    }

    void recall() {
        if (histSel_ >= 0 && histSel_ < (int)hist_.size()) expr_ = hist_[histSel_].expr;
        os::invalidate();
    }

    void save() {
        String blob;
        for (auto& h : hist_) blob += h.expr + "\x1f" + h.value + "\x1e";
        store::setStr("calchist", blob);
    }

    void load() {
        if (loaded_) return;
        loaded_ = true;
        deg_ = store::getInt("calcdeg", 0) != 0;
        String blob = store::getStr("calchist", "");
        int i = 0;
        while (i < (int)blob.length()) {
            int rec = blob.indexOf('\x1e', i);
            if (rec < 0) break;
            String one = blob.substring(i, rec);
            int sep = one.indexOf('\x1f');
            if (sep > 0) hist_.push_back({one.substring(0, sep), one.substring(sep + 1)});
            i = rec + 1;
        }
        if (!hist_.empty()) { last_ = hist_[0].value; ansValue_ = atof(last_.c_str()); }
    }

    std::vector<Item> hist_;
    String expr_, last_, err_;
    double ansValue_ = 0;
    int histSel_ = -1;
    bool deg_ = false, loaded_ = false;
};

App* calcApp() { static Calc a; return &a; }
