/**
 * @file idle_screen.cc
 * @brief Weather Clock Idle Screen Implementation
 * @version 2.0
 */

#include "config.h"
#include "application.h"
#include "idle_screen.h"
#include "ui_helpers.h"
#include "display/lcd_display.h"
#include <esp_log.h>
#include <time.h>
#include <sys/time.h>

#if IDLE_SCREEN_HOOK

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(ui_font_font48Seg);
LV_IMG_DECLARE(ui_img_xiaozhi_48_png);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"

static const char *TAG = "WeatherClock";

// Helper function to format time
static void get_time_string(char* hour_min_buf, char* second_buf, char* week_buf, char* date_buf) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Format hour:minute (HH:MM)
    snprintf(hour_min_buf, 16, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    // Format second (SS)
    snprintf(second_buf, 8, "%02d", timeinfo.tm_sec);
    
    // Format week (周X)
    const char* week_names[] = {"日", "一", "二", "三", "四", "五", "六"};
    snprintf(week_buf, 16, "周%s", week_names[timeinfo.tm_wday]);
    
    // Format date (MM月DD日)
    snprintf(date_buf, 32, "%02d月%02d日", timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

IdleScreen::IdleScreen() {
    ui_screen = NULL;
    ui_main_container = NULL;
    ui_scroll_container = NULL;
    ui_scroll_label = NULL;
    ui_city_label = NULL;
    ui_time_container = NULL;
    ui_time_hour_min = NULL;
    ui_time_second = NULL;
    ui_xiaozhi_icon = NULL;
    ui_info_container = NULL;
    ui_aqi_container = NULL;
    ui_aqi_label = NULL;
    ui_temp_label = NULL;
    ui_temp_icon_label = NULL;
    ui_humid_label = NULL;
    ui_humid_icon_label = NULL;
    ui_date_container = NULL;
    ui_week_label = NULL;
    ui_date_label = NULL;
    ui_alarm_info_label = NULL;
    
    ui_shown = false;
    current_scroll_index = 0;
    last_scroll_time = 0;
    p_theme = NULL;
}

IdleScreen::~IdleScreen() {
    ui_destroy();
}

void IdleScreen::ui_init(ThemeColors *p_current_theme) {
    auto screen = lv_screen_active();
    p_theme = p_current_theme;
    
    ESP_LOGI(TAG, "Initializing weather clock UI");
    
    // Main screen container
    ui_screen = lv_obj_create(screen);
    lv_obj_remove_flag(ui_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui_screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(ui_screen, 0, 0);
    lv_obj_set_style_pad_all(ui_screen, 0, 0);
    
    // Main container
    ui_main_container = lv_obj_create(ui_screen);
    lv_obj_remove_style_all(ui_main_container);
    lv_obj_set_size(ui_main_container, 240, 240);
    lv_obj_set_align(ui_main_container, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_main_container, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_color(ui_main_container, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_main_container, LV_OPA_COVER, 0);
    
    createTopSection();
    createMiddleSection();
    createBottomSection();
    
    // Alarm info label (initially hidden)
    ui_alarm_info_label = lv_label_create(ui_main_container);
    lv_obj_set_width(ui_alarm_info_label, 220);
    lv_obj_set_height(ui_alarm_info_label, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_alarm_info_label, 10, 80);
    lv_label_set_long_mode(ui_alarm_info_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui_alarm_info_label, "");
    lv_obj_set_style_text_align(ui_alarm_info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ui_alarm_info_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_alarm_info_label, lv_color_hex(0xFF0000), 0);
    lv_obj_add_flag(ui_alarm_info_label, LV_OBJ_FLAG_HIDDEN);
    
    // Draw divider lines
    static lv_point_precise_t line_points1[] = {{0, 34}, {240, 34}};
    static lv_point_precise_t line_points2[] = {{150, 0}, {150, 34}};
    static lv_point_precise_t line_points3[] = {{0, 166}, {240, 166}};
    static lv_point_precise_t line_points4[] = {{60, 166}, {60, 200}};
    static lv_point_precise_t line_points5[] = {{160, 166}, {160, 200}};
    
    lv_obj_t* line1 = lv_line_create(ui_main_container);
    lv_line_set_points(line1, line_points1, 2);
    lv_obj_set_style_line_color(line1, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_width(line1, 1, 0);
    
    lv_obj_t* line2 = lv_line_create(ui_main_container);
    lv_line_set_points(line2, line_points2, 2);
    lv_obj_set_style_line_color(line2, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_width(line2, 1, 0);
    
    lv_obj_t* line3 = lv_line_create(ui_main_container);
    lv_line_set_points(line3, line_points3, 2);
    lv_obj_set_style_line_color(line3, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_width(line3, 1, 0);
    
    lv_obj_t* line4 = lv_line_create(ui_main_container);
    lv_line_set_points(line4, line_points4, 2);
    lv_obj_set_style_line_color(line4, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_width(line4, 1, 0);
    
    lv_obj_t* line5 = lv_line_create(ui_main_container);
    lv_line_set_points(line5, line_points5, 2);
    lv_obj_set_style_line_color(line5, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_width(line5, 1, 0);
    
    // Hide by default
    lv_obj_add_flag(ui_screen, LV_OBJ_FLAG_HIDDEN);
    
    ESP_LOGI(TAG, "Weather clock UI initialized");
}

void IdleScreen::createTopSection() {
    // Top section container (0-34px height)
    ui_scroll_container = lv_obj_create(ui_main_container);
    lv_obj_remove_style_all(ui_scroll_container);
    lv_obj_set_size(ui_scroll_container, 148, 32);
    lv_obj_set_pos(ui_scroll_container, 2, 2);
    lv_obj_remove_flag(ui_scroll_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Scrolling weather info label
    ui_scroll_label = lv_label_create(ui_scroll_container);
    lv_obj_set_width(ui_scroll_label, 144);
    lv_label_set_long_mode(ui_scroll_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui_scroll_label, "正在获取天气信息...");
    lv_obj_set_style_text_font(ui_scroll_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_scroll_label, lv_color_hex(0x000000), 0);
    lv_obj_align(ui_scroll_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // City name label
    ui_city_label = lv_label_create(ui_main_container);
    lv_obj_set_width(ui_city_label, 88);
    lv_label_set_text(ui_city_label, "北京");
    lv_obj_set_style_text_font(ui_city_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_city_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_align(ui_city_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui_city_label, 152, 8);
}

void IdleScreen::createMiddleSection() {
    // Middle section container (35-165px, height 130px)
    ui_time_container = lv_obj_create(ui_main_container);
    lv_obj_remove_style_all(ui_time_container);
    lv_obj_set_size(ui_time_container, 240, 130);
    lv_obj_set_pos(ui_time_container, 0, 35);
    lv_obj_remove_flag(ui_time_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Large time display (HH:MM) - using ui_font_font48Seg (already in project)
    ui_time_hour_min = lv_label_create(ui_time_container);
    lv_label_set_text(ui_time_hour_min, "12:34");
    lv_obj_set_style_text_font(ui_time_hour_min, &ui_font_font48Seg, 0);
    lv_obj_set_style_text_color(ui_time_hour_min, lv_color_hex(0x000000), 0);
    lv_obj_align(ui_time_hour_min, LV_ALIGN_CENTER, -20, -25);
    
    // Small second display (SS)
    ui_time_second = lv_label_create(ui_time_container);
    lv_label_set_text(ui_time_second, "56");
    lv_obj_set_style_text_font(ui_time_second, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_time_second, lv_color_hex(0x000000), 0);
    lv_obj_align(ui_time_second, LV_ALIGN_CENTER, 65, -25);
    
    // Rotating Xiaozhi icon (位置在时间下方)
    ui_xiaozhi_icon = lv_img_create(ui_time_container);
    lv_img_set_src(ui_xiaozhi_icon, &ui_img_xiaozhi_48_png);
    lv_obj_align(ui_xiaozhi_icon, LV_ALIGN_CENTER, 0, 35);
    lv_img_set_pivot(ui_xiaozhi_icon, 24, 24);  // 设置旋转中心点 (48/2 = 24)
    
    // Add rotation animation (使用局部变量，LVGL会自动管理)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ui_xiaozhi_icon);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_img_set_angle);
    lv_anim_set_values(&anim, 0, 3600);  // 0-360度 (LVGL使用0.1度单位)
    lv_anim_set_time(&anim, 3000);       // 3秒转一圈
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
    
    ESP_LOGI(TAG, "Xiaozhi rotation animation started");
}

void IdleScreen::createBottomSection() {
    // Info container (166-200px, height 34px)
    ui_info_container = lv_obj_create(ui_main_container);
    lv_obj_remove_style_all(ui_info_container);
    lv_obj_set_size(ui_info_container, 240, 34);
    lv_obj_set_pos(ui_info_container, 0, 166);
    lv_obj_remove_flag(ui_info_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // AQI container (0-60px)
    ui_aqi_container = lv_obj_create(ui_info_container);
    lv_obj_set_size(ui_aqi_container, 50, 24);
    lv_obj_set_pos(ui_aqi_container, 5, 5);
    lv_obj_set_style_radius(ui_aqi_container, 4, 0);
    lv_obj_set_style_bg_color(ui_aqi_container, lv_color_hex(0x9CCA7F), 0);  // Default: 优
    lv_obj_set_style_bg_opa(ui_aqi_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_aqi_container, 0, 0);
    lv_obj_set_style_pad_all(ui_aqi_container, 0, 0);
    
    ui_aqi_label = lv_label_create(ui_aqi_container);
    lv_label_set_text(ui_aqi_label, "优");
    lv_obj_set_style_text_font(ui_aqi_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_aqi_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ui_aqi_label);
    
    // Humidity icon and label (middle section)
    ui_humid_icon_label = lv_label_create(ui_info_container);
    lv_label_set_text(ui_humid_icon_label, "湿");  // 湿度
    lv_obj_set_style_text_font(ui_humid_icon_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_humid_icon_label, lv_color_hex(0x0000FF), 0);
    lv_obj_set_pos(ui_humid_icon_label, 85, 8);
    
    ui_humid_label = lv_label_create(ui_info_container);
    lv_label_set_text(ui_humid_label, "65%");
    lv_obj_set_style_text_font(ui_humid_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_humid_label, lv_color_hex(0x000000), 0);
    lv_obj_set_pos(ui_humid_label, 110, 8);
    
    // Temperature icon and label (160-240px)
    ui_temp_icon_label = lv_label_create(ui_info_container);
    lv_label_set_text(ui_temp_icon_label, "温");  // 温度
    lv_obj_set_style_text_font(ui_temp_icon_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_temp_icon_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_pos(ui_temp_icon_label, 162, 8);
    
    ui_temp_label = lv_label_create(ui_info_container);
    lv_label_set_text(ui_temp_label, "25℃");
    lv_obj_set_style_text_font(ui_temp_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_temp_label, lv_color_hex(0x000000), 0);
    lv_obj_set_pos(ui_temp_label, 182, 8);
    
    // Date container (200-240px, height 40px)
    ui_date_container = lv_obj_create(ui_main_container);
    lv_obj_remove_style_all(ui_date_container);
    lv_obj_set_size(ui_date_container, 240, 34);
    lv_obj_set_pos(ui_date_container, 0, 200);
    lv_obj_remove_flag(ui_date_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Week label (0-60px)
    ui_week_label = lv_label_create(ui_date_container);
    lv_label_set_text(ui_week_label, "周一");
    lv_obj_set_style_text_font(ui_week_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_week_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_align(ui_week_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui_week_label, 5, 8);
    
    // Date label (61-160px)
    ui_date_label = lv_label_create(ui_date_container);
    lv_label_set_text(ui_date_label, "01月01日");
    lv_obj_set_style_text_font(ui_date_label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(ui_date_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_align(ui_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui_date_label, 70, 8);
}

void IdleScreen::ui_destroy() {
    if (ui_screen) {
        // Stop animation before deleting objects
        if (ui_xiaozhi_icon) {
            lv_anim_delete(ui_xiaozhi_icon, NULL);
        }
        
        lv_obj_delete(ui_screen);
        ui_screen = NULL;
    }
}

void IdleScreen::ui_showScreen(bool showIt) {
    if (!ui_screen) return;
    
    if (showIt) {
        lv_obj_remove_flag(ui_screen, LV_OBJ_FLAG_HIDDEN);
        ui_shown = true;
    } else {
        lv_obj_add_flag(ui_screen, LV_OBJ_FLAG_HIDDEN);
        ui_shown = false;
    }
}

void IdleScreen::ui_update() {
    if (!ui_screen || !ui_shown) return;
    
    // Update time display
    char hour_min_buf[16], second_buf[8], week_buf[16], date_buf[32];
    get_time_string(hour_min_buf, second_buf, week_buf, date_buf);
    
    if (ui_time_hour_min) {
        lv_label_set_text(ui_time_hour_min, hour_min_buf);
    }
    
    if (ui_time_second) {
        lv_label_set_text(ui_time_second, second_buf);
    }
    
    if (ui_week_label) {
        lv_label_set_text(ui_week_label, week_buf);
    }
    
    if (ui_date_label) {
        lv_label_set_text(ui_date_label, date_buf);
    }
    
    // Update scroll text every 2.5 seconds
    updateScrollText();
}

void IdleScreen::updateScrollText() {
    if (scroll_texts.empty()) return;
    
    uint32_t current_time = lv_tick_get();
    if (current_time - last_scroll_time > 2500) {  // 2.5 seconds
        current_scroll_index = (current_scroll_index + 1) % scroll_texts.size();
        if (ui_scroll_label) {
            lv_label_set_text(ui_scroll_label, scroll_texts[current_scroll_index].c_str());
        }
        last_scroll_time = current_time;
    }
}

void IdleScreen::ui_updateTheme(ThemeColors *p_current_theme) {
    p_theme = p_current_theme;
    // Weather clock uses fixed white background and black text
    // Theme colors are not applied to maintain Arduino design
}

void IdleScreen::ui_updateWeather(const WeatherData& weather) {
    ESP_LOGI(TAG, "Updating weather data");
    
    // Update city name
    if (ui_city_label) {
        lv_label_set_text(ui_city_label, weather.city_name.c_str());
    }
    
    // Update temperature
    if (ui_temp_label) {
        std::string temp_text = weather.temperature + "℃";
        lv_label_set_text(ui_temp_label, temp_text.c_str());
    }
    
    // Update humidity
    if (ui_humid_label) {
        lv_label_set_text(ui_humid_label, weather.humidity.c_str());
    }
    
    // Update AQI
    if (ui_aqi_label) {
        lv_label_set_text(ui_aqi_label, weather.aqi_desc.c_str());
        updateAQIColor(weather.aqi);
    }
    
    // Update scroll texts
    std::vector<std::string> texts;
    texts.push_back("实时天气 " + weather.weather_desc);
    texts.push_back("空气质量 " + weather.aqi_desc);
    texts.push_back("风向 " + weather.wind_direction + weather.wind_speed);
    texts.push_back("今日天气 " + weather.weather_desc);
    texts.push_back("最低温度 " + weather.temp_low + "℃");
    texts.push_back("最高温度 " + weather.temp_high + "℃");
    ui_setScrollText(texts);
}

void IdleScreen::ui_setScrollText(const std::vector<std::string>& texts) {
    scroll_texts = texts;
    current_scroll_index = 0;
    last_scroll_time = lv_tick_get();
    
    if (!texts.empty() && ui_scroll_label) {
        lv_label_set_text(ui_scroll_label, texts[0].c_str());
    }
}

void IdleScreen::updateAQIColor(int aqi) {
    if (!ui_aqi_container) return;
    
    lv_color_t color;
    if (aqi > 200) {
        // 重度污染 - 深红色
        color = lv_color_hex(0x880B20);
    } else if (aqi > 150) {
        // 中度污染 - 紫红色
        color = lv_color_hex(0xBA3779);
    } else if (aqi > 100) {
        // 轻度污染 - 橙色
        color = lv_color_hex(0xF29F39);
    } else if (aqi > 50) {
        // 良 - 黄色
        color = lv_color_hex(0xF7DB64);
    } else {
        // 优 - 绿色
        color = lv_color_hex(0x9CCA7F);
    }
    
    lv_obj_set_style_bg_color(ui_aqi_container, color, 0);
}

void IdleScreen::ui_showAlarmInfo(const char* alarm_message) {
    if (!ui_alarm_info_label) return;
    
    ESP_LOGI(TAG, "Showing alarm info: %s", alarm_message);
    lv_label_set_text(ui_alarm_info_label, alarm_message);
    lv_obj_remove_flag(ui_alarm_info_label, LV_OBJ_FLAG_HIDDEN);
}

void IdleScreen::ui_hideAlarmInfo() {
    if (!ui_alarm_info_label) return;
    
    ESP_LOGI(TAG, "Hiding alarm info");
    lv_obj_add_flag(ui_alarm_info_label, LV_OBJ_FLAG_HIDDEN);
}

#pragma GCC diagnostic pop

#endif // IDLE_SCREEN_HOOK
