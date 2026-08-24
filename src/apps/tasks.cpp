#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
#include "../kernel/cloud.h"
#include <Preferences.h>

// A checklist. Stored as a markdown task list so it is already the format
// Obsidian wants — S pushes it straight into the vault.
class Tasks : public App {
    enum Mode : uint8_t { LIST, ADD };
public:
    const char* name() const override { return "Tasks"; }
    const char* blurb() const override { return "todo"; }
    ui::Icon icon() const override { return ui::Icon::Check; }

    String title() const override {
        if (mode_ == ADD) return "New task";
        int done = 0;
        for (auto& t : items_) if (t.done) done++;
        return "Tasks  " + String(done) + "/" + String((int)items_.size());
    }

    bool onBack() override {
        if (mode_ == ADD) { mode_ = LIST; buf_ = ""; return true; }
        return false;
    }

    void onEnter() override { load(); os::invalidate(); }

    void onKey(const KeyEvent& k) override {
        if (mode_ == ADD) {
            if (k.enter) {
                String t = buf_;
                t.trim();
                if (t.length()) { items_.insert(items_.begin(), {t, false}); save(); }
                buf_ = "";
                mode_ = LIST;
                sel_ = 0;
                os::invalidate();
                return;
            }
            if (ui::editBuffer(buf_, k, 120)) os::invalidate();
            return;
        }

        if (k.is('n')) { buf_ = ""; mode_ = ADD; os::invalidate(); return; }
        if (k.is('s')) { syncVault(); return; }
        if (k.is('c')) {
            int before = items_.size();
            items_.erase(std::remove_if(items_.begin(), items_.end(),
                                        [](const Item& t) { return t.done; }),
                         items_.end());
            if (before != (int)items_.size()) {
                save();
                os::toast(String(before - items_.size()) + " cleared", os::Tone::Good);
            }
            if (sel_ >= (int)items_.size()) sel_ = max(0, (int)items_.size() - 1);
            os::invalidate();
            return;
        }
        if (items_.empty()) return;
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)items_.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter || k.space)  { items_[sel_].done = !items_[sel_].done; save();
                                   os::invalidate(); return; }
        if (k.is('d')) {
            items_.erase(items_.begin() + sel_);
            if (sel_ >= (int)items_.size()) sel_ = max(0, (int)items_.size() - 1);
            save();
            os::invalidate();
            return;
        }
        // Reorder: a task list is only useful if the top of it is the truth.
        if (k.left  && sel_ > 0) { std::swap(items_[sel_], items_[sel_ - 1]); sel_--; save();
                                   os::invalidate(); return; }
        if (k.right && sel_ < (int)items_.size() - 1) { std::swap(items_[sel_], items_[sel_ + 1]);
                                   sel_++; save(); os::invalidate(); return; }
    }

    void draw() override {
        if (mode_ == ADD) {
            ui::text(4, BODY_Y + 6, "What needs doing?", ui::c().dim);
            ui::inputLine(BODY_Y + 28, "> ", buf_, ui::c().fg);
            ui::hint("Enter add   ` cancel");
            return;
        }
        if (items_.empty()) {
            ui::centered(48, "Nothing to do", ui::c().dim);
            ui::centered(64, "press N to add something", ui::c().accent);
            ui::hint("N new   ` back");
            return;
        }
        std::vector<ui::Row> rows;
        for (auto& t : items_) {
            ui::Row r;
            r.label = t.text;
            r.icon = t.done ? ui::Icon::Check : ui::Icon::Cross;
            r.tint = t.done ? ui::c().dim : ui::c().fg;
            rows.push_back(r);
        }
        scroll_ = ui::listView(rows, sel_, scroll_);
        ui::hint("Enter tick  N new  D del  C clear done  S sync");
    }

private:
    struct Item { String text; bool done; };

    String asMarkdown() const {
        String md = "# Tasks\n\n";
        for (auto& t : items_) md += String(t.done ? "- [x] " : "- [ ] ") + t.text + "\n";
        return md;
    }

    void syncVault() {
        String sub = store::getStr(store::K_VAULT, "Cardputer");
        String md = asMarkdown();
        cloud::Result r;
        ui::await("Syncing tasks", [&] { r = cloud::vaultWrite(sub + "/tasks.md", md); });
        os::toast(r.ok ? "tasks synced" : r.error, r.ok ? os::Tone::Good : os::Tone::Bad);
        os::invalidate();
    }

    // Round-trips as markdown so the SD copy is directly editable elsewhere.
    void save() {
        Preferences p;
        if (p.begin("tasks", false)) { p.putString("md", asMarkdown()); p.end(); }
        if (store::sdReady()) store::writeFile("/tasks.md", asMarkdown());
    }

    void load() {
        if (loaded_) return;
        loaded_ = true;
        String md;
        if (store::sdReady() && store::exists("/tasks.md")) md = store::readFile("/tasks.md");
        if (!md.length()) {
            Preferences p;
            if (p.begin("tasks", true)) { md = p.isKey("md") ? p.getString("md", "") : String(""); p.end(); }
        }
        int i = 0;
        while (i < (int)md.length()) {
            int nl = md.indexOf('\n', i);
            if (nl < 0) nl = md.length();
            String line = md.substring(i, nl);
            line.trim();
            if (line.startsWith("- [")) {
                bool done = line.length() > 3 && (line[3] == 'x' || line[3] == 'X');
                int sp = line.indexOf("] ");
                if (sp > 0) items_.push_back({line.substring(sp + 2), done});
            }
            i = nl + 1;
        }
    }

    Mode mode_ = LIST;
    std::vector<Item> items_;
    String buf_;
    int sel_ = 0, scroll_ = 0;
    bool loaded_ = false;
};

App* tasksApp() { static Tasks a; return &a; }
