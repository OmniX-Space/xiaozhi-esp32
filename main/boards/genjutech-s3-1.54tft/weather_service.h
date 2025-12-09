/**
 * @file weather_service.h
 * @brief Weather API service for fetching weather data
 * @version 1.0
 * @date 2025-01-10
 */
#pragma once

#include <string>
#include <functional>
#include "idle_screen.h"

class WeatherService {
public:
    WeatherService();
    ~WeatherService();
    
    // Initialize weather service with city code (optional, will auto-detect if empty)
    void Initialize(const std::string& city_code = "");  // Empty: auto-detect
    
    // Auto-detect city code by IP address
    bool AutoDetectCityCode();
    
    // Fetch weather data (async)
    void FetchWeather();
    
    // Set callback for when weather data is updated
    void SetWeatherCallback(std::function<void(const WeatherData&)> callback);
    
    // Get last fetched weather data
    const WeatherData& GetLastWeatherData() const { return last_weather_data_; }
    
    // Get current city code
    const std::string& GetCityCode() const { return city_code_; }
    
private:
    void ParseWeatherData(const std::string& response);
    std::string ExtractJsonValue(const std::string& json, const std::string& key);
    
private:
    std::string city_code_;
    WeatherData last_weather_data_;
    std::function<void(const WeatherData&)> weather_callback_;
    bool auto_detect_enabled_;
};

