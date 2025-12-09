#ifndef DISPLAY_H
#define DISPLAY_H

#include "emoji_collection.h"

#ifndef CONFIG_USE_EMOTE_MESSAGE_STYLE
#define HAVE_LVGL 1
#include <lvgl.h>
#endif

#include <esp_timer.h>
#include <esp_log.h>
#include <esp_pm.h>

#include <string>
#include <chrono>

class Theme {
public:
    Theme(const std::string& name) : name_(name) {}
    virtual ~Theme() = default;

    inline std::string name() const { return name_; }
private:
    std::string name_;
};

class Display {
public:
    Display();
    virtual ~Display();

    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string &notification, int duration_ms = 3000);
    virtual void SetEmotion(const char* emotion);
    virtual void SetChatMessage(const char* role, const char* content);
    virtual void SetTheme(Theme* theme);
    virtual Theme* GetTheme() { return current_theme_; }
    virtual void UpdateStatusBar(bool update_all = false);
    virtual void SetPowerSaveMode(bool on);
    virtual void SetMusicInfo(const char* song_name);
    virtual void SetMusicProgress(const char* song_name, int current_seconds, int total_seconds, float progress_percent);
    virtual void ClearMusicInfo();

    // Alarm display on idle screen (for boards with idle screen support)
    virtual void ShowAlarmOnIdleScreen(const char* alarm_message) {}  // Default: do nothing
    virtual void HideAlarmOnIdleScreen() {}  // Default: do nothing

    // State change and clock timer callbacks (for boards with idle screen support)
    virtual void OnStateChanged() {}  // Default: do nothing
    virtual void OnClockTimer() {}    // Default: do nothing, called every second

    virtual void start() {}
    virtual void clearScreen() {}  // 清除FFT显示，默认为空实现
    virtual void stopFft() {}      // 停止FFT显示，默认为空实现

    inline int width() const { return width_; }
    inline int height() const { return height_; }

protected:
    int width_ = 0;
    int height_ = 0;

    Theme* current_theme_ = nullptr;

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t *emotion_label_ = nullptr;
};


class DisplayLockGuard {
public:
    DisplayLockGuard(Display *display) : display_(display) {
        if (!display_->Lock(30000)) {
            ESP_LOGE("Display", "Failed to lock display");
        }
    }
    ~DisplayLockGuard() {
        display_->Unlock();
    }

private:
    Display *display_;
};

class NoDisplay : public Display {
private:
    virtual bool Lock(int timeout_ms = 0) override {
        return true;
    }
    virtual void Unlock() override {}
};

#endif
