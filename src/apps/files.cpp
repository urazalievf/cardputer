#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/store.h"
#include <algorithm>

// SD card browser. Text files open in a pager; everything else shows its size.
class Files : public App {
    enum Mode { BROWSE, VIEW };
public:
    const char* name() const override { return "Files"; }
    const char* blurb() const override { return "sd card"; }

    String title() const override {
        if (mode_ == VIEW) return ui::ellipsize(viewName_, 30);
        if (!store::sdReady()) return "Files  no card";
        return "Files " + ui::ellipsize(path_, 24);
    }

    bool escExits() const override { return mode_ == BROWSE && path_ == "/"; }

    void onEnter() override { path_ = "/"; sel_ = 0; scroll_ = 0; mode_ = BROWSE; refresh(); }

    void onKey(const KeyEvent& k) override {
        if (mode_ == VIEW) {
            if (k.esc)               { mode_ = BROWSE; os::invalidate(); return; }
            if (k.down || k.is('j')) { vscroll_++; os::invalidate(); return; }
            if (k.up   || k.is('k')) { if (vscroll_ > 0) vscroll_--; os::invalidate(); return; }
            return;
        }
        if (k.esc) { up(); return; }
        if (k.is('m')) { store::sdMount(); refresh(); os::toast(store::sdReady() ? "mounted" : "no card"); return; }
        if (entries_.empty()) return;
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)entries_.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter) { activate(); return; }
        if (k.is('d')) {
            auto& e = entries_[sel_];
            if (!e.isDir && ui::confirm("Delete " + e.name + "?")) {
                store::removeFile(join(path_, e.name));
                refresh();
            }
            os::invalidate();
        }
    }

    void draw() override {
        if (!store::sdReady()) {
            ui::centered(50, "No SD card", ui::BAD);
            ui::centered(64, "insert and press M", ui::DIM);
            ui::hint("M mount   ` back");
            return;
        }
        if (mode_ == VIEW) {
            auto lines = ui::wrap(content_, CHARS_PER_LINE);
            if (vscroll_ > (int)lines.size() - 1) vscroll_ = max(0, (int)lines.size() - 1);
            for (int i = 0; i < BODY_ROWS && vscroll_ + i < (int)lines.size(); i++)
                ui::text(2, BODY_Y + i * ROW_H, lines[vscroll_ + i], ui::FG);
            ui::hint("J/K scroll   ` back");
            return;
        }

        std::vector<String> rows;
        for (auto& e : entries_) {
            if (e.isDir) rows.push_back("/ " + e.name);
            else {
                String r = "  " + ui::ellipsize(e.name, 27);
                while (r.length() < 30) r += ' ';
                r += e.size < 1024 ? String(e.size) + "B" : String(e.size / 1024) + "K";
                rows.push_back(r);
            }
        }
        if (rows.empty()) ui::centered(55, "(empty)", ui::DIM);
        else scroll_ = ui::drawList(rows, sel_, scroll_, 9, BODY_Y);
        ui::hint(String((int)store::sdUsedMB()) + "/" + String((int)store::sdTotalMB()) +
                 "MB  Enter open  D del  ` up");
    }

private:
    static String join(const String& dir, const String& name) {
        return dir.endsWith("/") ? dir + name : dir + "/" + name;
    }

    void refresh() {
        entries_ = store::listDir(path_);
        std::sort(entries_.begin(), entries_.end(),
                  [](const store::Entry& a, const store::Entry& b) {
                      if (a.isDir != b.isDir) return a.isDir;
                      return a.name < b.name;
                  });
        if (sel_ >= (int)entries_.size()) sel_ = max(0, (int)entries_.size() - 1);
        os::invalidate();
    }

    void up() {
        if (path_ == "/") return;
        int slash = path_.lastIndexOf('/');
        path_ = slash <= 0 ? "/" : path_.substring(0, slash);
        sel_ = 0; scroll_ = 0;
        refresh();
    }

    void activate() {
        auto e = entries_[sel_];
        if (e.isDir) { path_ = join(path_, e.name); sel_ = 0; scroll_ = 0; refresh(); return; }
        String lower = e.name;
        lower.toLowerCase();
        bool textual = lower.endsWith(".md") || lower.endsWith(".txt") || lower.endsWith(".json") ||
                       lower.endsWith(".csv") || lower.endsWith(".log") || lower.endsWith(".ini") ||
                       lower.endsWith(".cfg") || lower.endsWith(".yaml") || lower.endsWith(".yml");
        if (!textual || e.size > 64 * 1024) {
            os::toast(String(e.size) + " bytes - not previewable");
            return;
        }
        content_ = store::readFile(join(path_, e.name));
        viewName_ = e.name;
        vscroll_ = 0;
        mode_ = VIEW;
        os::invalidate();
    }

    Mode mode_ = BROWSE;
    String path_ = "/", content_, viewName_;
    std::vector<store::Entry> entries_;
    int sel_ = 0, scroll_ = 0, vscroll_ = 0;
};

App* filesApp() { static Files a; return &a; }
