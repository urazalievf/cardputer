#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/store.h"
#include <algorithm>

// SD browser: make folders, make files, edit them, and move things between
// folders with cut and paste. Everything here needs a FAT32 card.
class Files : public App {
    enum Mode : uint8_t { BROWSE, VIEW, EDIT };
public:
    const char* name() const override { return "Files"; }
    const char* blurb() const override { return "sd card"; }
    ui::Icon icon() const override { return ui::Icon::Folder; }

    String title() const override {
        if (mode_ == EDIT) return "Edit " + ui::ellipsize(viewName_, 18);
        if (mode_ == VIEW) return ui::ellipsize(viewName_, 24);
        if (!store::sdReady()) return "Files  no card";
        String t = "Files " + ui::ellipsize(path_, 18);
        if (clip_.length()) t += "  [1]";
        return t;
    }

    bool onBack() override {
        if (mode_ == EDIT) { saveEdit(); return true; }
        if (mode_ == VIEW) { mode_ = BROWSE; return true; }
        if (path_ != "/")  { up(); return true; }
        return false;
    }

    void onEnter() override {
        path_ = "/"; sel_ = 0; scroll_ = 0; mode_ = BROWSE;
        sort_ = store::getInt("filesort", 0);
        refresh();
    }

    void onKey(const KeyEvent& k) override {
        switch (mode_) {
            case EDIT:
                if (ui::editBuffer(content_, k, MAXEDIT, true)) os::invalidate();
                return;
            case VIEW:
                if (k.down || k.is('j')) { vscroll_++; os::invalidate(); return; }
                if (k.up   || k.is('k')) { if (vscroll_ > 0) vscroll_--; os::invalidate(); return; }
                if (k.is('e') && editable_) { mode_ = EDIT; os::invalidate(); return; }
                return;
            case BROWSE:
                return keyBrowse(k);
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
        if (mode_ == EDIT) return drawEdit();
        if (mode_ == VIEW) {
            ui::pager(content_, vscroll_, ui::c().fg);
            ui::hint(editable_ ? "E edit   J/K scroll   ` back" : "J/K scroll   ` back");
            return;
        }

        std::vector<ui::Row> rows;
        for (auto& e : entries_) {
            ui::Row r;
            r.label = e.name;
            r.icon = e.isDir ? ui::Icon::Folder : ui::Icon::Note;
            if (!e.isDir)
                r.detail = e.size < 1024 ? String(e.size) + "B" : String(e.size / 1024) + "K";
            bool cut = clip_.length() && clip_ == join(path_, e.name);
            r.tint = cut ? ui::c().warn : e.isDir ? ui::c().accent2 : ui::c().fg;
            rows.push_back(r);
        }
        if (rows.empty()) ui::centered(52, "(empty folder)", ui::c().dim);
        else scroll_ = ui::listView(rows, sel_, scroll_);

        if (clip_.length())
            ui::hint("V paste here   X cancel   F folder   N file");
        else
            ui::hint(String("F folder N file R name X cut O ") + sortName());
    }

private:
    static const size_t MAXEDIT = 4000;

    static String join(const String& dir, const String& n) {
        return dir.endsWith("/") ? dir + n : dir + "/" + n;
    }

    static bool textual(const String& name) {
        String lower = name;
        lower.toLowerCase();
        const char* ok[] = {".md", ".txt", ".json", ".csv", ".log", ".ini",
                            ".cfg", ".yaml", ".yml", ".sh", ".py", ".c", ".h"};
        for (auto* ext : ok) if (lower.endsWith(ext)) return true;
        return lower.indexOf('.') < 0;      // extensionless files are usually text
    }

    void keyBrowse(const KeyEvent& k) {
        if (k.is('m')) {
            store::sdRelease();
            bool ok = store::sdAcquire(/*force=*/true);
            refresh();
            os::toast(ok ? "mounted" : "no card, or not FAT32",
                      ok ? os::Tone::Good : os::Tone::Bad);
            return;
        }
        if (k.is('f')) { newFolder(); return; }
        if (k.is('n')) { newFile(); return; }
        if (k.is('v')) { paste(); return; }
        if (k.is('o')) {
            sort_ = (sort_ + 1) % 4;
            store::setInt("filesort", sort_);
            String keep = entries_.empty() ? String("") : entries_[sel_].name;
            refresh();
            selectByName(keep);
            os::toast(String("sorted by ") + sortName());
            return;
        }
        if (k.is('x')) { cutOrCancel(); return; }

        if (entries_.empty()) return;
        if (k.up   || k.is('k')) { if (sel_ > 0) sel_--; os::invalidate(); return; }
        if (k.down || k.is('j')) { if (sel_ < (int)entries_.size() - 1) sel_++; os::invalidate(); return; }
        if (k.enter) { activate(); return; }
        if (k.is('r')) { renameSelected(); return; }
        if (k.is('d')) { deleteSelected(); return; }
    }

    void newFolder() {
        String name = ui::prompt("New folder name", "", 40);
        name.trim();
        if (!name.length()) return;
        if (store::makeDir(join(path_, name))) {
            os::toast("created " + name, os::Tone::Good);
            refresh();
            selectByName(name);
        } else {
            os::toast("could not create (exists?)", os::Tone::Bad);
        }
        os::invalidate();
    }

    void newFile() {
        String name = ui::prompt("New file name", ".md", 40);
        name.trim();
        if (!name.length() || name == ".md") return;
        String full = join(path_, name);
        if (store::exists(full)) { os::toast("already exists", os::Tone::Bad); return; }
        if (!store::writeFile(full, "")) { os::toast("could not create", os::Tone::Bad); return; }
        refresh();
        selectByName(name);
        // Straight into the editor: making an empty file is never the goal.
        viewName_ = name;
        viewPath_ = full;
        content_ = "";
        editable_ = true;
        mode_ = EDIT;
        os::invalidate();
    }

    void renameSelected() {
        const auto& e = entries_[sel_];
        String name = ui::prompt("Rename", e.name, 40);
        name.trim();
        if (!name.length() || name == e.name) return;
        if (store::rename(join(path_, e.name), join(path_, name))) {
            os::toast("renamed", os::Tone::Good);
            refresh();
            selectByName(name);
        } else {
            os::toast("rename failed", os::Tone::Bad);
        }
        os::invalidate();
    }

    void deleteSelected() {
        const auto& e = entries_[sel_];
        if (e.isDir) {
            if (!ui::confirm("Delete folder \"" + e.name + "\"? It must be empty.")) return;
            bool ok = store::removeDir(join(path_, e.name));
            os::toast(ok ? "folder deleted" : "not empty, or in use",
                      ok ? os::Tone::Good : os::Tone::Bad);
        } else {
            if (!ui::confirm("Delete " + e.name + "?")) return;
            bool ok = store::removeFile(join(path_, e.name));
            os::toast(ok ? "deleted" : "delete failed", ok ? os::Tone::Good : os::Tone::Bad);
        }
        refresh();
        os::invalidate();
    }

    void cutOrCancel() {
        if (clip_.length()) { clip_ = ""; os::toast("move cancelled"); os::invalidate(); return; }
        if (entries_.empty()) return;
        clip_ = join(path_, entries_[sel_].name);
        clipName_ = entries_[sel_].name;
        os::toast("cut " + clipName_ + " - open a folder and press V");
        os::invalidate();
    }

    void paste() {
        if (!clip_.length()) return;
        String dest = join(path_, clipName_);
        if (dest == clip_) { clip_ = ""; os::invalidate(); return; }
        if (store::exists(dest)) { os::toast("name already used here", os::Tone::Bad); return; }
        // SD.rename moves within the card; there is no copy, which is fine
        // because moving is what you actually want on 8GB of flash.
        bool ok = store::rename(clip_, dest);
        os::toast(ok ? "moved " + clipName_ : "move failed",
                  ok ? os::Tone::Good : os::Tone::Bad);
        if (ok) clip_ = "";
        refresh();
        selectByName(clipName_);
        os::invalidate();
    }

    void selectByName(const String& name) {
        for (size_t i = 0; i < entries_.size(); i++)
            if (entries_[i].name == name) { sel_ = i; return; }
    }

    const char* sortName() const {
        static const char* names[] = {"name", "size", "type", "newest"};
        return names[sort_ & 3];
    }

    void refresh() {
        entries_ = store::listDir(path_);
        int mode = sort_;
        // Folders always float to the top; the mode only orders within a group.
        std::sort(entries_.begin(), entries_.end(),
                  [mode](const store::Entry& a, const store::Entry& b) {
                      if (a.isDir != b.isDir) return a.isDir;
                      switch (mode) {
                          case 1: return a.size > b.size;
                          case 2: {
                              String ea = a.name.substring(a.name.lastIndexOf('.') + 1);
                              String eb = b.name.substring(b.name.lastIndexOf('.') + 1);
                              ea.toLowerCase(); eb.toLowerCase();
                              if (ea != eb) return ea < eb;
                              return a.name < b.name;
                          }
                          case 3: if (a.mtime != b.mtime) return a.mtime > b.mtime;
                                  return a.name < b.name;
                          default: return a.name < b.name;
                      }
                  });
        if (sel_ >= (int)entries_.size()) sel_ = max(0, (int)entries_.size() - 1);
        os::invalidate();
    }

    void up() {
        int slash = path_.lastIndexOf('/');
        String leaving = path_.substring(slash + 1);
        path_ = slash <= 0 ? "/" : path_.substring(0, slash);
        sel_ = 0; scroll_ = 0;
        refresh();
        selectByName(leaving);
    }

    void activate() {
        auto e = entries_[sel_];
        if (e.isDir) { path_ = join(path_, e.name); sel_ = 0; scroll_ = 0; refresh(); return; }
        if (e.size > 60 * 1024) {
            os::toast(String(e.size / 1024) + "KB - too big to open here");
            return;
        }
        viewPath_ = join(path_, e.name);
        viewName_ = e.name;
        content_ = store::readFile(viewPath_);
        editable_ = textual(e.name) && e.size <= MAXEDIT;
        vscroll_ = 0;
        mode_ = VIEW;
        os::invalidate();
    }

    void drawEdit() {
        auto lines = ui::wrap(content_);
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
        ui::progress(3, HINT_Y - 10, SCREEN_W - 6, 4,
                     (float)content_.length() / MAXEDIT, ui::c().accent);
        ui::hint("` save & close   ctrl+Bksp word");
    }

    void saveEdit() {
        bool ok = store::writeFile(viewPath_, content_);
        os::toast(ok ? "saved " + viewName_ : "save failed",
                  ok ? os::Tone::Good : os::Tone::Bad);
        mode_ = BROWSE;
        refresh();
        selectByName(viewName_);
    }

    Mode mode_ = BROWSE;
    String path_ = "/", content_, viewName_, viewPath_, clip_, clipName_;
    std::vector<store::Entry> entries_;
    int sel_ = 0, scroll_ = 0, vscroll_ = 0, sort_ = 0;
    bool editable_ = false;
};

App* filesApp() { static Files a; return &a; }
