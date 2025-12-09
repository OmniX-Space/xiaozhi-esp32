/**
 * @file idle_screen.h
 * @brief Weather Clock Idle Screen for GenJuTech S3 1.54TFT
 * @brief Inspired by Arduino MiniTV weather clock project
 * @version 2.0
 * @date 2025-01-10
 */
#pragma once

#include "config.h"
#if IDLE_SCREEN_HOOK

#include <lvgl.h>
#include <string>
#include <vector>

// Theme color structure (copied from lcd_display.h to avoid circular dependency)
struct ThemeColors {
    lv_color_t background;
    lv_color_t text;
    lv_color_t chat_background;
    lv_color_t user_bubble;
    lv_color_t assistant_bubble;
    lv_color_t system_bubble;
    lv_color_t system_text;
    lv_color_t border;
    lv_color_t low_battery;
};

// Weather data structure
struct WeatherData {
    std::string city_name;           // 城市名称
    std::string temperature;         // 当前温度
    std::string humidity;            // 湿度
    std::string weather_desc;        // 天气描述
    std::string wind_direction;      // 风向
    std::string wind_speed;          // 风速
    int aqi;                         // 空气质量指数
    std::string aqi_desc;            // 空气质量描述
    std::string temp_low;            // 最低温度
    std::string temp_high;           // 最高温度
    uint32_t last_update_time;       // 最后更新时间
};

class IdleScreen
{
public:
    IdleScreen();
    ~IdleScreen();

public:
    void ui_init(ThemeColors *p_current_theme);
    void ui_destroy();
    void ui_showScreen(bool showIt);
    void ui_update();  // Called every second to update time
    void ui_updateTheme(ThemeColors *p_current_theme);
    
    // Weather related methods
    void ui_updateWeather(const WeatherData& weather);
    void ui_setScrollText(const std::vector<std::string>& texts);
    
    // Alarm display methods
    void ui_showAlarmInfo(const char* alarm_message);
    void ui_hideAlarmInfo();
    
public:
    bool ui_shown;  // UI shown or not
    
private:
    void createTopSection();      // 顶部：滚动信息 + 城市
    void createMiddleSection();   // 中间：时间显示区域
    void createBottomSection();   // 底部：温湿度 + 日期
    void updateScrollText();      // 更新滚动文字
    void updateAQIColor(int aqi); // 更新空气质量颜色
    
private:
    // Main containers
    lv_obj_t* ui_screen;
    lv_obj_t* ui_main_container;
    
    // Top section - Weather scroll and city
    lv_obj_t* ui_scroll_container;
    lv_obj_t* ui_scroll_label;
    lv_obj_t* ui_city_label;
    
    // Middle section - Time display
    lv_obj_t* ui_time_container;
    lv_obj_t* ui_time_hour_min;    // 时:分 (大字体)
    lv_obj_t* ui_time_second;      // 秒 (小字体)
    lv_obj_t* ui_xiaozhi_icon;     // 旋转小智图标
    
    // Bottom upper section - AQI, Temperature, Humidity
    lv_obj_t* ui_info_container;
    lv_obj_t* ui_aqi_container;
    lv_obj_t* ui_aqi_label;
    lv_obj_t* ui_temp_label;
    lv_obj_t* ui_temp_icon_label;
    lv_obj_t* ui_humid_label;
    lv_obj_t* ui_humid_icon_label;
    
    // Bottom lower section - Week and Date
    lv_obj_t* ui_date_container;
    lv_obj_t* ui_week_label;
    lv_obj_t* ui_date_label;
    
    // Alarm info (overlays on screen when alarm triggers)
    lv_obj_t* ui_alarm_info_label;
    
    // Scroll text management
    std::vector<std::string> scroll_texts;
    int current_scroll_index;
    uint32_t last_scroll_time;
    
    // Theme
    ThemeColors* p_theme;
};

#endif // IDLE_SCREEN_HOOK
