#include "expr.h"
#include <math.h>

namespace expr {
namespace {

// grammar: expr := term (('+'|'-') term)*
//          term := power (('*'|'/'|'%') power)*
//          power:= unary ('^' power)?          right-associative
//          unary:= ('-'|'+')? atom
//          atom := number | 'ans' | 'pi' | 'e' | func '(' expr ')' | '(' expr ')'
struct Parser {
    const char* p;
    bool fail = false;
    String err;
    Options opt;

    void skip() { while (*p == ' ') p++; }

    void die(const char* why) { if (!fail) { fail = true; err = why; } }

    double parseExpr() {
        double v = parseTerm();
        while (!fail) {
            skip();
            if (*p == '+')      { p++; v += parseTerm(); }
            else if (*p == '-') { p++; v -= parseTerm(); }
            else break;
        }
        return v;
    }

    double parseTerm() {
        double v = parsePower();
        while (!fail) {
            skip();
            if (*p == '*')      { p++; v *= parsePower(); }
            else if (*p == '/') {
                p++;
                double d = parsePower();
                if (d == 0) { die("div by zero"); return 0; }
                v /= d;
            } else if (*p == '%') {
                p++;
                double d = parsePower();
                if (d == 0) { die("div by zero"); return 0; }
                v = fmod(v, d);
            } else break;
        }
        return v;
    }

    double parsePower() {
        double base = parseUnary();
        skip();
        if (*p == '^') { p++; return pow(base, parsePower()); }
        return base;
    }

    double parseUnary() {
        skip();
        if (*p == '-') { p++; return -parseUnary(); }
        if (*p == '+') { p++; return parseUnary(); }
        return parseAtom();
    }

    bool word(const char* w) {
        size_t n = strlen(w);
        if (strncasecmp(p, w, n) != 0) return false;
        // Don't let "e" swallow the start of "exp" or a variable name.
        char after = p[n];
        if (isalpha((int)after)) return false;
        p += n;
        return true;
    }

    double parseAtom() {
        skip();
        if (*p == '(') {
            p++;
            double v = parseExpr();
            skip();
            if (*p == ')') p++; else die("missing )");
            return v;
        }

        struct Fn { const char* name; };
        static const Fn fns[] = {{"sqrt"}, {"abs"}, {"ln"}, {"log"}, {"sin"}, {"cos"},
                                 {"tan"}, {"round"}, {"floor"}, {"ceil"}};
        for (auto& f : fns) {
            const char* save = p;
            size_t n = strlen(f.name);
            if (strncasecmp(p, f.name, n) != 0) continue;
            p += n;
            skip();
            if (*p != '(') { p = save; continue; }
            p++;
            double a = parseExpr();
            skip();
            if (*p == ')') p++; else die("missing )");
            String nm = f.name;
            if (nm == "sqrt")  { if (a < 0) { die("sqrt of negative"); return 0; } return sqrt(a); }
            if (nm == "abs")   return fabs(a);
            if (nm == "ln")    { if (a <= 0) { die("ln domain"); return 0; } return log(a); }
            if (nm == "log")   { if (a <= 0) { die("log domain"); return 0; } return log10(a); }
            if (nm == "round") return round(a);
            if (nm == "floor") return floor(a);
            if (nm == "ceil")  return ceil(a);
            double r = opt.degrees ? a * M_PI / 180.0 : a;
            if (nm == "sin") return sin(r);
            if (nm == "cos") return cos(r);
            return tan(r);
        }

        if (word("ans")) return opt.ans;
        if (word("pi"))  return M_PI;
        if (word("e"))   return M_E;

        char* end = nullptr;
        double v = strtod(p, &end);
        if (end == p) { die("syntax"); return 0; }
        p = end;
        return v;
    }
};

}  // namespace

bool eval(const String& input, double& out, String& err, const Options& opt) {
    String trimmed = input;
    trimmed.trim();
    if (!trimmed.length()) { err = "empty"; return false; }

    Parser ps;
    ps.p = trimmed.c_str();
    ps.opt = opt;
    double v = ps.parseExpr();
    ps.skip();
    if (!ps.fail && *ps.p != '\0') { ps.fail = true; ps.err = String("unexpected '") + *ps.p + "'"; }
    if (ps.fail) { err = ps.err; return false; }
    if (isnan(v) || isinf(v)) { err = isnan(v) ? "not a number" : "overflow"; return false; }
    out = v;
    return true;
}

String format(double v) {
    if (isnan(v)) return "nan";
    if (isinf(v)) return v > 0 ? "inf" : "-inf";
    if (fabs(v) < 1e15 && v == floor(v)) {
        char b[24];
        snprintf(b, sizeof(b), "%.0f", v);
        return b;
    }
    char b[24];
    if (fabs(v) >= 1e9 || (fabs(v) < 1e-4 && v != 0)) snprintf(b, sizeof(b), "%.6g", v);
    else snprintf(b, sizeof(b), "%.6f", v);
    String s = b;
    if (s.indexOf('.') >= 0 && s.indexOf('e') < 0) {
        while (s.endsWith("0")) s.remove(s.length() - 1);
        if (s.endsWith(".")) s.remove(s.length() - 1);
    }
    return s;
}

}  // namespace expr
