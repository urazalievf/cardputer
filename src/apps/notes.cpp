#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"

// Markdown notes. On SD they're real .md files, so the card drops straight
// into an Obsidian vault; S pushes one to the vault over WiFi instead.
class Notes : public App {
    enum Mode { LIST, VIEW, EDIT, ASK };
public:
    const char* name() const override { return "Notes"; }
    const char* blurb() const override { return "markdown"; }

    String title() const override {
        switch (mode_) {
            case LIST: return "Notes (" + String((int)files_.size()) + ")" +
                              (store::sdReady() ? "" : " [nvs]");
            case VIEW: return ui::ellipsize(titleOf(body_), 30);
            case EDIT: return String(editingNew_ ? "New note " : "Edit ") +
                              String((int)buf_.length()) + "/" + String(MAXLEN);
            case ASK:  return String("Ask about note");
        }
        return "Notes";
    }

    bool escExits() const override { return mode_ == LIST; }

    void onEnter() override { reload(); mode_ = LIST; os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case LIST: return keyList(k);
            case VIEW: return keyView(k);
            case EDIT: return keyEdit(k);
            case ASK:  return keyAsk(k);
        }
    }

    void draw() override {
        switch (mode_) {
            case LIST: return drawList();
            case VIEW: return drawView();
            case EDIT: return drawEdit();
            case ASK:  return drawAsk();
        }
    }

private:
    static const size_t MAXLEN = 4000;

    void reload() {
        files_ = store::listNotes();
        if (sel_ >= (int)files_.size()) sel_ = files_.empty() ? 0 : files_.size() - 1;
    }

    static String titleOf(const String& body) { return ui::firstLine(body, 34); }

    // ---------- LIST ----------
    void keyList(const KeyEvent& k) {
        if (k.is('n')) { editingNew_ = true; buf_ = ""; mode_ = EDIT; os::invalidate(); return; }
        if (k.is('r')) { reload(); os::toast("reloaded"); return; }
        if (files_.empty()) return;
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)files_.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter) { open(sel_); return; }
    }

    void drawList() {
        if (files_.empty()) {
            ui::centered(50, "No notes yet", ui::DIM);
            ui::centered(64, "press N", ui::ACCENT);
            ui::hint("N new   ` back");
            return;
        }
        std::vector<String> rows;
        for (auto& f : files_) rows.push_back(prettyName(f));
        scroll_ = ui::drawList(rows, sel_, scroll_, 9, BODY_Y);
        ui::hint("Enter open  N new  J/K move  ` back");
    }

    // "20260824-143210-standup-notes.md" -> "08-24 14:32  standup notes"
    static String prettyName(const String& f) {
        if (f.length() < 16) return f;
        String date = f.substring(4, 6) + "-" + f.substring(6, 8);
        String time = f.substring(9, 11) + ":" + f.substring(11, 13);
        String slug = f.substring(16);
        slug.replace(".md", "");
        slug.replace("-", " ");
        return date + " " + time + "  " + slug;
    }

    void open(int i) {
        current_ = files_[i];
        body_ = store::readNote(current_);
        viewScroll_ = 0;
        mode_ = VIEW;
        os::invalidate();
    }

    // ---------- VIEW ----------
    void keyView(const KeyEvent& k) {
        if (k.esc)               { mode_ = LIST; reload(); os::invalidate(); return; }
        if (k.down || k.is('j')) { viewScroll_++; os::invalidate(); return; }
        if (k.up   || k.is('k')) { if (viewScroll_ > 0) viewScroll_--; os::invalidate(); return; }
        if (k.is('e')) { editingNew_ = false; buf_ = body_; mode_ = EDIT; os::invalidate(); return; }
        if (k.is('a')) { question_ = ""; answer_ = ""; askScroll_ = 0; mode_ = ASK; os::invalidate(); return; }
        if (k.is('d')) {
            if (ui::confirm("Delete \"" + titleOf(body_) + "\"?")) {
                store::deleteNote(current_);
                os::toast("deleted");
                mode_ = LIST;
                reload();
            }
            os::invalidate();
            return;
        }
        if (k.is('s')) { syncToVault(); return; }
    }

    void drawView() {
        auto lines = ui::wrap(body_, CHARS_PER_LINE);
        if (viewScroll_ > (int)lines.size() - 1) viewScroll_ = max(0, (int)lines.size() - 1);
        for (int i = 0; i < BODY_ROWS && viewScroll_ + i < (int)lines.size(); i++)
            ui::text(2, BODY_Y + i * ROW_H, lines[viewScroll_ + i], ui::FG);
        ui::hint("E edit  A ask  S sync  D del  ` back");
    }

    void syncToVault() {
        os::toast("syncing...");
        ui::clear(); draw(); ui::statusBar("Notes");
        String sub = store::getStr(store::K_VAULT, "Cardputer");
        auto r = cloud::vaultWrite(sub + "/" + current_, body_);
        os::toast(r.ok ? "synced to vault" : r.error);
        os::invalidate();
    }

    // ---------- EDIT ----------
    void keyEdit(const KeyEvent& k) {
        if (k.esc) { save(); return; }
        if (ui::editBuffer(buf_, k, MAXLEN, /*multiline=*/true)) os::invalidate();
    }

    void drawEdit() {
        auto lines = ui::wrap(buf_, CHARS_PER_LINE);
        int visible = BODY_ROWS - 1;
        int start = (int)lines.size() > visible ? lines.size() - visible : 0;
        for (int i = 0; i < visible && start + i < (int)lines.size(); i++)
            ui::text(2, BODY_Y + i * ROW_H, lines[start + i], ui::FG);

        int row = (int)lines.size() - start;
        if (row < 1) row = 1;
        int lastLen = lines.empty() ? 0 : lines.back().length();
        ui::text(2 + lastLen * 6, BODY_Y + (row - 1) * ROW_H, "_", ui::GOOD);
        ui::hint("` save & close   Bksp delete");
    }

    void save() {
        String trimmed = buf_;
        trimmed.trim();
        if (!trimmed.length()) {
            if (!editingNew_) {
                if (ui::confirm("Note is empty. Delete it?")) store::deleteNote(current_);
            }
            mode_ = LIST; reload(); os::invalidate();
            return;
        }
        if (editingNew_) current_ = store::newNoteName(ui::firstLine(trimmed, 30));
        bool ok = store::writeNote(current_, buf_);
        body_ = buf_;
        os::toast(ok ? "saved" : "SAVE FAILED");
        reload();
        mode_ = VIEW;
        viewScroll_ = 0;
        os::invalidate();
    }

    // ---------- ASK ----------
    void keyAsk(const KeyEvent& k) {
        if (waiting_) return;
        if (k.esc)   { mode_ = VIEW; os::invalidate(); return; }
        if (k.enter) { runAsk(); return; }
        if (question_.length() == 0) {
            if (k.down) { askScroll_++; os::invalidate(); return; }
            if (k.up)   { if (askScroll_ > 0) askScroll_--; os::invalidate(); return; }
        }
        if (ui::editBuffer(question_, k, 200)) os::invalidate();
    }

    void drawAsk() {
        if (answer_.length()) {
            auto lines = ui::wrap(answer_, CHARS_PER_LINE);
            if (askScroll_ > (int)lines.size() - 1) askScroll_ = max(0, (int)lines.size() - 1);
            for (int i = 0; i < 8 && askScroll_ + i < (int)lines.size(); i++)
                ui::text(2, BODY_Y + i * ROW_H, lines[askScroll_ + i], ui::ACCENT);
        } else if (!waiting_) {
            ui::text(2, BODY_Y, "Ask Claude about this note.", ui::DIM);
        }
        ui::inputLine(104, waiting_ ? "" : "q> ",
                      waiting_ ? String("thinking...") : question_,
                      waiting_ ? ui::WARN : ui::FG, !waiting_);
        ui::hint("Enter send  fn+;/. scroll  ` back");
    }

    void runAsk() {
        if (!question_.length() || waiting_) return;
        waiting_ = true;
        os::invalidate();
        ui::clear(); draw(); ui::statusBar("Notes");

        auto r = cloud::ask(
            "Note:\n" + body_ + "\n\nQuestion: " + question_,
            "You are answering about a note on a 240x135 handheld. Plain text only, "
            "no markdown, under 400 characters.", 300);
        answer_ = r.ok ? r.text : ("[" + r.error + "]");
        if (r.ok) os::toast(String("via ") + r.sourceName());
        question_ = "";
        askScroll_ = 0;
        waiting_ = false;
        os::invalidate();
    }

    Mode mode_ = LIST;
    std::vector<String> files_;
    int sel_ = 0, scroll_ = 0, viewScroll_ = 0, askScroll_ = 0;
    String current_, body_, buf_, question_, answer_;
    bool editingNew_ = false, waiting_ = false;
};

App* notesApp() { static Notes a; return &a; }
