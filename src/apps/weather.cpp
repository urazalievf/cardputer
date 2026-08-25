#include "apps.h"
#include "../kernel/ui.h"
#include "../kernel/theme.h"
#include "../kernel/net.h"
#include "../kernel/store.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../kernel/net.h"

// Weather with no account and no API key, over plain HTTP — so it costs none
// of the ~45KB a TLS handshake would want. open-meteo for the forecast,
// ip-api for "where am I" when no place is set.
class Weather : public App {
    enum Mode : uint8_t { VIEW, PLACE };
public:
    const char* name() const override { return "Weather"; }
    const char* blurb() const override { return "forecast"; }
    ui::Icon icon() const override { return ui::Icon::Cloud; }

    String title() const override {
        if (mode_ == PLACE) return "Location";
        return place_.length() ? ui::ellipsize(place_, 22) : String("Weather");
    }

    bool onBack() override {
        if (mode_ == PLACE) { mode_ = VIEW; return true; }
        return false;
    }

    void onEnter() override {
        mode_ = VIEW;
        place_ = store::getStr("wxname", "");
        if (!have_ || millis() - fetched_ > 15UL * 60UL * 1000UL) fetch();
        os::invalidate();
    }

    void onKey(const KeyEvent& k) override {
        if (mode_ == PLACE) {
            if (k.enter) {
                store::setStr("wxplace", buf_);
                store::remove("wxlat");        // force a re-geocode
                mode_ = VIEW;
                fetch();
                return;
            }
            if (ui::editBuffer(buf_, k, 60)) os::invalidate();
            return;
        }
        if (k.is('r')) { fetch(); return; }
        if (k.is('l')) { buf_ = store::getStr("wxplace", ""); mode_ = PLACE; os::invalidate(); return; }
        if (k.is('u')) {
            metric_ = !metric_;
            store::setInt("wxmetric", metric_);
            fetch();
            return;
        }
    }

    void draw() override {
        if (mode_ == PLACE) {
            ui::text(4, BODY_Y + 6, "City name, or leave blank to", ui::c().dim);
            ui::text(4, BODY_Y + 17, "use your network location.", ui::c().dim);
            ui::inputLine(BODY_Y + 40, "> ", buf_, ui::c().fg);
            ui::hint("Enter save   ` cancel");
            return;
        }
        if (err_.length()) {
            ui::icon(4, BODY_Y + 4, ui::Icon::Cross, ui::c().bad);
            ui::text(18, BODY_Y + 4, "Could not fetch", ui::c().bad);
            ui::pager(err_, 0, ui::c().dim, 4, BODY_Y + 20);
            ui::hint("R retry   L place   ` back");
            return;
        }
        if (!have_) {
            ui::spinner(SCREEN_W / 2, 54, ui::c().accent);
            ui::centered(76, "Checking the sky", ui::c().dim);
            ui::hint("");
            return;
        }

        const char* unit = metric_ ? "C" : "F";

        ui::gfx().setTextSize(3);
        String big = String((int)lroundf(temp_)) + unit;
        ui::text(6, BODY_Y + 6, big, ui::c().accent);
        ui::gfx().setTextSize(1);

        int rx = 6 + (int)big.length() * 18 + 8;
        ui::text(rx, BODY_Y + 6, ui::ellipsize(cond_, (SCREEN_W - rx) / 6), ui::c().fg);
        ui::text(rx, BODY_Y + 18, String("feels ") + (int)lroundf(feels_) + unit, ui::c().dim);

        ui::text(6, BODY_Y + 34, String("humidity ") + humidity_ + "%", ui::c().dim);
        ui::text(6, BODY_Y + 45, String("wind ") + String(wind_, 0) +
                                 (metric_ ? " km/h" : " mph"), ui::c().dim);
        ui::gfx().drawFastHLine(0, BODY_Y + 58, SCREEN_W, ui::c().border);

        int y = BODY_Y + 63;
        for (int i = 0; i < days_ && y < HINT_Y - 12; i++) {
            ui::text(6, y, dayName_[i], ui::c().accent2);
            String range = String((int)lroundf(dayMin_[i])) + " / " +
                           String((int)lroundf(dayMax_[i])) + unit;
            ui::text(52, y, range, ui::c().fg);
            ui::text(110, y, ui::ellipsize(dayCond_[i], 21), ui::c().dim);
            y += 11;
        }
        ui::hint("R refresh   L place   U " + String(metric_ ? "to F" : "to C"));
    }

private:
    // WMO weather interpretation codes, collapsed to what fits on this screen.
    static String wmo(int code) {
        switch (code) {
            case 0:  return "clear";
            case 1:  return "mostly clear";
            case 2:  return "partly cloudy";
            case 3:  return "overcast";
            case 45: case 48: return "fog";
            case 51: case 53: case 55: return "drizzle";
            case 56: case 57: return "freezing drizzle";
            case 61: return "light rain";
            case 63: return "rain";
            case 65: return "heavy rain";
            case 66: case 67: return "freezing rain";
            case 71: return "light snow";
            case 73: return "snow";
            case 75: return "heavy snow";
            case 77: return "snow grains";
            case 80: case 81: return "showers";
            case 82: return "violent showers";
            case 85: case 86: return "snow showers";
            case 95: return "thunderstorm";
            case 96: case 99: return "storm with hail";
            default: return "code " + String(code);
        }
    }

    static bool getJson(const String& url, JsonDocument& doc, String& err) {
        HTTPClient http;
        http.setConnectTimeout(6000);
        http.setTimeout(15000);
        http.setReuse(false);
        if (!http.begin(url)) { err = "connect failed"; return false; }
        int code = http.GET();
        if (code != 200) {
            err = String("HTTP ") + code;
            http.end();
            os::logf("weather: %s -> %s", url.c_str(), err.c_str());
            return false;
        }
        // getString() de-chunks; getStream() does not, and open-meteo replies
        // with chunked transfer encoding.
        String body = http.getString();
        http.end();
        DeserializationError e = deserializeJson(doc, body);
        if (e) {
            err = String("bad json: ") + e.c_str();
            os::logf("weather: parse failed (%d bytes): %s", body.length(), e.c_str());
            return false;
        }
        return true;
    }

    // Resolve a place once, then cache the coordinates: geocoding a city that
    // hasn't moved on every refresh is a waste of a round trip.
    bool resolveLocation(String& err) {
        String want = store::getStr("wxplace", "");
        String cachedFor = store::getStr("wxfor", "\x01");
        if (store::getStr("wxlat", "").length() && cachedFor == want) {
            lat_ = store::getStr("wxlat", "0").toFloat();
            lon_ = store::getStr("wxlon", "0").toFloat();
            place_ = store::getStr("wxname", "");
            return true;
        }

        JsonDocument doc;
        if (want.length()) {
            String q = want;
            q.replace(" ", "+");
            if (!getJson("http://geocoding-api.open-meteo.com/v1/search?count=1&name=" + q,
                         doc, err)) return false;
            if (!doc["results"][0]["latitude"].is<float>()) { err = "no such place"; return false; }
            lat_ = doc["results"][0]["latitude"].as<float>();
            lon_ = doc["results"][0]["longitude"].as<float>();
            place_ = doc["results"][0]["name"].as<String>();
            String admin = doc["results"][0]["admin1"].as<String>();
            if (admin.length()) place_ += ", " + admin;
        } else {
            if (!getJson("http://ip-api.com/json/?fields=city,regionName,lat,lon", doc, err))
                return false;
            if (!doc["lat"].is<float>()) { err = "no location from IP"; return false; }
            lat_ = doc["lat"].as<float>();
            lon_ = doc["lon"].as<float>();
            place_ = doc["city"].as<String>();
            String region = doc["regionName"].as<String>();
            if (region.length()) place_ += ", " + region;
        }

        store::setStr("wxlat", String(lat_, 4));
        store::setStr("wxlon", String(lon_, 4));
        store::setStr("wxname", place_);
        store::setStr("wxfor", want);
        return true;
    }

    void fetch() {
        err_ = "";
        have_ = false;
        metric_ = store::getInt("wxmetric", 0) != 0;
        if (!net::connected()) { err_ = "no wifi"; os::invalidate(); return; }
        String err;
        bool located = false;
        ui::await("Finding you", [&] { located = resolveLocation(err); });
        if (!located) { err_ = err; os::invalidate(); return; }

        String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(lat_, 4) +
                     "&longitude=" + String(lon_, 4) +
                     "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
                     "wind_speed_10m,weather_code"
                     "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                     "&forecast_days=4&timezone=auto";
        if (!metric_) url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";

        JsonDocument doc;
        bool got = false;
        ui::await("Checking the sky", [&] { got = getJson(url, doc, err); });
        if (!got) { err_ = err; os::invalidate(); return; }

        JsonObject cur = doc["current"];
        temp_ = cur["temperature_2m"].as<float>();
        feels_ = cur["apparent_temperature"].as<float>();
        humidity_ = cur["relative_humidity_2m"].as<int>();
        wind_ = cur["wind_speed_10m"].as<float>();
        cond_ = wmo(cur["weather_code"].as<int>());

        JsonArray codes = doc["daily"]["weather_code"].as<JsonArray>();
        JsonArray maxs = doc["daily"]["temperature_2m_max"].as<JsonArray>();
        JsonArray mins = doc["daily"]["temperature_2m_min"].as<JsonArray>();
        JsonArray dates = doc["daily"]["time"].as<JsonArray>();
        days_ = 0;
        for (size_t i = 1; i < codes.size() && days_ < 3; i++) {   // skip today
            dayCond_[days_] = wmo(codes[i].as<int>());
            dayMax_[days_] = maxs[i].as<float>();
            dayMin_[days_] = mins[i].as<float>();
            dayName_[days_] = weekday(dates[i].as<String>());
            days_++;
        }

        have_ = true;
        fetched_ = millis();
        os::invalidate();
    }

    // "2026-08-25" -> "Tue", by Sakamoto's day-of-week algorithm.
    static String weekday(const String& iso) {
        if (iso.length() < 10) return "?";
        int y = iso.substring(0, 4).toInt();
        int m = iso.substring(5, 7).toInt();
        int d = iso.substring(8, 10).toInt();
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (m < 3) y -= 1;
        int dow = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
        static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        return names[dow < 0 ? 0 : dow];
    }

    Mode mode_ = VIEW;
    String err_, buf_, place_, cond_;
    float lat_ = 0, lon_ = 0, temp_ = 0, feels_ = 0, wind_ = 0;
    int humidity_ = 0, days_ = 0;
    String dayCond_[3], dayName_[3];
    float dayMax_[3] = {0}, dayMin_[3] = {0};
    bool have_ = false, metric_ = false;
    uint32_t fetched_ = 0;
};

App* weatherApp() { static Weather a; return &a; }
