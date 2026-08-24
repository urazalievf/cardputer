#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"
#include "../kernel/ai.h"
#include <algorithm>

// Markdown notes. Real .md files on the SD card, so the card drops straight
// into an Obsidian vault; S pushes one over WiFi instead.
class Notes : public App {
    enum Mode : uint8_t { LIST, VIEW, EDIT, ASK };
public:
    const char* name() const override { return "Notes"; }
    const char* blurb() const override { return "markdown"; }
    ui::Icon icon() const override { return ui::Icon::Note; }

    String title() const override {
        switch (mode_) {
            case LIST: return "Notes (" + String((int)files_.size()) + ")" +
                              (store::sdReady() ? "" : " nvs");
            case VIEW: return ui::firstLine(body_, 26);
            case EDIT: return String(editingNew_ ? "New " : "Edit ") +
                              String((int)buf_.length()) + "/" + String((int)MAXLEN);
            case ASK:  return String("Ask ") + ai::spec(ai::preferred()).label;
        }
        return "Notes";
    }

    bool onBack() override {
        switch (mode_) {
            case VIEW: mode_ = LIST; reload(); return true;
            case EDIT: save(); return true;
            case ASK:  mode_ = VIEW; return true;
            default:   return false;
        }
    }

    void onEnter() override {
        sort_ = store::getInt("notesort", 0);
        reload();
        mode_ = LIST;
        os::invalidate();
    }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case LIST: return keyList(k);
            case VIEW: return keyView(k);
            case EDIT: if (ui::editBuffer(buf_, k, MAXLEN, true)) os::invalidate(); return;
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

    const char* sortName() const {
        static const char* names[] = {"newest", "oldest", "A-Z"};
        return names[sort_ % 3];
    }

    void reload() {
        files_ = store::listNotes();       // already newest-first
        int mode = sort_;
        if (mode == 1) std::reverse(files_.begin(), files_.end());
        else if (mode == 2)
            std::sort(files_.begin(), files_.end(), [](const String& a, const String& b) {
                // Names are "<timestamp>-<slug>.md"; sort on the slug.
                return a.substring(16) < b.substring(16);
            });
        if (sel_ >= (int)files_.size()) sel_ = files_.empty() ? 0 : files_.size() - 1;
    }

    // "20260824-143210-standup.md" -> "08-24 14:32   standup"
    static String pretty(const String& f) {
        if (f.length() < 16) return f;
        String slug = f.substring(16);
        slug.replace(".md", "");
        slug.replace("-", " ");
        return f.substring(4, 6) + "-" + f.substring(6, 8) + " " +
               f.substring(9, 11) + ":" + f.substring(11, 13) + "  " + slug;
    }

    void keyList(const KeyEvent& k) {
        if (k.is('n')) { editingNew_ = true; buf_ = ""; mode_ = EDIT; os::invalidate(); return; }
        if (k.is('r')) { reload(); os::toast("reloaded"); return; }
        if (k.is('o')) {
            sort_ = (sort_ + 1) % 3;
            store::setInt("notesort", sort_);
            reload();
            sel_ = 0;
            os::toast(String("sorted by ") + sortName(), os::Tone::Good);
            os::invalidate();
            return;
        }
        if (files_.empty()) return;
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)files_.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter) {
            current_ = files_[sel_];
            body_ = store::readNote(current_);
            viewScroll_ = 0;
            mode_ = VIEW;
            os::invalidate();
        }
    }

    void drawList() {
        if (files_.empty()) {
            ui::centered(48, "No notes yet", ui::c().dim);
            ui::centered(64, "press N to write one", ui::c().accent);
            ui::hint("N new   ` back");
            return;
        }
        std::vector<ui::Row> rows;
        for (auto& f : files_) rows.push_back({pretty(f), "", ui::Icon::Note, 0});
        scroll_ = ui::listView(rows, sel_, scroll_);
        ui::hint(String("Enter open   N new   O ") + sortName() + "   ` back");
    }

    void keyView(const KeyEvent& k) {
        if (k.down || k.is('j')) { viewScroll_++; os::invalidate(); return; }
        if (k.up   || k.is('k')) { if (viewScroll_ > 0) viewScroll_--; os::invalidate(); return; }
        if (k.is('e')) { editingNew_ = false; buf_ = body_; mode_ = EDIT; os::invalidate(); return; }
        if (k.is('a')) { question_ = ""; answer_ = ""; askScroll_ = 0; mode_ = ASK; os::invalidate(); return; }
        if (k.is('d')) {
            if (ui::confirm("Delete \"" + ui::firstLine(body_, 24) + "\"?")) {
                store::deleteNote(current_);
                os::toast("deleted", os::Tone::Good);
                mode_ = LIST;
                reload();
            }
            os::invalidate();
            return;
        }
        if (k.is('s')) {
            ui::busy("Syncing to vault");
            String sub = store::getStr(store::K_VAULT, "Cardputer");
            auto r = cloud::vaultWrite(sub + "/" + current_, body_);
            os::toast(r.ok ? "synced to Obsidian" : r.error,
                      r.ok ? os::Tone::Good : os::Tone::Bad);
            os::invalidate();
        }
    }

    void drawView() {
        ui::pager(body_, viewScroll_, ui::c().fg);
        ui::hint("E edit  A ask  S sync  D del  ` back");
    }

    void drawEdit() {
        auto lines = ui::wrap(buf_);
        int visible = theme::bodyRows() - 1;
        int rh = theme::rowHeight();
        int start = (int)lines.size() > visible ? lines.size() - visible : 0;
        for (int i = 0; i < visible && start + i < (int)lines.size(); i++)
            ui::text(3, BODY_Y + i * rh, lines[start + i], ui::c().fg);
        int row = max(1, (int)lines.size() - start);
        int lastLen = lines.empty() ? 0 : lines.back().length();
        if ((millis() / 450) % 2 == 0)
            ui::gfx().fillRect(3 + lastLen * (theme::bigText() ? 12 : 6),
                               BODY_Y + (row - 1) * rh, 2, theme::bigText() ? 16 : 8,
                               ui::c().accent);
        ui::progress(3, HINT_Y - 10, SCREEN_W - 6, 4, (float)buf_.length() / MAXLEN, ui::c().accent);
        ui::hint("` save & close   ctrl+Bksp word");
    }

    void save() {
        String trimmed = buf_;
        trimmed.trim();
        if (!trimmed.length()) {
            if (!editingNew_ && ui::confirm("Note is empty. Delete it?"))
                store::deleteNote(current_);
            mode_ = LIST; reload(); os::invalidate();
            return;
        }
        if (editingNew_) current_ = store::newNoteName(ui::firstLine(trimmed, 30));
        bool ok = store::writeNote(current_, buf_);
        body_ = buf_;
        os::toast(ok ? "saved" : "save failed", ok ? os::Tone::Good : os::Tone::Bad);
        reload();
        mode_ = VIEW;
        viewScroll_ = 0;
    }

    void keyAsk(const KeyEvent& k) {
        if (k.enter) { runAsk(); return; }
        if (question_.length() == 0 && k.chars.empty()) {
            if (k.down) { askScroll_++; os::invalidate(); return; }
            if (k.up)   { if (askScroll_ > 0) askScroll_--; os::invalidate(); return; }
        }
        if (ui::editBuffer(question_, k, 200)) os::invalidate();
    }

    void drawAsk() {
        if (answer_.length()) ui::pager(answer_, askScroll_, ui::c().accent2, 8);
        else ui::text(4, BODY_Y + 4, "Ask about this note.", ui::c().dim);
        ui::inputLine(104, "q ", question_, ui::c().fg);
        ui::hint("Enter send   fn+; / fn+. scroll   ` back");
    }

    void runAsk() {
        if (!question_.length()) return;
        ui::busy("Thinking");
        auto r = ai::ask("Note:\n" + body_ + "\n\nQuestion: " + question_,
                         "You are answering about a note on a 240x135 handheld. Plain text "
                         "only, no markdown, under 400 characters.", 300);
        answer_ = r.ok ? r.text : ("[" + r.error + "]");
        os::toast(r.ok ? String("via ") + r.usedLabel() : r.error,
                  r.ok ? os::Tone::Good : os::Tone::Bad);
        question_ = "";
        askScroll_ = 0;
        os::invalidate();
    }

    Mode mode_ = LIST;
    std::vector<String> files_;
    int sel_ = 0, scroll_ = 0, viewScroll_ = 0, askScroll_ = 0, sort_ = 0;
    String current_, body_, buf_, question_, answer_;
    bool editingNew_ = false;
};

App* notesApp() { static Notes a; return &a; }
