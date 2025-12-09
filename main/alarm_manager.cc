#include "alarm_manager.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <time.h>
#include <sys/time.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

#define TAG "AlarmManager"
#define ALARM_SETTINGS_NAMESPACE "alarms"

AlarmManager::AlarmManager() 
    : initialized_(false), next_alarm_id_(1), 
      default_snooze_minutes_(5), default_max_snooze_count_(3) {
}

AlarmManager::~AlarmManager() {
    Cleanup();
}

void AlarmManager::Initialize() {
    if (initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing Alarm Manager");
    
    // 初始化设置存储
    settings_ = std::make_unique<Settings>(ALARM_SETTINGS_NAMESPACE, true);
    
    // 从存储中加载闹钟
    LoadAlarmsFromStorage();
    
    // 获取下一个闹钟ID
    next_alarm_id_ = settings_->GetInt("next_id", 1);
    
    initialized_ = true;
    ESP_LOGI(TAG, "Alarm Manager initialized with %d alarms", alarms_.size());
}

void AlarmManager::Cleanup() {
    if (!initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Cleaning up Alarm Manager");
    
    // 停止所有活动的闹钟
    StopAllActiveAlarms();
    
    // 保存数据
    SaveAlarmsToStorage();
    
    // 清理资源
    {
        std::lock_guard<std::mutex> lock(alarms_mutex_);
        alarms_.clear();
    }
    
    settings_.reset();
    initialized_ = false;
}

int AlarmManager::AddAlarm(int hour, int minute, AlarmRepeatMode repeat_mode, 
                          const std::string& label, const std::string& music_name) {
    if (!initialized_) {
        ESP_LOGE(TAG, "AlarmManager not initialized");
        return -1;
    }
    
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        ESP_LOGE(TAG, "Invalid time: %02d:%02d", hour, minute);
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto alarm = std::make_unique<AlarmItem>();
    alarm->id = GetNextAlarmId();
    alarm->hour = hour;
    alarm->minute = minute;
    alarm->repeat_mode = repeat_mode;
    alarm->label = label;
    alarm->music_name = music_name;
    alarm->status = kAlarmEnabled;
    alarm->snooze_minutes = default_snooze_minutes_;
    alarm->max_snooze_count = default_max_snooze_count_;
    
    // 设置星期掩码
    switch (repeat_mode) {
        case kAlarmDaily:
            alarm->weekdays_mask = 0b1111111; // 每天
            break;
        case kAlarmWeekdays:
            alarm->weekdays_mask = 0b0111110; // 周一到周五
            break;
        case kAlarmWeekends:
            alarm->weekdays_mask = 0b1000001; // 周六周日
            break;
        default:
            alarm->weekdays_mask = 0; // 一次性或自定义
            break;
    }
    
    int alarm_id = alarm->id;
    alarms_.push_back(std::move(alarm));
    
    // 保存到存储
    SaveAlarmToStorage(*alarms_.back());
    settings_->SetInt("next_id", next_alarm_id_);
    
    ESP_LOGI(TAG, "Added alarm %d: %02d:%02d, repeat=%d, label='%s', music='%s'", 
             alarm_id, hour, minute, repeat_mode, label.c_str(), music_name.c_str());
    
    return alarm_id;
}

bool AlarmManager::RemoveAlarm(int alarm_id) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [alarm_id](const auto& alarm) { return alarm->id == alarm_id; });
    
    if (it != alarms_.end()) {
        ESP_LOGI(TAG, "Removing alarm %d", alarm_id);
        alarms_.erase(it);
        RemoveAlarmFromStorage(alarm_id);
        return true;
    }
    
    ESP_LOGW(TAG, "Alarm %d not found for removal", alarm_id);
    return false;
}

bool AlarmManager::EnableAlarm(int alarm_id, bool enabled) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [alarm_id](const auto& alarm) { return alarm->id == alarm_id; });
    
    if (it != alarms_.end()) {
        (*it)->status = enabled ? kAlarmEnabled : kAlarmDisabled;
        SaveAlarmToStorage(**it);
        ESP_LOGI(TAG, "Alarm %d %s", alarm_id, enabled ? "enabled" : "disabled");
        return true;
    }
    
    return false;
}

bool AlarmManager::ModifyAlarm(int alarm_id, int hour, int minute, AlarmRepeatMode repeat_mode, 
                              const std::string& label, const std::string& music_name) {
    if (!initialized_) {
        return false;
    }
    
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        ESP_LOGE(TAG, "Invalid time: %02d:%02d", hour, minute);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [alarm_id](const auto& alarm) { return alarm->id == alarm_id; });
    
    if (it != alarms_.end()) {
        (*it)->hour = hour;
        (*it)->minute = minute;
        (*it)->repeat_mode = repeat_mode;
        (*it)->label = label;
        (*it)->music_name = music_name;
        
        // 重新设置星期掩码
        switch (repeat_mode) {
            case kAlarmDaily:
                (*it)->weekdays_mask = 0b1111111;
                break;
            case kAlarmWeekdays:
                (*it)->weekdays_mask = 0b0111110;
                break;
            case kAlarmWeekends:
                (*it)->weekdays_mask = 0b1000001;
                break;
            default:
                break;
        }
        
        SaveAlarmToStorage(**it);
        ESP_LOGI(TAG, "Modified alarm %d: %02d:%02d", alarm_id, hour, minute);
        return true;
    }
    
    return false;
}

std::vector<AlarmItem> AlarmManager::GetAllAlarms() const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    std::vector<AlarmItem> result;
    
    for (const auto& alarm : alarms_) {
        result.push_back(*alarm);
    }
    
    return result;
}

AlarmItem* AlarmManager::GetAlarm(int alarm_id) {
    if (!initialized_) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [alarm_id](const auto& alarm) { return alarm->id == alarm_id; });
    
    return (it != alarms_.end()) ? it->get() : nullptr;
}

std::vector<AlarmItem> AlarmManager::GetActiveAlarms() const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    std::vector<AlarmItem> result;
    
    for (const auto& alarm : alarms_) {
        if (alarm->status == kAlarmTriggered || alarm->status == kAlarmSnoozed) {
            result.push_back(*alarm);
        }
    }
    
    return result;
}

std::string AlarmManager::GetNextAlarmInfo() const {
    if (!initialized_) {
        return "闹钟管理器未初始化";
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    // 查找下一个要触发的闹钟
    AlarmItem* next_alarm = nullptr;
    int64_t current_time = GetCurrentTimeInMinutes();
    int current_weekday = GetCurrentWeekday();
    int64_t min_time_diff = 24 * 60 * 7; // 一周的分钟数
    
    for (const auto& alarm : alarms_) {
        if (alarm->status != kAlarmEnabled) {
            continue;
        }
        
        // 计算今天和明天的时间差
        int64_t alarm_time = alarm->hour * 60 + alarm->minute;
        
        for (int day_offset = 0; day_offset < 7; day_offset++) {
            int check_weekday = (current_weekday + day_offset) % 7;
            
            if (day_offset == 0 && alarm_time <= current_time) {
                continue; // 今天已经过了
            }
            
            if (alarm->repeat_mode == kAlarmOnce && day_offset > 0) {
                continue; // 一次性闹钟只检查今天
            }
            
            if (IsWeekdayActive(*alarm, check_weekday)) {
                int64_t time_diff = day_offset * 24 * 60 + alarm_time - current_time;
                if (day_offset == 0) {
                    time_diff = alarm_time - current_time;
                }
                
                if (time_diff < min_time_diff) {
                    min_time_diff = time_diff;
                    next_alarm = alarm.get();
                }
                break;
            }
        }
    }
    
    if (!next_alarm) {
        return "无活动闹钟";
    }
    
    // 格式化下一个闹钟信息
    std::ostringstream oss;
    oss << "下个闹钟: " << FormatTime(next_alarm->hour, next_alarm->minute);
    
    if (min_time_diff < 24 * 60) {
        int hours = min_time_diff / 60;
        int minutes = min_time_diff % 60;
        if (hours > 0) {
            oss << " (" << hours << "小时" << minutes << "分钟后)";
        } else {
            oss << " (" << minutes << "分钟后)";
        }
    } else {
        int days = min_time_diff / (24 * 60);
        oss << " (" << days << "天后)";
    }
    
    if (!next_alarm->label.empty()) {
        oss << " - " << next_alarm->label;
    }
    
    return oss.str();
}

bool AlarmManager::SnoozeAlarm(int alarm_id) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [alarm_id](const auto& alarm) { return alarm->id == alarm_id; });
    
    if (it != alarms_.end() && (*it)->status == kAlarmTriggered) {
        if ((*it)->snooze_count < (*it)->max_snooze_count) {
            (*it)->status = kAlarmSnoozed;
            (*it)->snooze_count++;
            (*it)->next_snooze_time = esp_timer_get_time() / 1000000 + (*it)->snooze_minutes * 60;
            
            ESP_LOGI(TAG, "Snoozed alarm %d for %d minutes (count: %d/%d)", 
                     alarm_id, (*it)->snooze_minutes, (*it)->snooze_count, (*it)->max_snooze_count);
            
            if (on_alarm_snoozed_) {
                on_alarm_snoozed_(**it);
            }
            return true;
        } else {
            ESP_LOGI(TAG, "Alarm %d exceeded max snooze count, stopping", alarm_id);
            StopAlarm(alarm_id);
        }
    }
    
    return false;
}

bool AlarmManager::StopAlarm(int alarm_id) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [alarm_id](const auto& alarm) { return alarm->id == alarm_id; });
    
    if (it != alarms_.end() && 
        ((*it)->status == kAlarmTriggered || (*it)->status == kAlarmSnoozed)) {
        
        AlarmStatus old_status = (*it)->status;
        (*it)->status = ((*it)->repeat_mode == kAlarmOnce) ? kAlarmDisabled : kAlarmEnabled;
        (*it)->snooze_count = 0;
        (*it)->next_snooze_time = 0;
        
        ESP_LOGI(TAG, "Stopped alarm %d", alarm_id);
        
        if (on_alarm_stopped_) {
            on_alarm_stopped_(**it);
        }
        
        return true;
    }
    
    return false;
}

void AlarmManager::StopAllActiveAlarms() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    for (auto& alarm : alarms_) {
        if (alarm->status == kAlarmTriggered || alarm->status == kAlarmSnoozed) {
            alarm->status = (alarm->repeat_mode == kAlarmOnce) ? kAlarmDisabled : kAlarmEnabled;
            alarm->snooze_count = 0;
            alarm->next_snooze_time = 0;
            
            if (on_alarm_stopped_) {
                on_alarm_stopped_(*alarm);
            }
        }
    }
    
    ESP_LOGI(TAG, "Stopped all active alarms");
}

void AlarmManager::CheckAlarms() {
    if (!initialized_) {
        return;
    }
    
    int64_t current_time_seconds = esp_timer_get_time() / 1000000;
    int64_t current_time_minutes = GetCurrentTimeInMinutes();
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    for (auto& alarm : alarms_) {
        // 检查贪睡闹钟
        if (alarm->status == kAlarmSnoozed && 
            current_time_seconds >= alarm->next_snooze_time) {
            
            alarm->status = kAlarmTriggered;
            alarm->next_snooze_time = 0;
            
            ESP_LOGI(TAG, "Snooze ended for alarm %d, triggering again", alarm->id);
            
            if (on_alarm_triggered_) {
                on_alarm_triggered_(*alarm);
            }
            continue;
        }
        
        // 检查正常闹钟触发
        if (alarm->status != kAlarmEnabled) {
            continue;
        }
        
        int64_t alarm_time_minutes = alarm->hour * 60 + alarm->minute;
        
        // 检查是否是触发时间（精确到分钟）
        if (alarm_time_minutes != current_time_minutes) {
            continue;
        }
        
        // 检查是否应该在今天触发
        if (!ShouldTriggerToday(*alarm)) {
            continue;
        }
        
        // 防止重复触发（同一分钟内）
        int64_t current_time_in_seconds = esp_timer_get_time() / 1000000;
        if (alarm->last_triggered_time > 0 && 
            (current_time_in_seconds - alarm->last_triggered_time) < 60) {
            continue;
        }
        
        // 触发闹钟
        alarm->status = kAlarmTriggered;
        alarm->last_triggered_time = current_time_in_seconds;
        alarm->snooze_count = 0;
        
        ESP_LOGI(TAG, "Triggering alarm %d: %02d:%02d - %s", 
                 alarm->id, alarm->hour, alarm->minute, alarm->label.c_str());
        
        if (on_alarm_triggered_) {
            on_alarm_triggered_(*alarm);
        }
    }
}

void AlarmManager::SetAlarmTriggeredCallback(AlarmTriggeredCallback callback) {
    on_alarm_triggered_ = callback;
}

void AlarmManager::SetAlarmSnoozeCallback(AlarmSnoozeCallback callback) {
    on_alarm_snoozed_ = callback;
}

void AlarmManager::SetAlarmStopCallback(AlarmStopCallback callback) {
    on_alarm_stopped_ = callback;
}

void AlarmManager::SetDefaultSnoozeMinutes(int minutes) {
    default_snooze_minutes_ = std::max(1, std::min(60, minutes));
}

void AlarmManager::SetDefaultMaxSnoozeCount(int count) {
    default_max_snooze_count_ = std::max(0, std::min(10, count));
}

// 静态工具方法
std::string AlarmManager::FormatTime(int hour, int minute) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hour 
        << ":" << std::setfill('0') << std::setw(2) << minute;
    return oss.str();
}

std::string AlarmManager::FormatAlarmTime(const AlarmItem& alarm) {
    std::ostringstream oss;
    oss << FormatTime(alarm.hour, alarm.minute);
    
    switch (alarm.repeat_mode) {
        case kAlarmOnce:
            oss << " (一次)";
            break;
        case kAlarmDaily:
            oss << " (每日)";
            break;
        case kAlarmWeekdays:
            oss << " (工作日)";
            break;
        case kAlarmWeekends:
            oss << " (周末)";
            break;
        case kAlarmCustom:
            oss << " (自定义)";
            break;
    }
    
    return oss.str();
}

bool AlarmManager::IsWeekdayActive(const AlarmItem& alarm, int weekday) {
    if (alarm.repeat_mode == kAlarmOnce) {
        return true; // 一次性闹钟在任何一天都可以触发
    }
    
    return (alarm.weekdays_mask & (1 << weekday)) != 0;
}

// 私有方法实现
void AlarmManager::LoadAlarmsFromStorage() {
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    alarms_.clear();
    
    // 读取闹钟数量
    int alarm_count = settings_->GetInt("count", 0);
    ESP_LOGI(TAG, "Loading %d alarms from storage", alarm_count);
    
    for (int i = 0; i < alarm_count; i++) {
        std::string alarm_key = "alarm_" + std::to_string(i);
        std::string alarm_json = settings_->GetString(alarm_key);
        
        if (alarm_json.empty()) {
            continue;
        }
        
        cJSON* json = cJSON_Parse(alarm_json.c_str());
        if (!json) {
            ESP_LOGW(TAG, "Failed to parse alarm JSON: %s", alarm_key.c_str());
            continue;
        }
        
        auto alarm = std::make_unique<AlarmItem>();
        
        // 解析JSON数据
        if (auto id = cJSON_GetObjectItem(json, "id")) alarm->id = id->valueint;
        if (auto hour = cJSON_GetObjectItem(json, "hour")) alarm->hour = hour->valueint;
        if (auto minute = cJSON_GetObjectItem(json, "minute")) alarm->minute = minute->valueint;
        if (auto repeat = cJSON_GetObjectItem(json, "repeat")) alarm->repeat_mode = (AlarmRepeatMode)repeat->valueint;
        if (auto mask = cJSON_GetObjectItem(json, "weekdays")) alarm->weekdays_mask = mask->valueint;
        if (auto status = cJSON_GetObjectItem(json, "status")) alarm->status = (AlarmStatus)status->valueint;
        if (auto label = cJSON_GetObjectItem(json, "label")) alarm->label = label->valuestring;
        if (auto music = cJSON_GetObjectItem(json, "music")) alarm->music_name = music->valuestring;
        if (auto snooze_min = cJSON_GetObjectItem(json, "snooze_minutes")) alarm->snooze_minutes = snooze_min->valueint;
        if (auto max_snooze = cJSON_GetObjectItem(json, "max_snooze")) alarm->max_snooze_count = max_snooze->valueint;
        
        // 重置运行时状态
        alarm->snooze_count = 0;
        alarm->last_triggered_time = 0;
        alarm->next_snooze_time = 0;
        if (alarm->status == kAlarmTriggered || alarm->status == kAlarmSnoozed) {
            alarm->status = kAlarmEnabled;
        }
        
        alarms_.push_back(std::move(alarm));
        cJSON_Delete(json);
    }
    
    ESP_LOGI(TAG, "Loaded %d alarms successfully", alarms_.size());
}

void AlarmManager::SaveAlarmsToStorage() {
    if (!settings_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(alarms_mutex_);
    
    // 保存闹钟数量
    settings_->SetInt("count", alarms_.size());
    
    // 保存每个闹钟
    for (size_t i = 0; i < alarms_.size(); i++) {
        std::string alarm_key = "alarm_" + std::to_string(i);
        SaveAlarmToStorage(*alarms_[i]);
    }
    
    ESP_LOGI(TAG, "Saved %d alarms to storage", alarms_.size());
}

void AlarmManager::SaveAlarmToStorage(const AlarmItem& alarm) {
    if (!settings_) {
        return;
    }
    
    cJSON* json = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(json, "id", alarm.id);
    cJSON_AddNumberToObject(json, "hour", alarm.hour);
    cJSON_AddNumberToObject(json, "minute", alarm.minute);
    cJSON_AddNumberToObject(json, "repeat", alarm.repeat_mode);
    cJSON_AddNumberToObject(json, "weekdays", alarm.weekdays_mask);
    cJSON_AddNumberToObject(json, "status", alarm.status);
    cJSON_AddStringToObject(json, "label", alarm.label.c_str());
    cJSON_AddStringToObject(json, "music", alarm.music_name.c_str());
    cJSON_AddNumberToObject(json, "snooze_minutes", alarm.snooze_minutes);
    cJSON_AddNumberToObject(json, "max_snooze", alarm.max_snooze_count);
    
    char* json_string = cJSON_PrintUnformatted(json);
    
    // 找到这个闹钟在数组中的位置
    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                          [&alarm](const auto& a) { return a->id == alarm.id; });
    
    if (it != alarms_.end()) {
        size_t index = std::distance(alarms_.begin(), it);
        std::string alarm_key = "alarm_" + std::to_string(index);
        settings_->SetString(alarm_key, json_string);
    }
    
    cJSON_free(json_string);
    cJSON_Delete(json);
}

void AlarmManager::RemoveAlarmFromStorage(int alarm_id) {
    // 重新保存所有闹钟（简单实现）
    SaveAlarmsToStorage();
}

int AlarmManager::GetNextAlarmId() {
    return next_alarm_id_++;
}

bool AlarmManager::ShouldTriggerToday(const AlarmItem& alarm) const {
    if (alarm.repeat_mode == kAlarmOnce) {
        return true;
    }
    
    int weekday = GetCurrentWeekday();
    return IsWeekdayActive(alarm, weekday);
}

int64_t AlarmManager::GetCurrentTimeInMinutes() const {
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_hour * 60 + timeinfo->tm_min;
}

int AlarmManager::GetCurrentWeekday() const {
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_wday; // 0=周日, 1=周一, ..., 6=周六
}
