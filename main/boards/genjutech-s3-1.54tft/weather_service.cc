/**
 * @file weather_service.cc
 * @brief Weather API service implementation
 * @version 1.0
 */

#include "weather_service.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <time.h>

static const char *TAG = "WeatherService";


WeatherService::WeatherService() {
    city_code_ = "101010100";  // Default: Beijing
    auto_detect_enabled_ = false;
}

WeatherService::~WeatherService() {
}

void WeatherService::Initialize(const std::string& city_code) {
    if (city_code.empty()) {
        // Auto-detect city code by IP
        ESP_LOGI(TAG, "Auto-detecting city code by IP address...");
        if (AutoDetectCityCode()) {
            ESP_LOGI(TAG, "Auto-detected city code: %s", city_code_.c_str());
            auto_detect_enabled_ = true;
        } else {
            ESP_LOGW(TAG, "Failed to auto-detect city code, using default: Beijing (101010100)");
            city_code_ = "101010100";
            auto_detect_enabled_ = false;
        }
    } else {
        city_code_ = city_code;
        auto_detect_enabled_ = false;
        ESP_LOGI(TAG, "Weather service initialized with city code: %s", city_code_.c_str());
    }
}

bool WeatherService::AutoDetectCityCode() {
    // Use weather.com.cn IP geolocation API
    time_t now;
    time(&now);
    char url[256];
    snprintf(url, sizeof(url), "http://wgeo.weather.com.cn/ip/?_=%ld", (long)now);
    
    ESP_LOGI(TAG, "Fetching city code from: %s", url);
    
    // Allocate response buffer
    char *buffer = (char*)malloc(2048);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return false;
    }
    memset(buffer, 0, 2048);
    
    // Configure HTTP client with larger buffer
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 15000;
    config.buffer_size = 2048;       // Important: increase buffer size
    config.buffer_size_tx = 1024;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 3;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(buffer);
        return false;
    }
    
    // Set headers
    esp_http_client_set_header(client, "User-Agent", 
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    esp_http_client_set_header(client, "Referer", "http://www.weather.com.cn/");
    esp_http_client_set_header(client, "Accept", "*/*");
    
    bool success = false;
    
    // Open connection and read
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status = %d, Content-Length = %d", status_code, content_length);
        
        if (status_code == 200 || status_code == 302) {
            // Read response data
            int total_read = 0;
            int read_len;
            
            while (total_read < 2047) {
                read_len = esp_http_client_read(client, buffer + total_read, 2047 - total_read);
                if (read_len <= 0) {
                    break;
                }
                total_read += read_len;
            }
            
            buffer[total_read] = '\0';
            
            ESP_LOGI(TAG, "Read %d bytes from API", total_read);
            
            if (total_read > 0) {
                // Print first 200 chars for debugging
                char preview[201];
                int preview_len = (total_read > 200) ? 200 : total_read;
                memcpy(preview, buffer, preview_len);
                preview[preview_len] = '\0';
                ESP_LOGI(TAG, "Response preview: %s", preview);
                
                std::string response(buffer);
                
                // Try multiple parsing patterns
                size_t id_pos = response.find("id=\"");
                if (id_pos != std::string::npos) {
                    // Pattern: id="101010100" (JavaScript variable with double quotes)
                    size_t start = id_pos + 4;  // Skip id="
                    size_t end = response.find("\"", start);
                    if (end != std::string::npos && end > start) {
                        city_code_ = response.substr(start, end - start);
                        ESP_LOGI(TAG, "✅ Detected city code: %s", city_code_.c_str());
                        success = true;
                    }
                } else if ((id_pos = response.find("id='")) != std::string::npos) {
                    // Pattern: id='101010100' (JavaScript variable with single quotes)
                    size_t start = id_pos + 4;
                    size_t end = response.find("'", start);
                    if (end != std::string::npos && end > start) {
                        city_code_ = response.substr(start, end - start);
                        ESP_LOGI(TAG, "✅ Detected city code: %s", city_code_.c_str());
                        success = true;
                    }
                } else if ((id_pos = response.find("id\":\"")) != std::string::npos) {
                    // Pattern: "id":"101010100" (JSON format)
                    size_t start = id_pos + 5;
                    size_t end = response.find("\"", start);
                    if (end != std::string::npos && end > start) {
                        city_code_ = response.substr(start, end - start);
                        ESP_LOGI(TAG, "✅ Detected city code: %s", city_code_.c_str());
                        success = true;
                    }
                } else {
                    ESP_LOGW(TAG, "❌ City code pattern not found in response");
                }
            } else {
                ESP_LOGW(TAG, "❌ No data read from API");
            }
        } else {
            ESP_LOGW(TAG, "❌ HTTP status: %d", status_code);
        }
        
        esp_http_client_close(client);
    } else {
        ESP_LOGE(TAG, "❌ Failed to connect: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(buffer);
    
    return success;
}

void WeatherService::SetWeatherCallback(std::function<void(const WeatherData&)> callback) {
    weather_callback_ = callback;
}

void WeatherService::FetchWeather() {
    ESP_LOGI(TAG, "Fetching weather data for city: %s", city_code_.c_str());
    
    // Construct URL
    time_t now;
    time(&now);
    char url[256];
    snprintf(url, sizeof(url), "http://d1.weather.com.cn/weather_index/%s.html?_=%ld", 
             city_code_.c_str(), (long)now);
    
    // Configure HTTP client (no event handler, read directly)
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 10000;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set User-Agent header
    esp_http_client_set_header(client, "User-Agent", 
        "Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38");
    esp_http_client_set_header(client, "Referer", "http://www.weather.com.cn/");
    
    // Open connection
    esp_err_t err = esp_http_client_open(client, 0);
    
    if (err == ESP_OK) {
        // Read response
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP GET Status = %d, Content-Length = %d", status_code, content_length);
        
        if (status_code == 200) {
            // Allocate buffer (use 8KB if content_length unknown or too large)
            int buffer_size = (content_length > 0 && content_length < 8192) ? content_length + 1 : 8192;
            char *buffer = (char*)malloc(buffer_size);
            
            if (buffer) {
                int total_read = 0;
                int read_len;
                
                // Read all data
                while ((read_len = esp_http_client_read(client, buffer + total_read, buffer_size - total_read - 1)) > 0) {
                    total_read += read_len;
                    if (total_read >= buffer_size - 1) {
                        break;
                    }
                }
                
                buffer[total_read] = '\0';
                
                ESP_LOGI(TAG, "Read %d bytes of weather data", total_read);
                
                std::string response(buffer);
                ParseWeatherData(response);
                
                if (weather_callback_) {
                    weather_callback_(last_weather_data_);
                }
                
                free(buffer);
            } else {
                ESP_LOGE(TAG, "Failed to allocate buffer for response");
            }
        } else {
            ESP_LOGW(TAG, "HTTP request returned status code: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }
    
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

std::string WeatherService::ExtractJsonValue(const std::string& json, const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\":");
    if (key_pos == std::string::npos) {
        return "";
    }
    
    size_t value_start = json.find("\"", key_pos + key.length() + 3);
    if (value_start == std::string::npos) {
        return "";
    }
    value_start++;
    
    size_t value_end = json.find("\"", value_start);
    if (value_end == std::string::npos) {
        return "";
    }
    
    return json.substr(value_start, value_end - value_start);
}

void WeatherService::ParseWeatherData(const std::string& response) {
    ESP_LOGI(TAG, "Parsing weather data...");
    
    try {
        // Extract dataSK JSON section
        size_t sk_start = response.find("dataSK =");
        size_t sk_end = response.find(";var dataZS");
        
        if (sk_start != std::string::npos && sk_end != std::string::npos) {
            std::string dataSK = response.substr(sk_start + 8, sk_end - sk_start - 8);
            
            // Parse JSON using cJSON
            cJSON *root = cJSON_Parse(dataSK.c_str());
            if (root) {
                cJSON *city = cJSON_GetObjectItem(root, "cityname");
                cJSON *temp = cJSON_GetObjectItem(root, "temp");
                cJSON *humidity = cJSON_GetObjectItem(root, "SD");
                cJSON *weather = cJSON_GetObjectItem(root, "weather");
                cJSON *wind_dir = cJSON_GetObjectItem(root, "WD");
                cJSON *wind_speed = cJSON_GetObjectItem(root, "WS");
                cJSON *aqi = cJSON_GetObjectItem(root, "aqi");
                
                if (city && cJSON_IsString(city)) {
                    last_weather_data_.city_name = city->valuestring;
                }
                if (temp && cJSON_IsString(temp)) {
                    last_weather_data_.temperature = temp->valuestring;
                }
                if (humidity && cJSON_IsString(humidity)) {
                    last_weather_data_.humidity = humidity->valuestring;
                }
                if (weather && cJSON_IsString(weather)) {
                    last_weather_data_.weather_desc = weather->valuestring;
                }
                if (wind_dir && cJSON_IsString(wind_dir)) {
                    last_weather_data_.wind_direction = wind_dir->valuestring;
                }
                if (wind_speed && cJSON_IsString(wind_speed)) {
                    last_weather_data_.wind_speed = wind_speed->valuestring;
                }
                if (aqi && cJSON_IsNumber(aqi)) {
                    last_weather_data_.aqi = aqi->valueint;
                    
                    // Determine AQI description
                    if (last_weather_data_.aqi > 200) {
                        last_weather_data_.aqi_desc = "重度";
                    } else if (last_weather_data_.aqi > 150) {
                        last_weather_data_.aqi_desc = "中度";
                    } else if (last_weather_data_.aqi > 100) {
                        last_weather_data_.aqi_desc = "轻度";
                    } else if (last_weather_data_.aqi > 50) {
                        last_weather_data_.aqi_desc = "良";
                    } else {
                        last_weather_data_.aqi_desc = "优";
                    }
                }
                
                cJSON_Delete(root);
            }
        }
        
        // Extract forecast data (f section)
        size_t fc_start = response.find("\"f\":[");
        size_t fc_end = response.find(",{\"fa", fc_start);
        
        if (fc_start != std::string::npos && fc_end != std::string::npos) {
            std::string dataFC = response.substr(fc_start + 5, fc_end - fc_start - 5);
            
            cJSON *root = cJSON_Parse(dataFC.c_str());
            if (root) {
                cJSON *temp_low = cJSON_GetObjectItem(root, "fd");
                cJSON *temp_high = cJSON_GetObjectItem(root, "fc");
                
                if (temp_low && cJSON_IsString(temp_low)) {
                    last_weather_data_.temp_low = temp_low->valuestring;
                }
                if (temp_high && cJSON_IsString(temp_high)) {
                    last_weather_data_.temp_high = temp_high->valuestring;
                }
                
                cJSON_Delete(root);
            }
        }
        
        last_weather_data_.last_update_time = esp_timer_get_time() / 1000000;
        
        ESP_LOGI(TAG, "Weather parsed: City=%s, Temp=%s℃, Humidity=%s, AQI=%d(%s)", 
                 last_weather_data_.city_name.c_str(),
                 last_weather_data_.temperature.c_str(),
                 last_weather_data_.humidity.c_str(),
                 last_weather_data_.aqi,
                 last_weather_data_.aqi_desc.c_str());
                 
    } catch (...) {
        ESP_LOGE(TAG, "Failed to parse weather data");
    }
}

