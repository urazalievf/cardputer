#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/audio.h"
#include "../kernel/store.h"

// Stopwatch, countdown and pomodoro. The one app that has to keep running
// while you look away, so it draws only when a displayed digit changes.
class Timer : public App {
    enum Kind : uint8_t { STOPWATCH, COUNTDOWN, POMODORO };
public:
    const char* name() const override { return "Timer"; }
    const char* blurb() const override { return "focus"; }
    ui::Icon icon() const override { return ui::Icon::Clock; }

    String title() const override {
        switch (kind_) {
            case STOPWATCH: return "Stopwatch";
            case COUNTDOWN: return "Countdown  " + mmss(preset_);
            default:        return pomoBreak_ ? "Pomodoro  break" : "Pomodoro  focus";
        }
    }

    void onEnter() override {
        preset_ = store::getInt("timerlen", 300);
        pomoWork_ = store::getInt("pomowork", 25 * 60);
        pomoRest_ = store::getInt("pomorest", 5 * 60);
        os::invalidate();
    }
    void onExit() override { running_ = false; }

    void onKey(const KeyEvent& k) override {
        if (k.space || k.enter) { toggle(); return; }
        if (k.is('r')) { reset(); return; }
        if (k.tab) { kind_ = (Kind)((kind_ + 1) % 3); reset(); os::invalidate(); return; }
        if (kind_ == COUNTDOWN && !running_) {
            if (k.up   || k.is('k')) { preset_ = min(preset_ + 60, 5999); persist(); return; }
            if (k.down || k.is('j')) { preset_ = max(preset_ - 60, 60);   persist(); return; }
            if (k.right)             { preset_ = min(preset_ + 10, 5999); persist(); return; }
            if (k.left)              { preset_ = max(preset_ - 10, 10);   persist(); return; }
        }
    }

    void tick() override {
        if (!running_) return;
        uint32_t now = millis();
        int shown = displaySeconds(now);
        if (shown != lastShown_) { lastShown_ = shown; os::invalidate(); }

        if (kind_ != STOPWATCH && shown <= 0) {
            fire();
        }
    }

    void draw() override {
        int secs = displaySeconds(millis());

        // Progress ring is overkill on 240x135; a wide bar reads better.
        if (kind_ != STOPWATCH) {
            int total = (kind_ == COUNTDOWN) ? preset_ : (pomoBreak_ ? pomoRest_ : pomoWork_);
            float frac = total > 0 ? (float)(total - secs) / total : 0;
            ui::progress(12, 30, SCREEN_W - 24, 8,
                         frac, pomoBreak_ ? ui::c().good : ui::c().accent);
        }

        ui::gfx().setTextSize(4);
        String big = mmss(secs < 0 ? 0 : secs);
        int w = (int)big.length() * 24;
        ui::text((SCREEN_W - w) / 2, 48, big,
                 running_ ? ui::c().fg : ui::c().dim);
        ui::gfx().setTextSize(1);

        if (kind_ == STOPWATCH && laps_.size()) {
            String l = "lap " + String((int)laps_.size()) + ": " + mmss(laps_.back());
            ui::centered(90, l, ui::c().accent2);
        } else if (kind_ == POMODORO) {
            ui::centered(90, String("round ") + (rounds_ + 1), ui::c().accent2);
        } else if (kind_ == COUNTDOWN && !running_) {
            ui::centered(90, "arrows adjust  +/- 1m, </> 10s", ui::c().dim);
        }

        ui::hint(String(running_ ? "Space pause" : "Space start") +
                 "   R reset   TAB mode   ` back");
    }

private:
    static String mmss(int s) {
        if (s < 0) s = 0;
        char b[12];
        if (s >= 3600) snprintf(b, sizeof(b), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
        else           snprintf(b, sizeof(b), "%02d:%02d", s / 60, s % 60);
        return b;
    }

    int elapsed(uint32_t now) const {
        return (int)((accum_ + (running_ ? now - started_ : 0)) / 1000);
    }

    int displaySeconds(uint32_t now) const {
        if (kind_ == STOPWATCH) return elapsed(now);
        int total = (kind_ == COUNTDOWN) ? preset_ : (pomoBreak_ ? pomoRest_ : pomoWork_);
        return total - elapsed(now);
    }

    void toggle() {
        if (running_) {
            accum_ += millis() - started_;
            running_ = false;
        } else {
            started_ = millis();
            running_ = true;
        }
        os::invalidate();
    }

    void reset() {
        running_ = false;
        accum_ = 0;
        laps_.clear();
        if (kind_ == POMODORO) { pomoBreak_ = false; rounds_ = 0; }
        lastShown_ = -1;
        os::invalidate();
    }

    void fire() {
        running_ = false;
        accum_ = 0;
        if (theme::sounds()) {
            for (int i = 0; i < 3; i++) { audio::beep(1400, 90); delay(70); }
        }
        if (kind_ == POMODORO) {
            pomoBreak_ = !pomoBreak_;
            if (!pomoBreak_) rounds_++;
            os::toast(pomoBreak_ ? "break time" : "back to work", os::Tone::Good);
            started_ = millis();
            running_ = true;           // roll straight into the next phase
        } else {
            os::toast("time's up", os::Tone::Good);
        }
        lastShown_ = -1;
        os::invalidate();
    }

    void persist() {
        store::setInt("timerlen", preset_);
        lastShown_ = -1;
        os::invalidate();
    }

    Kind kind_ = STOPWATCH;
    bool running_ = false, pomoBreak_ = false;
    uint32_t started_ = 0, accum_ = 0;
    int preset_ = 300, pomoWork_ = 1500, pomoRest_ = 300;
    int rounds_ = 0, lastShown_ = -1;
    std::vector<int> laps_;
};

App* timerApp() { static Timer a; return &a; }
