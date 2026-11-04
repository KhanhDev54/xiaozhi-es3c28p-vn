#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include "weather_model.h"
#include "weather_config.h"
#include <string>
#include <memory>
#include <ctime>

// Weather data from Open-Meteo (https://open-meteo.com) — free, no API key.
// Location: NVS "wifi/weather_city" if set (geocoded), otherwise detected by IP.
class WeatherService {
public:
    static WeatherService& GetInstance() {
        static WeatherService instance;
        return instance;
    }

    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    // Fetch current weather, 5-day forecast and air quality
    bool FetchWeatherData();

    // Get current weather info
    const WeatherInfo& GetWeatherInfo() const { return weather_info_; }

    // Check if weather data needs update
    bool NeedsUpdate() const;

    // Set city manually (name, geocoded via Open-Meteo)
    void SetCity(const std::string& city) { city_ = city; }

    // Helpers shared with UI
    static const char* WmoToIconCode(int wmo, bool is_day);
    static const char* WmoToVietnamese(int wmo);
    static const char* AqiLevel(int aqi);
    static uint32_t AqiColor(int aqi);

private:
    WeatherService();
    ~WeatherService() = default;

    struct Location {
        std::string name;
        float lat = 0;
        float lon = 0;
        bool valid = false;
    };

    Location LocationFromIP();
    Location LocationFromName(const std::string& name);
    bool FetchForecast(const Location& loc);
    void FetchAirQuality(const Location& loc);
    std::string HttpGet(const std::string& url);

    static std::string UrlEncode(const std::string& value);
    static std::string DayNameFromDate(const std::string& yyyy_mm_dd);

    WeatherInfo weather_info_;
    std::string city_;
    Location cached_location_;
    uint32_t last_update_time_;
};

#endif // WEATHER_SERVICE_H
