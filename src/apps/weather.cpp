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

        // The headline: temperature large on the left, the sky itself on the
        // right. The icon is the thing you read from across the room.
        drawSky(SCREEN_W - 30, BODY_Y + 21, 16, wmoSky(code_), day_);

        ui::gfx().setTextSize(3);
        String big = String((int)lroundf(temp_)) + unit;
        ui::text(6, BODY_Y + 5, big, ui::c().accent);
        ui::gfx().setTextSize(1);

        // Wrapped to the icon's left edge rather than the panel's, so a long
        // condition never runs underneath it.
        int room = (SCREEN_W - 52 - 6) / 6;
        ui::text(6, BODY_Y + 32, ui::ellipsize(cond_, room), ui::c().fg);
        ui::text(6, BODY_Y + 42, String("feels ") + (int)lroundf(feels_) + unit +
                                 "   hum " + humidity_ + "%", ui::c().dim);
        ui::text(6, BODY_Y + 52, String("wind ") + String(wind_, 0) +
                                 (metric_ ? " km/h" : " mph"), ui::c().dim);
        ui::gfx().drawFastHLine(0, BODY_Y + 62, SCREEN_W, ui::c().border);

        int y = BODY_Y + 66;
        for (int i = 0; i < days_ && y + 12 < HINT_Y - 3; i++) {
            ui::text(6, y, dayName_[i], ui::c().accent2);
            // Daytime shape for a whole-day forecast: a moon on a Tuesday
            // would be saying something the forecast does not.
            drawSky(38, y + 4, 6, wmoSky(dayCode_[i]), true);
            String range = String((int)lroundf(dayMin_[i])) + " / " +
                           String((int)lroundf(dayMax_[i])) + unit;
            ui::text(52, y, range, ui::c().fg);
            ui::text(110, y, ui::ellipsize(dayCond_[i], 21), ui::c().dim);
            y += 12;
        }
        ui::hint("R refresh   L place   U " + String(metric_ ? "to F" : "to C"));
    }

private:
    // The nine shapes worth drawing. Anything finer than this is illegible at
    // 13 pixels, and the word next to the icon carries the detail.
    enum class Sky : uint8_t {
        Clear, MostlyClear, PartlyCloudy, Overcast, Fog,
        Drizzle, Rain, Snow, Storm,
    };

    static Sky wmoSky(int code) {
        switch (code) {
            case 0:  return Sky::Clear;
            case 1:  return Sky::MostlyClear;
            case 2:  return Sky::PartlyCloudy;
            case 3:  return Sky::Overcast;
            case 45: case 48: return Sky::Fog;
            case 51: case 53: case 55: case 56: case 57: return Sky::Drizzle;
            case 61: case 63: case 65: case 66: case 67:
            case 80: case 81: case 82: return Sky::Rain;
            case 71: case 73: case 75: case 77: case 85: case 86: return Sky::Snow;
            case 95: case 96: case 99: return Sky::Storm;
            default: return Sky::Overcast;
        }
    }

    // Drawn from primitives at any radius, like the rest of the icon set: no
    // bitmaps to store, no second copy for the forecast rows, and they follow
    // the theme. `r` is half the icon box; 16 for the headline, 6 for a row.
    static void sun(int cx, int cy, int r, uint16_t col) {
        auto& g = ui::gfx();
        int body = max(2, r * 55 / 100);
        g.fillCircle(cx, cy, body, col);
        // Rays only once there is room for them to read as rays.
        if (r < 6) return;
        for (int i = 0; i < 8; i++) {
            float a = i * (float)PI / 4.0f;
            int x0 = cx + (int)(cosf(a) * (body + 2)), y0 = cy + (int)(sinf(a) * (body + 2));
            int x1 = cx + (int)(cosf(a) * (r + 1)),    y1 = cy + (int)(sinf(a) * (r + 1));
            g.drawLine(x0, y0, x1, y1, col);
        }
    }

    // A crescent is a disc with a second disc bitten out of it in the page
    // colour, which is why this has to be drawn before anything overlaps it.
    static void moon(int cx, int cy, int r, uint16_t col) {
        auto& g = ui::gfx();
        int body = max(2, r * 62 / 100);
        g.fillCircle(cx, cy, body, col);
        g.fillCircle(cx + body / 2, cy - body / 2, body, ui::c().bg);
    }

    static void cloud(int cx, int cy, int r, uint16_t col) {
        auto& g = ui::gfx();
        int big = max(2, r * 52 / 100);
        int left = max(1, r * 38 / 100);
        int right = max(1, r * 34 / 100);
        g.fillCircle(cx - r / 3, cy + r / 8, left, col);
        g.fillCircle(cx + r / 8, cy - r / 6, big, col);
        g.fillCircle(cx + r / 2, cy + r / 6, right, col);
        g.fillRect(cx - r / 3, cy + r / 8, r * 5 / 6, max(1, left), col);
    }

    // Streaks for rain, shorter and sparser for drizzle.
    static void fall(int cx, int cy, int r, uint16_t col, int count, int len) {
        auto& g = ui::gfx();
        int step = max(2, r / 2);
        int x = cx - step * (count - 1) / 2;
        for (int i = 0; i < count; i++, x += step)
            g.drawLine(x + 1, cy, x - 1, cy + len, col);
    }

    static void flakes(int cx, int cy, int r, uint16_t col, int count) {
        auto& g = ui::gfx();
        int step = max(2, r / 2);
        int x = cx - step * (count - 1) / 2;
        int arm = r < 8 ? 1 : 2;
        for (int i = 0; i < count; i++, x += step) {
            int y = cy + (i % 2 ? arm + 1 : 0);
            g.drawFastHLine(x - arm, y, arm * 2 + 1, col);
            g.drawFastVLine(x, y - arm, arm * 2 + 1, col);
        }
    }

    static void bolt(int cx, int cy, int r, uint16_t col) {
        auto& g = ui::gfx();
        int h = max(3, r * 2 / 3), w = max(2, r / 3);
        g.fillTriangle(cx + w / 2, cy, cx - w, cy + h / 2, cx, cy + h / 2, col);
        g.fillTriangle(cx, cy + h / 2, cx + w, cy + h / 2, cx - w / 2, cy + h, col);
    }

    // One entry point so the headline and the forecast rows can never drift.
    static void drawSky(int cx, int cy, int r, Sky s, bool day) {
        const auto& p = ui::c();
        uint16_t sunCol = p.warn, cloudCol = p.dim, wetCol = p.accent2, snowCol = p.fg;
        switch (s) {
            case Sky::Clear:
                day ? sun(cx, cy, r, sunCol) : moon(cx, cy, r, p.fg);
                break;
            case Sky::MostlyClear:
                // Sun up and left, a small cloud tucked under it.
                day ? sun(cx - r / 4, cy - r / 4, r * 3 / 4, sunCol)
                    : moon(cx - r / 4, cy - r / 4, r * 3 / 4, p.fg);
                cloud(cx + r / 3, cy + r / 3, r * 2 / 3, cloudCol);
                break;
            case Sky::PartlyCloudy:
                day ? sun(cx - r / 2, cy - r / 2, r * 2 / 3, sunCol)
                    : moon(cx - r / 2, cy - r / 2, r * 2 / 3, p.fg);
                cloud(cx + r / 6, cy + r / 5, r * 5 / 6, cloudCol);
                break;
            case Sky::Overcast:
                cloud(cx, cy, r, cloudCol);
                break;
            case Sky::Fog: {
                cloud(cx, cy - r / 4, r * 4 / 5, cloudCol);
                auto& g = ui::gfx();
                for (int i = 0; i < 3; i++)
                    g.drawFastHLine(cx - r + (i % 2) * 2, cy + r / 2 + i * max(2, r / 4),
                                    r * 2 - 2, p.border);
                break;
            }
            case Sky::Drizzle:
                cloud(cx, cy - r / 4, r * 4 / 5, cloudCol);
                fall(cx, cy + r / 2, r, wetCol, 3, max(2, r / 3));
                break;
            case Sky::Rain:
                cloud(cx, cy - r / 4, r * 4 / 5, cloudCol);
                fall(cx, cy + r / 2, r, wetCol, 4, max(3, r * 2 / 3));
                break;
            case Sky::Snow:
                cloud(cx, cy - r / 4, r * 4 / 5, cloudCol);
                flakes(cx, cy + r * 3 / 5, r, snowCol, 3);
                break;
            case Sky::Storm:
                cloud(cx, cy - r / 4, r * 4 / 5, cloudCol);
                bolt(cx, cy + r / 3, r, sunCol);
                break;
        }
    }

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
                     "wind_speed_10m,weather_code,is_day"
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
        code_ = cur["weather_code"].as<int>();
        cond_ = wmo(code_);
        // Absent on a cached or trimmed reply; daylight is the safer default
        // because a sun drawn at night is a smaller lie than a moon at noon.
        day_ = cur["is_day"].is<int>() ? cur["is_day"].as<int>() != 0 : true;

        JsonArray codes = doc["daily"]["weather_code"].as<JsonArray>();
        JsonArray maxs = doc["daily"]["temperature_2m_max"].as<JsonArray>();
        JsonArray mins = doc["daily"]["temperature_2m_min"].as<JsonArray>();
        JsonArray dates = doc["daily"]["time"].as<JsonArray>();
        days_ = 0;
        for (size_t i = 1; i < codes.size() && days_ < 3; i++) {   // skip today
            dayCode_[days_] = codes[i].as<int>();
            dayCond_[days_] = wmo(dayCode_[days_]);
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
    int humidity_ = 0, days_ = 0, code_ = 0;
    bool day_ = true;
    int dayCode_[3] = {0};
    String dayCond_[3], dayName_[3];
    float dayMax_[3] = {0}, dayMin_[3] = {0};
    bool have_ = false, metric_ = false;
    uint32_t fetched_ = 0;
};

App* weatherApp() { static Weather a; return &a; }
