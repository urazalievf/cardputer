#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/net.h"
#include "../kernel/store.h"
#include <WiFi.h>
#include <WebServer.h>

// Serve the SD card over HTTP so a phone or laptop can pull notes off the
// device without a cable. Falls back to hosting its own access point when
// there is no network to join.
class Share : public App {
    enum Mode : uint8_t { OFF, RUNNING };
public:
    const char* name() const override { return "Share"; }
    const char* blurb() const override { return "sd over wifi"; }
    ui::Icon icon() const override { return ui::Icon::Cloud; }

    String title() const override {
        if (mode_ == RUNNING) return ap_ ? "Sharing (own AP)" : "Sharing";
        return "Share";
    }

    bool onBack() override {
        if (mode_ == RUNNING) { stop(); return true; }
        return false;
    }

    void onEnter() override { os::invalidate(); }
    void onExit() override { stop(); }

    void onKey(const KeyEvent& k) override {
        if (mode_ == RUNNING) return;
        if (k.enter || k.space) { start(false); return; }
        if (k.is('a')) { start(true); return; }
    }

    void tick() override {
        if (mode_ != RUNNING || !server_) return;
        server_->handleClient();
        if (millis() - painted_ > 1000) { painted_ = millis(); os::invalidate(); }
    }

    void draw() override {
        if (!store::sdReady()) {
            ui::centered(46, "No SD card", ui::c().bad);
            ui::centered(60, "nothing to share", ui::c().dim);
            ui::hint("` back");
            return;
        }
        if (mode_ == OFF) {
            ui::centered(34, "Serve the SD card", ui::c().fg);
            ui::centered(50, "over WiFi to any browser", ui::c().dim);
            if (net::connected())
                ui::centered(70, String("on ") + net::ssid(), ui::c().accent);
            else
                ui::centered(70, "not joined - press A for own AP", ui::c().warn);
            ui::hint("Enter start   A own hotspot   ` back");
            return;
        }

        ui::centered(28, "Open in a browser", ui::c().dim);
        ui::gfx().setTextSize(2);
        String url = String("http://") + address();
        int w = (int)url.length() * 12;
        ui::text(max(2, (SCREEN_W - w) / 2), 44, url, ui::c().accent);
        ui::gfx().setTextSize(1);
        if (ap_) {
            ui::centered(70, String("join wifi: ") + AP_SSID, ui::c().fg);
            ui::centered(82, String("password: ") + AP_PASS, ui::c().dim);
        } else {
            ui::centered(74, String(hits_) + " request(s) served", ui::c().dim);
        }
        ui::hint("` stop sharing");
    }

private:
    static constexpr const char* AP_SSID = "CardputerOS";
    static constexpr const char* AP_PASS = "cardputer";

    String address() const {
        return ap_ ? WiFi.softAPIP().toString() : net::ip();
    }

    static String htmlEscape(const String& in) {
        String out;
        for (size_t i = 0; i < in.length(); i++) {
            char c = in[i];
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '"') out += "&quot;";
            else out += c;
        }
        return out;
    }

    static String urlEncode(const String& in) {
        String out;
        for (size_t i = 0; i < in.length(); i++) {
            char c = in[i];
            if (isalnum((int)c) || strchr("-_.~/", c)) out += c;
            else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (uint8_t)c); out += b; }
        }
        return out;
    }

    void start(bool ap) {
        if (!store::sdReady()) { os::toast("no SD card", os::Tone::Bad); return; }
        ap_ = ap;
        if (ap) {
            WiFi.mode(WIFI_AP);
            if (!WiFi.softAP(AP_SSID, AP_PASS)) {
                os::toast("could not start hotspot", os::Tone::Bad);
                return;
            }
        } else if (!net::connected()) {
            os::toast("join a network first, or press A", os::Tone::Bad);
            return;
        }

        server_ = new WebServer(80);
        server_->on("/", [this]() { handleList(); });
        server_->on("/dl", [this]() { handleDownload(); });
        server_->onNotFound([this]() { handleList(); });
        server_->begin();
        hits_ = 0;
        mode_ = RUNNING;
        os::toast(String("sharing at ") + address(), os::Tone::Good);
        os::invalidate();
    }

    void stop() {
        if (server_) { server_->stop(); delete server_; server_ = nullptr; }
        if (ap_) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); ap_ = false; }
        mode_ = OFF;
        os::invalidate();
    }

    void handleList() {
        hits_++;
        String dir = server_->hasArg("dir") ? server_->arg("dir") : String("/");
        if (!dir.startsWith("/")) dir = "/" + dir;

        String page =
            "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>CardputerOS</title><style>"
            "body{font:16px/1.6 system-ui;margin:0;padding:16px;background:#0b0d12;color:#e8ecf4}"
            "a{color:#22d3ee;text-decoration:none}h1{font-size:18px}"
            "li{list-style:none;padding:8px 0;border-bottom:1px solid #2a3040}"
            "small{color:#7a8498}</style><h1>";
        page += htmlEscape(dir) + "</h1><ul>";

        if (dir != "/") {
            int slash = dir.lastIndexOf('/');
            String up = slash <= 0 ? "/" : dir.substring(0, slash);
            page += "<li><a href='/?dir=" + urlEncode(up) + "'>../</a></li>";
        }
        for (auto& e : store::listDir(dir)) {
            String full = dir.endsWith("/") ? dir + e.name : dir + "/" + e.name;
            if (e.isDir) {
                page += "<li><a href='/?dir=" + urlEncode(full) + "'>" +
                        htmlEscape(e.name) + "/</a></li>";
            } else {
                page += "<li><a href='/dl?path=" + urlEncode(full) + "'>" +
                        htmlEscape(e.name) + "</a> <small>" +
                        (e.size < 1024 ? String(e.size) + " B"
                                       : String(e.size / 1024) + " KB") + "</small></li>";
            }
        }
        page += "</ul>";
        server_->send(200, "text/html", page);
        os::invalidate();
    }

    void handleDownload() {
        hits_++;
        if (!server_->hasArg("path")) { server_->send(400, "text/plain", "no path"); return; }
        String path = server_->arg("path");
        String body = store::readFile(path);
        if (!body.length() && !store::exists(path)) {
            server_->send(404, "text/plain", "not found");
            return;
        }
        String name = path.substring(path.lastIndexOf('/') + 1);
        // Markdown and text render in the browser; anything else downloads.
        String lower = name; lower.toLowerCase();
        bool inline_ = lower.endsWith(".md") || lower.endsWith(".txt");
        server_->sendHeader("Content-Disposition",
                            String(inline_ ? "inline" : "attachment") +
                            "; filename=\"" + name + "\"");
        server_->send(200, "text/plain; charset=utf-8", body);
        os::invalidate();
    }

    Mode mode_ = OFF;
    WebServer* server_ = nullptr;
    bool ap_ = false;
    int hits_ = 0;
    uint32_t painted_ = 0;
};

App* shareApp() { static Share a; return &a; }
