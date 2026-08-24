#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/cloud.h"
#include "../kernel/store.h"

// Agent mode. The daemon runs a real coding CLI in a project directory on your
// Mac, so it has tools and a filesystem; the Cardputer is keyboard and screen.
// Host-only on purpose: an agent without a filesystem isn't an agent.
class Code : public App {
    enum Mode : uint8_t { PROMPT, REPLY, PROJECT };
public:
    const char* name() const override { return "Code"; }
    const char* blurb() const override { return "agent"; }
    ui::Icon icon() const override { return ui::Icon::Code; }

    String title() const override {
        if (mode_ == PROJECT) return "Project directory";
        String p = store::getStr("project", "");
        if (p.length()) {
            int slash = p.lastIndexOf('/');
            p = slash >= 0 ? p.substring(slash + 1) : p;
        } else p = "default";
        return backend() + "  " + ui::ellipsize(p, 14);
    }

    bool onBack() override {
        if (mode_ == PROJECT) { mode_ = PROMPT; return true; }
        if (mode_ == REPLY)   { mode_ = PROMPT; return true; }
        if (prompt_.length()) { prompt_ = ""; return true; }
        return false;
    }

    void onEnter() override {
        if (!cloud::hostOnline()) os::toast("daemon offline - Settings > Find Mac", os::Tone::Bad);
        mode_ = PROMPT;
        os::invalidate();
    }

    void onKey(const KeyEvent& k) override {
        if (mode_ == PROJECT) {
            if (k.enter) { store::setStr("project", projBuf_); mode_ = PROMPT;
                           os::toast("project set", os::Tone::Good); return; }
            if (ui::editBuffer(projBuf_, k, 120)) os::invalidate();
            return;
        }
        if (mode_ == REPLY) {
            if (k.down || k.is('j')) { scroll_++; os::invalidate(); return; }
            if (k.up   || k.is('k')) { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
            return;
        }
        if (k.enter) { run(); return; }
        if (k.ctrl && k.is('p')) {
            projBuf_ = store::getStr("project", "");
            mode_ = PROJECT;
            os::invalidate();
            return;
        }
        if (k.tab) {
            std::vector<String> opts = {"claude", "codex", "gemini"};
            int pick = ui::chooser("Agent CLI", opts, 0);
            if (pick >= 0) { store::setStr("m_host", opts[pick]);
                             os::toast("agent: " + opts[pick], os::Tone::Good); }
            os::invalidate();
            return;
        }
        if (ui::editBuffer(prompt_, k, 500)) os::invalidate();
    }

    void tick() override {}

    void draw() override {
        if (mode_ == PROJECT) {
            ui::text(4, BODY_Y + 4, "Absolute path on the Mac:", ui::c().dim);
            ui::inputLine(BODY_Y + 24, "", projBuf_, ui::c().fg);
            ui::text(4, BODY_Y + 50, "blank = the daemon's default", ui::c().dim);
            ui::hint("Enter save   ` cancel");
            return;
        }
        if (mode_ == REPLY) {
            ui::pager(output_, scroll_, ui::c().fg);
            ui::hint("J/K scroll   ` new prompt");
            return;
        }
        if (!cloud::hostOnline()) {
            ui::icon(4, BODY_Y + 4, ui::Icon::Cross, ui::c().bad);
            ui::text(18, BODY_Y + 4, "Mac daemon not reachable", ui::c().bad);
            ui::text(4, BODY_Y + 20, "run host/cardputerd.py, then", ui::c().dim);
            ui::text(4, BODY_Y + 31, "Settings > Find Mac", ui::c().dim);
        } else {
            ui::text(4, BODY_Y + 4, "Send a task to the agent.", ui::c().dim);
            ui::text(4, BODY_Y + 18, "Runs with tools, in:", ui::c().dim);
            String p = store::getStr("project", "(daemon default)");
            ui::text(4, BODY_Y + 30, ui::ellipsize(p, 38), ui::c().accent);
            ui::badge(4, BODY_Y + 46, backend(), ui::c().bg, ui::c().accent2);
        }
        ui::inputLine(104, "$ ", prompt_, ui::c().fg);
        ui::hint("Enter run  TAB agent  ctrl+P project  ` back");
    }

private:
    static String backend() { return store::getStr("m_host", "claude"); }

    void run() {
        String p = prompt_;
        p.trim();
        if (!p.length()) return;
        cloud::Result r;
        String proj = store::getStr("project", "");
        String be = backend();
        ui::await(be + " is working", [&] { r = cloud::code(p, proj, be); });
        output_ = r.ok ? r.text : ("[" + r.error + "]");
        if (r.ok) prompt_ = "";
        else os::toast(r.error, os::Tone::Bad);
        scroll_ = 0;
        mode_ = REPLY;
        os::invalidate();
    }

    Mode mode_ = PROMPT;
    String prompt_, output_, projBuf_;
    int scroll_ = 0;
};

App* codeApp() { static Code a; return &a; }
