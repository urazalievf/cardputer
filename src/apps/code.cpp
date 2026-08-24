#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/cloud.h"
#include "../kernel/store.h"

// Claude Code, remote-controlled. The daemon runs the real `claude` CLI in a
// project directory on your Mac, so it has tools and a filesystem; the
// Cardputer is the keyboard and the screen.
class Code : public App {
    enum Mode { PROMPT, BUSY, REPLY, PROJECT };
public:
    const char* name() const override { return "Code"; }
    const char* blurb() const override { return "claude code"; }

    String title() const override {
        if (mode_ == BUSY)    return "Running...";
        if (mode_ == PROJECT) return "Project dir";
        String p = store::getStr("project", "");
        return "Code  " + (p.length() ? ui::ellipsize(p, 22) : String("(default)"));
    }

    bool escExits() const override { return mode_ == PROMPT && prompt_.length() == 0; }

    void onEnter() override {
        if (!cloud::hostOnline()) os::toast("daemon offline - Settings > Host");
        mode_ = PROMPT;
        os::invalidate();
    }

    void onKey(const KeyEvent& k) override {
        if (mode_ == BUSY) return;

        if (mode_ == PROJECT) {
            if (k.esc)   { mode_ = PROMPT; os::invalidate(); return; }
            if (k.enter) { store::setStr("project", projBuf_); mode_ = PROMPT; os::toast("project set"); return; }
            if (ui::editBuffer(projBuf_, k, 120)) os::invalidate();
            return;
        }
        if (mode_ == REPLY) {
            if (k.esc)               { mode_ = PROMPT; os::invalidate(); return; }
            if (k.down || k.is('j')) { scroll_++; os::invalidate(); return; }
            if (k.up   || k.is('k')) { if (scroll_ > 0) scroll_--; os::invalidate(); return; }
            return;
        }
        // PROMPT
        if (k.enter) { run(); return; }
        if (prompt_.length() == 0 && k.ctrl && k.is('p')) {
            projBuf_ = store::getStr("project", "");
            mode_ = PROJECT;
            os::invalidate();
            return;
        }
        if (ui::editBuffer(prompt_, k, 500, /*multiline=*/false)) os::invalidate();
    }

    void draw() override {
        if (mode_ == PROJECT) {
            ui::text(2, BODY_Y, "Absolute path on the Mac:", ui::DIM);
            ui::inputLine(BODY_Y + 22, "", projBuf_, ui::FG);
            ui::text(2, BODY_Y + 46, "blank = daemon's default dir", ui::DIM);
            ui::hint("Enter save   ` cancel");
            return;
        }
        if (mode_ == BUSY) {
            ui::centered(48, "claude is working", ui::WARN);
            ui::centered(64, String((millis() - startedAt_) / 1000) + "s", ui::DIM);
            ui::hint("this can take a few minutes");
            return;
        }
        if (mode_ == REPLY) {
            auto lines = ui::wrap(output_, CHARS_PER_LINE);
            if (scroll_ > (int)lines.size() - 1) scroll_ = max(0, (int)lines.size() - 1);
            for (int i = 0; i < 9 && scroll_ + i < (int)lines.size(); i++)
                ui::text(2, BODY_Y + i * ROW_H, lines[scroll_ + i], ui::FG);
            ui::hint("J/K scroll   ` new prompt");
            return;
        }

        if (!cloud::hostOnline()) {
            ui::text(2, BODY_Y, "Mac daemon not reachable.", ui::BAD);
            ui::text(2, BODY_Y + 12, "Run host/cardputerd.py and set", ui::DIM);
            ui::text(2, BODY_Y + 22, "the host in Settings.", ui::DIM);
        } else {
            ui::text(2, BODY_Y, "Send a task to Claude Code.", ui::DIM);
            ui::text(2, BODY_Y + 12, "It runs with tools, in:", ui::DIM);
            String p = store::getStr("project", "(daemon default)");
            ui::text(2, BODY_Y + 24, ui::ellipsize(p, 38), ui::ACCENT);
        }
        ui::inputLine(104, "$ ", prompt_, ui::FG);
        ui::hint("Enter run   ctrl+P project   ` back");
    }

    void tick() override {
        if (mode_ == BUSY && millis() - lastPaint_ > 1000) { lastPaint_ = millis(); os::invalidate(); }
    }

private:
    void run() {
        String p = prompt_;
        p.trim();
        if (!p.length()) return;
        mode_ = BUSY;
        startedAt_ = millis();
        lastPaint_ = millis();
        ui::clear(); draw(); ui::statusBar("Code");

        auto r = cloud::code(p, store::getStr("project", ""));
        output_ = r.ok ? r.text : ("[" + r.error + "]");
        if (r.ok) prompt_ = "";
        scroll_ = 0;
        mode_ = REPLY;
        os::invalidate();
    }

    Mode mode_ = PROMPT;
    String prompt_, output_, projBuf_;
    int scroll_ = 0;
    uint32_t startedAt_ = 0, lastPaint_ = 0;
};

App* codeApp() { static Code a; return &a; }
