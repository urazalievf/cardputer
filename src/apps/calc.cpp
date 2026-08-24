#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
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

    // ---------- parser ----------
    // grammar: expr := term (('+'|'-') term)*
    //          term := power (('*'|'/'|'%') power)*
    //          power:= unary ('^' power)?
    //          unary:= ('-'|'+')? atom
    //          atom := number | 'ans' | func '(' expr ')' | '(' expr ')'
    const char* p_ = nullptr;
    bool fail_ = false;

    void skip() { while (*p_ == ' ') p_++; }

    double parseExpr() {
        double v = parseTerm();
        while (!fail_) {
            skip();
            if (*p_ == '+')      { p_++; v += parseTerm(); }
            else if (*p_ == '-') { p_++; v -= parseTerm(); }
            else break;
        }
        return v;
    }

    double parseTerm() {
        double v = parsePower();
        while (!fail_) {
            skip();
            if (*p_ == '*')      { p_++; v *= parsePower(); }
            else if (*p_ == '/') {
                p_++;
                double d = parsePower();
                if (d == 0) { fail_ = true; err_ = "div by zero"; return 0; }
                v /= d;
            } else if (*p_ == '%') {
                p_++;
                double d = parsePower();
                if (d == 0) { fail_ = true; err_ = "div by zero"; return 0; }
                v = fmod(v, d);
            } else break;
        }
        return v;
    }

    double parsePower() {
        double base = parseUnary();
        skip();
        if (*p_ == '^') { p_++; return pow(base, parsePower()); }   // right-associative
        return base;
    }

    double parseUnary() {
        skip();
        if (*p_ == '-') { p_++; return -parseUnary(); }
        if (*p_ == '+') { p_++; return parseUnary(); }
        return parseAtom();
    }

    bool word(const char* w) {
        size_t n = strlen(w);
        if (strncasecmp(p_, w, n) == 0) { p_ += n; return true; }
        return false;
    }

    double parseAtom() {
        skip();
        if (*p_ == '(') {
            p_++;
            double v = parseExpr();
            skip();
            if (*p_ == ')') p_++;
            else { fail_ = true; err_ = "missing )"; }
            return v;
        }
        if (word("ans")) return ansValue_;
        if (word("pi"))  return M_PI;
        if (word("e"))   return M_E;

        struct Fn { const char* name; double (*fn)(double); };
        static const Fn fns[] = {
            {"sqrt", sqrt}, {"abs", fabs}, {"ln", log}, {"log", log10},
            {"sin", nullptr}, {"cos", nullptr}, {"tan", nullptr},
            {"round", nullptr}, {"floor", floor}, {"ceil", ceil},
        };
        for (auto& f : fns) {
            const char* save = p_;
            if (!word(f.name)) continue;
            skip();
            if (*p_ != '(') { p_ = save; break; }
            p_++;
            double a = parseExpr();
            skip();
            if (*p_ == ')') p_++; else { fail_ = true; err_ = "missing )"; }
            if (f.fn) return f.fn(a);
            String n = f.name;
            if (n == "round") return round(a);
            double r = deg_ ? a * M_PI / 180.0 : a;
            if (n == "sin") return sin(r);
            if (n == "cos") return cos(r);
            return tan(r);
        }

        char* end = nullptr;
        double v = strtod(p_, &end);
        if (end == p_) { fail_ = true; if (!err_.length()) err_ = "syntax"; return 0; }
        p_ = end;
        return v;
    }

    static String fmt(double v) {
        if (isnan(v)) return "nan";
        if (isinf(v)) return v > 0 ? "inf" : "-inf";
        // Integers shouldn't grow a trailing ".00"; big numbers go exponential.
        if (fabs(v) < 1e15 && v == floor(v)) { char b[24]; snprintf(b, sizeof(b), "%.0f", v); return b; }
        char b[24];
        if (fabs(v) >= 1e9 || (fabs(v) < 1e-4 && v != 0)) snprintf(b, sizeof(b), "%.6g", v);
        else snprintf(b, sizeof(b), "%.6f", v);
        String s = b;
        if (s.indexOf('.') >= 0) {
            while (s.endsWith("0")) s.remove(s.length() - 1);
            if (s.endsWith(".")) s.remove(s.length() - 1);
        }
        return s;
    }

    void evaluate() {
        String e = expr_;
        e.trim();
        if (!e.length()) return;
        p_ = e.c_str();
        fail_ = false;
        err_ = "";
        double v = parseExpr();
        skip();
        if (!fail_ && *p_ != '\0') { fail_ = true; err_ = "unexpected '" + String(*p_) + "'"; }
        if (fail_) { last_ = ""; os::invalidate(); return; }

        ansValue_ = v;
        last_ = fmt(v);
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
