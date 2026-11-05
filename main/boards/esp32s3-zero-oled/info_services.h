#ifndef ZERO_INFO_SERVICES_H
#define ZERO_INFO_SERVICES_H

// Weather (Open-Meteo, no API key) and news headlines (VnExpress RSS) for the Zero board.
// Registers MCP tools: self.weather.get, self.news.get.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct WeatherDay {
    std::string day;    // "T6"
    std::string desc;   // "Mưa rào"
    int tmin = 0;
    int tmax = 0;
    int rain_prob = 0;
};

struct WeatherNow {
    bool valid = false;
    std::string city;
    int temp = 0;
    int feels_like = 0;
    int humidity = 0;
    float wind = 0;
    std::string desc;
    int code = -1;      // WMO weather code (for the idle screen icon)
    bool is_day = true;
    int aqi = -1;
    std::vector<WeatherDay> days;  // today + next days
};

class InfoServices {
public:
    static InfoServices& GetInstance() {
        static InfoServices instance;
        return instance;
    }

    // Register MCP tools. Safe to call from a board constructor.
    void RegisterTools();

    // Fetch weather if older than max_age_s (blocking, call from a background task)
    bool RefreshWeather(int max_age_s = 1800);
    WeatherNow GetWeather();

private:
    InfoServices() = default;
    bool ResolveLocation();

    std::mutex mutex_;
    WeatherNow weather_;
    int64_t weather_time_us_ = 0;
    float lat_ = 0, lon_ = 0;
    std::string city_;
    bool location_ok_ = false;
};

#endif  // ZERO_INFO_SERVICES_H
