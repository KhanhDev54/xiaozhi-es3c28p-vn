// --- [DIENBIEN MOD] ---
#ifndef WEATHER_MODEL_H
#define WEATHER_MODEL_H

#include <string>
#include <vector>

// Cấu trúc lưu trữ 1 mốc dự báo
struct ForecastItem {
    std::string day_name; // Ví dụ: "T2", "T3"
    std::string icon_code; // Mã icon
    float temp;           // Nhiệt độ dự báo (cao nhất trong ngày)
    float temp_min = 0.0f; // Nhiệt độ thấp nhất trong ngày
    int rain_prob = 0;    // Khả năng mưa (%)
};

// Cấu trúc thông tin thời tiết (Lấy từ API về)
struct WeatherInfo {
    std::string city;
    std::string description;
    std::string icon_code;
    float temp = 0.0f;
    int humidity = 0;
    float feels_like = 0.0f;
    int pressure = 0;
    float wind_speed = 0.0f;
    float temp_max = 0.0f;  // cao nhất hôm nay
    float temp_min = 0.0f;  // thấp nhất hôm nay
    int aqi = -1;           // US AQI (Open-Meteo), -1 = chưa có
    float latitude = 0.0f;
    float longitude = 0.0f;
    bool valid = false;

    std::vector<ForecastItem> forecast; 
};

// Cấu trúc hiển thị ra màn hình
struct IdleCardInfo {
    std::string city;
    std::string time_text;
    std::string date_text;
    std::string day_text;
    std::string temperature_text;
    std::string humidity_text;
    std::string description_text;
    std::string feels_like_text;
    std::string wind_text;
    std::string pressure_text;
    std::string lunar_text;     // "26/9 âm lịch"
    std::string aqi_text;       // "AQI 96 · Trung bình"
    uint32_t aqi_color = 0;     // màu theo mức AQI
    std::string range_text;     // "24°/31°"
    std::string battery_icon;
    std::string network_icon;
    const char* icon = nullptr;
    
    // System Info
    int battery_level = 100;
    bool is_charging = false;
    int8_t rssi = 0;
    
    std::vector<ForecastItem> forecast;
};

#endif // WEATHER_MODEL_H