#include "weather_service.h"
#include "settings.h"
#include "wifi_station.h"
#include "board.h"

#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "WeatherService"

WeatherService::WeatherService() : last_update_time_(0) {
    weather_info_.valid = false;
}

bool WeatherService::NeedsUpdate() const {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    return (current_time - last_update_time_) >= WEATHER_UPDATE_INTERVAL_MS;
}

std::string WeatherService::UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        escaped << std::uppercase << '%' << std::setw(2) << int((unsigned char)c) << std::nouppercase;
    }
    return escaped.str();
}

std::string WeatherService::DayNameFromDate(const std::string& ymd) {
    // ymd = "2026-11-04"
    int y = 0, m = 0, d = 0;
    if (sscanf(ymd.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return "";
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    static const char* wdays[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
    return wdays[t.tm_wday % 7];
}

// WMO weather code -> OpenWeatherMap-style icon code (used by WeatherUI::GetWeatherIcon)
const char* WeatherService::WmoToIconCode(int wmo, bool is_day) {
    const char* base;
    if (wmo == 0) base = "01";
    else if (wmo == 1) base = "02";
    else if (wmo == 2) base = "03";
    else if (wmo == 3) base = "04";
    else if (wmo == 45 || wmo == 48) base = "50";
    else if (wmo >= 51 && wmo <= 57) base = "09";
    else if (wmo >= 61 && wmo <= 67) base = "10";
    else if (wmo >= 71 && wmo <= 77) base = "13";
    else if (wmo >= 80 && wmo <= 82) base = "09";
    else if (wmo == 85 || wmo == 86) base = "13";
    else if (wmo >= 95) base = "11";
    else base = "03";
    static char buf[4];
    snprintf(buf, sizeof(buf), "%s%c", base, is_day ? 'd' : 'n');
    return buf;
}

const char* WeatherService::WmoToVietnamese(int wmo) {
    switch (wmo) {
        case 0: return "Trời quang";
        case 1: return "Ít mây";
        case 2: return "Có mây";
        case 3: return "Nhiều mây";
        case 45: case 48: return "Sương mù";
        case 51: case 53: case 55: return "Mưa phùn";
        case 56: case 57: return "Mưa phùn lạnh";
        case 61: return "Mưa nhỏ";
        case 63: return "Mưa vừa";
        case 65: return "Mưa to";
        case 66: case 67: return "Mưa lạnh";
        case 71: case 73: case 75: case 77: return "Tuyết";
        case 80: return "Mưa rào nhẹ";
        case 81: return "Mưa rào";
        case 82: return "Mưa rào to";
        case 85: case 86: return "Mưa tuyết";
        case 95: return "Dông";
        case 96: case 99: return "Dông, mưa đá";
        default: return "Có mây";
    }
}

const char* WeatherService::AqiLevel(int aqi) {
    if (aqi < 0) return "";
    if (aqi <= 50) return "Tốt";
    if (aqi <= 100) return "Trung bình";
    if (aqi <= 150) return "Kém";
    if (aqi <= 200) return "Xấu";
    if (aqi <= 300) return "Rất xấu";
    return "Nguy hại";
}

uint32_t WeatherService::AqiColor(int aqi) {
    if (aqi < 0) return 0xAAAAAA;
    if (aqi <= 50) return 0x22C55E;
    if (aqi <= 100) return 0xEAB308;
    if (aqi <= 150) return 0xF97316;
    if (aqi <= 200) return 0xEF4444;
    if (aqi <= 300) return 0xA855F7;
    return 0x9F1239;
}

std::string WeatherService::HttpGet(const std::string& url) {
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(WEATHER_HTTP_TIMEOUT_MS);
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "HTTP open failed: %s", url.c_str());
        return "";
    }
    int status = http->GetStatusCode();
    std::string body;
    if (status == 200) {
        body = http->ReadAll();
    } else {
        ESP_LOGW(TAG, "HTTP %d: %s", status, url.c_str());
    }
    http->Close();
    return body;
}

WeatherService::Location WeatherService::LocationFromIP() {
    Location loc;
    std::string body = HttpGet(IP_LOCATION_API_ENDPOINT);
    if (body.empty()) return loc;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return loc;
    cJSON* success = cJSON_GetObjectItem(root, "success");
    cJSON* lat = cJSON_GetObjectItem(root, "latitude");
    cJSON* lon = cJSON_GetObjectItem(root, "longitude");
    cJSON* city = cJSON_GetObjectItem(root, "city");
    cJSON* region = cJSON_GetObjectItem(root, "region");
    if (cJSON_IsTrue(success) && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        loc.lat = (float)lat->valuedouble;
        loc.lon = (float)lon->valuedouble;
        if (cJSON_IsString(city) && city->valuestring[0]) {
            loc.name = city->valuestring;
        } else if (cJSON_IsString(region)) {
            loc.name = region->valuestring;
        }
        loc.valid = true;
        ESP_LOGI(TAG, "IP location: %s (%.4f, %.4f)", loc.name.c_str(), loc.lat, loc.lon);
    }
    cJSON_Delete(root);
    return loc;
}

WeatherService::Location WeatherService::LocationFromName(const std::string& name) {
    Location loc;
    std::string url = std::string(OPEN_METEO_GEOCODING_ENDPOINT) + "?name=" + UrlEncode(name) +
                      "&count=1&language=vi&format=json";
    std::string body = HttpGet(url);
    if (body.empty()) return loc;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return loc;
    cJSON* results = cJSON_GetObjectItem(root, "results");
    cJSON* first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
    if (first) {
        cJSON* lat = cJSON_GetObjectItem(first, "latitude");
        cJSON* lon = cJSON_GetObjectItem(first, "longitude");
        cJSON* nm = cJSON_GetObjectItem(first, "name");
        if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            loc.lat = (float)lat->valuedouble;
            loc.lon = (float)lon->valuedouble;
            loc.name = cJSON_IsString(nm) ? nm->valuestring : name;
            loc.valid = true;
            ESP_LOGI(TAG, "Geocoded '%s' -> %s (%.4f, %.4f)", name.c_str(), loc.name.c_str(), loc.lat, loc.lon);
        }
    }
    cJSON_Delete(root);
    return loc;
}

static double NumAt(cJSON* arr, int i) {
    cJSON* v = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, i) : nullptr;
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

bool WeatherService::FetchForecast(const Location& loc) {
    char url[512];
    snprintf(url, sizeof(url),
             "%s?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,"
             "surface_pressure,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&timezone=auto&forecast_days=5&wind_speed_unit=ms",
             OPEN_METEO_FORECAST_ENDPOINT, loc.lat, loc.lon);
    std::string body = HttpGet(url);
    if (body.empty()) return false;

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Forecast JSON parse failed");
        return false;
    }
    bool ok = false;
    cJSON* cur = cJSON_GetObjectItem(root, "current");
    cJSON* daily = cJSON_GetObjectItem(root, "daily");
    if (cJSON_IsObject(cur)) {
        cJSON* t = cJSON_GetObjectItem(cur, "temperature_2m");
        cJSON* h = cJSON_GetObjectItem(cur, "relative_humidity_2m");
        cJSON* fl = cJSON_GetObjectItem(cur, "apparent_temperature");
        cJSON* day = cJSON_GetObjectItem(cur, "is_day");
        cJSON* code = cJSON_GetObjectItem(cur, "weather_code");
        cJSON* p = cJSON_GetObjectItem(cur, "surface_pressure");
        cJSON* w = cJSON_GetObjectItem(cur, "wind_speed_10m");
        if (cJSON_IsNumber(t) && cJSON_IsNumber(code)) {
            bool is_day = !cJSON_IsNumber(day) || day->valueint == 1;
            weather_info_.temp = (float)t->valuedouble;
            weather_info_.humidity = cJSON_IsNumber(h) ? h->valueint : 0;
            weather_info_.feels_like = cJSON_IsNumber(fl) ? (float)fl->valuedouble : weather_info_.temp;
            weather_info_.pressure = cJSON_IsNumber(p) ? (int)lround(p->valuedouble) : 0;
            weather_info_.wind_speed = cJSON_IsNumber(w) ? (float)w->valuedouble : 0;
            weather_info_.icon_code = WmoToIconCode(code->valueint, is_day);
            weather_info_.description = WmoToVietnamese(code->valueint);
            weather_info_.city = loc.name;
            weather_info_.latitude = loc.lat;
            weather_info_.longitude = loc.lon;
            ok = true;
        }
    }
    if (ok && cJSON_IsObject(daily)) {
        cJSON* times = cJSON_GetObjectItem(daily, "time");
        cJSON* codes = cJSON_GetObjectItem(daily, "weather_code");
        cJSON* tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON* tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON* rain = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
        weather_info_.forecast.clear();
        for (int i = 0; i < n; i++) {
            double hi = NumAt(tmax, i), lo = NumAt(tmin, i), c = NumAt(codes, i), r = NumAt(rain, i);
            if (i == 0) {
                if (!std::isnan(hi)) weather_info_.temp_max = (float)hi;
                if (!std::isnan(lo)) weather_info_.temp_min = (float)lo;
                continue;  // today is shown in the main block
            }
            cJSON* tday = cJSON_GetArrayItem(times, i);
            ForecastItem item;
            item.day_name = cJSON_IsString(tday) ? DayNameFromDate(tday->valuestring) : "";
            item.icon_code = WmoToIconCode(std::isnan(c) ? 3 : (int)c, true);
            item.temp = std::isnan(hi) ? 0 : (float)hi;
            item.temp_min = std::isnan(lo) ? 0 : (float)lo;
            item.rain_prob = std::isnan(r) ? 0 : (int)r;
            weather_info_.forecast.push_back(item);
        }
    }
    cJSON_Delete(root);
    return ok;
}

void WeatherService::FetchAirQuality(const Location& loc) {
    char url[256];
    snprintf(url, sizeof(url), "%s?latitude=%.4f&longitude=%.4f&current=us_aqi,pm2_5&timezone=auto",
             OPEN_METEO_AIR_ENDPOINT, loc.lat, loc.lon);
    std::string body = HttpGet(url);
    if (body.empty()) return;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return;
    cJSON* cur = cJSON_GetObjectItem(root, "current");
    cJSON* aqi = cJSON_IsObject(cur) ? cJSON_GetObjectItem(cur, "us_aqi") : nullptr;
    if (cJSON_IsNumber(aqi)) {
        weather_info_.aqi = (int)lround(aqi->valuedouble);
        ESP_LOGI(TAG, "AQI: %d (%s)", weather_info_.aqi, AqiLevel(weather_info_.aqi));
    }
    cJSON_Delete(root);
}

bool WeatherService::FetchWeatherData() {
#if !CONFIG_WEATHER_IDLE_DISPLAY_ENABLE
    ESP_LOGW(TAG, "Weather feature is disabled");
    return false;
#endif
    int wait_retries = 0;
    while (!WifiStation::GetInstance().IsConnected() && wait_retries < 20) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_retries++;
    }
    if (!WifiStation::GetInstance().IsConnected()) {
        ESP_LOGE(TAG, "WiFi not connected, skipping weather update.");
        return false;
    }

    // Resolve location once (configured city name, else IP), then reuse it
    if (!cached_location_.valid) {
        Settings weather_settings("wifi", false);
        std::string city = city_.empty() ? weather_settings.GetString("weather_city", "") : city_;
        if (!city.empty() && city != "auto") {
            cached_location_ = LocationFromName(city);
        }
        for (int i = 0; i < 2 && !cached_location_.valid; i++) {
            cached_location_ = LocationFromIP();
            if (!cached_location_.valid) vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!cached_location_.valid) {
            ESP_LOGW(TAG, "Location unknown, fallback to %s", CITY_LOCATION_DEFAULT);
            cached_location_.name = "Hà Nội";
            cached_location_.lat = CITY_LAT_DEFAULT;
            cached_location_.lon = CITY_LON_DEFAULT;
            cached_location_.valid = true;
        }
    }

    if (!FetchForecast(cached_location_)) {
        return false;
    }
    FetchAirQuality(cached_location_);
    weather_info_.valid = true;
    last_update_time_ = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Weather updated: %s %.1fC %s", weather_info_.city.c_str(), weather_info_.temp,
             weather_info_.description.c_str());
    return true;
}
