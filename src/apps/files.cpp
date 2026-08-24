#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
#include <algorithm>

// SD browser with a text pager.
class Files : public App {
    enum Mode : uint8_t { BROWSE, VIEW };
public:
    const char* name() const override { return "Files"; }
    const char* blurb() const override { return "sd card"; }
    ui::Icon icon() const override { return ui::Icon::Folder; }

    String title() const override {
        if (mode_ == VIEW) return ui::ellipsize(viewName_, 26);
        if (!store::sdReady()) return "Files  no card";
        return "Files " + ui::ellipsize(path_, 20);
    }

    bool onBack() override {
        if (mode_ == VIEW) { mode_ = BROWSE; return true; }
        if (path_ != "/")  { up(); return true; }
        return false;
    }

    void onEnter() override { path_ = "/"; sel_ = 0; scroll_ = 0; mode_ = BROWSE; refresh(); }

    void onKey(const KeyEvent& k) override {
        if (mode_ == VIEW) {
            if (k.down || k.is('j')) { vscroll_++; os::invalidate(); return; }
            if (k.up   || k.is('k')) { if (vscroll_ > 0) vscroll_--; os::invalidate(); return; }
            return;
        }
        if (k.is('m')) {
            store::sdRelease();
            bool ok = store::sdAcquire();
            refresh();
            os::toast(ok ? "mounted" : "no card, or not FAT32",
                      ok ? os::Tone::Good : os::Tone::Bad);
            return;
        }
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
            ui::centered(44, "No SD card mounted", ui::c().bad);
            ui::centered(58, "insert one and press M", ui::c().dim);
            ui::centered(72, "card must be FAT32, not exFAT", ui::c().dim);
            ui::hint("M mount   ` back");
            return;
        }
        if (mode_ == VIEW) {
            ui::pager(content_, vscroll_, ui::c().fg);
            ui::hint("J/K scroll   ` back");
            return;
        }
        std::vector<ui::Row> rows;
        for (auto& e : entries_) {
            ui::Row r;
            r.label = e.name;
            r.icon = e.isDir ? ui::Icon::Folder : ui::Icon::Note;
            if (!e.isDir)
                r.detail = e.size < 1024 ? String(e.size) + "B" : String(e.size / 1024) + "K";
            r.tint = e.isDir ? ui::c().accent2 : ui::c().fg;
            rows.push_back(r);
        }
        if (rows.empty()) ui::centered(56, "(empty folder)", ui::c().dim);
        else scroll_ = ui::listView(rows, sel_, scroll_);
        ui::hint(String((int)store::sdUsedMB()) + "/" + String((int)store::sdTotalMB()) +
                 "MB   Enter open   D del   ` up");
    }

private:
    static String join(const String& dir, const String& n) {
        return dir.endsWith("/") ? dir + n : dir + "/" + n;
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
        const char* ok[] = {".md", ".txt", ".json", ".csv", ".log", ".ini", ".cfg", ".yaml", ".yml"};
        bool textual = false;
        for (auto* ext : ok) if (lower.endsWith(ext)) { textual = true; break; }
        if (!textual || e.size > 64 * 1024) {
            os::toast(String(e.size) + " bytes - no preview for this type");
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
