#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <esp_timer.h>
#include "settings.h"

// 闹钟重复模式
enum AlarmRepeatMode {
    kAlarmOnce = 0,      // 一次性闹钟
    kAlarmDaily = 1,     // 每日重复
    kAlarmWeekdays = 2,  // 工作日(周一到周五)
    kAlarmWeekends = 3,  // 周末(周六周日)
    kAlarmCustom = 4     // 自定义星期(使用weekdays_mask)
};

// 闹钟状态
enum AlarmStatus {
    kAlarmEnabled = 0,   // 启用
    kAlarmDisabled = 1,  // 禁用
    kAlarmTriggered = 2, // 已触发(等待贪睡或关闭)
    kAlarmSnoozed = 3    // 贪睡中
};

// 闹钟项结构
struct AlarmItem {
    int id;                          // 闹钟ID
    int hour;                        // 小时 (0-23)
    int minute;                      // 分钟 (0-59)
    AlarmRepeatMode repeat_mode;     // 重复模式
    uint8_t weekdays_mask;           // 星期掩码 (bit0=周日, bit1=周一, ..., bit6=周六)
    AlarmStatus status;              // 闹钟状态
    std::string label;               // 闹钟标签/备注
    std::string music_name;          // 指定的音乐名称(空则使用默认铃声)
    int snooze_count;                // 当前贪睡次数
    int max_snooze_count;            // 最大贪睡次数 (默认3次)
    int snooze_minutes;              // 贪睡间隔(分钟，默认5分钟)
    int64_t last_triggered_time;     // 上次触发时间戳(避免重复触发)
    int64_t next_snooze_time;        // 下次贪睡时间戳
    
    AlarmItem() : id(0), hour(0), minute(0), repeat_mode(kAlarmOnce), 
                  weekdays_mask(0), status(kAlarmEnabled), label(""), 
                  music_name(""), snooze_count(0), max_snooze_count(3), 
                  snooze_minutes(5), last_triggered_time(0), next_snooze_time(0) {}
};

// 闹钟触发回调类型
using AlarmTriggeredCallback = std::function<void(const AlarmItem& alarm)>;
using AlarmSnoozeCallback = std::function<void(const AlarmItem& alarm)>;
using AlarmStopCallback = std::function<void(const AlarmItem& alarm)>;

class AlarmManager {
public:
    static AlarmManager& GetInstance() {
        static AlarmManager instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值运算符
    AlarmManager(const AlarmManager&) = delete;
    AlarmManager& operator=(const AlarmManager&) = delete;

    // 初始化和清理
    void Initialize();
    void Cleanup();

    // 闹钟管理
    int AddAlarm(int hour, int minute, AlarmRepeatMode repeat_mode = kAlarmOnce, 
                 const std::string& label = "", const std::string& music_name = "");
    bool RemoveAlarm(int alarm_id);
    bool EnableAlarm(int alarm_id, bool enabled = true);
    bool ModifyAlarm(int alarm_id, int hour, int minute, AlarmRepeatMode repeat_mode = kAlarmOnce, 
                     const std::string& label = "", const std::string& music_name = "");
    
    // 查询功能
    std::vector<AlarmItem> GetAllAlarms() const;
    AlarmItem* GetAlarm(int alarm_id);
    std::vector<AlarmItem> GetActiveAlarms() const;
    std::string GetNextAlarmInfo() const;  // 获取下一个闹钟的信息字符串
    
    // 贪睡和停止
    bool SnoozeAlarm(int alarm_id);
    bool StopAlarm(int alarm_id);
    void StopAllActiveAlarms();
    
    // 时间检查 (由Application的CLOCK_TICK调用)
    void CheckAlarms();
    
    // 回调设置
    void SetAlarmTriggeredCallback(AlarmTriggeredCallback callback);
    void SetAlarmSnoozeCallback(AlarmSnoozeCallback callback);
    void SetAlarmStopCallback(AlarmStopCallback callback);
    
    // 配置设置
    void SetDefaultSnoozeMinutes(int minutes);
    void SetDefaultMaxSnoozeCount(int count);
    
    // 时间工具
    static std::string FormatTime(int hour, int minute);
    static std::string FormatAlarmTime(const AlarmItem& alarm);
    static bool IsWeekdayActive(const AlarmItem& alarm, int weekday); // 0=周日, 1=周一, ..., 6=周六

private:
    AlarmManager();
    ~AlarmManager();

    // 内部方法
    void LoadAlarmsFromStorage();
    void SaveAlarmsToStorage();
    void SaveAlarmToStorage(const AlarmItem& alarm);
    void RemoveAlarmFromStorage(int alarm_id);
    
    int GetNextAlarmId();
    bool ShouldTriggerToday(const AlarmItem& alarm) const;
    int64_t GetCurrentTimeInMinutes() const;  // 获取当前时间的分钟数(从午夜开始)
    int GetCurrentWeekday() const;            // 获取当前星期几
    
    // 成员变量
    std::vector<std::unique_ptr<AlarmItem>> alarms_;
    std::unique_ptr<Settings> settings_;
    bool initialized_;
    int next_alarm_id_;
    
    // 配置
    int default_snooze_minutes_;
    int default_max_snooze_count_;
    
    // 回调函数
    AlarmTriggeredCallback on_alarm_triggered_;
    AlarmSnoozeCallback on_alarm_snoozed_;
    AlarmStopCallback on_alarm_stopped_;
    
    // 互斥锁保护
    mutable std::mutex alarms_mutex_;
};

#endif // ALARM_MANAGER_H
