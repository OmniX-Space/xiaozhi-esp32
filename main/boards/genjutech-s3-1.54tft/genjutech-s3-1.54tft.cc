#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include <esp_efuse_table.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "power_save_timer.h"

#include "assets/lang_config.h"
#include "power_manager.h"
#include "alarm_manager.h"  // 用于检测和停止闹钟

#if IDLE_SCREEN_HOOK
#include "idle_screen.h"
#include "weather_service.h"
#endif

#define TAG "GenJuTech_s3_1_54TFT"

#if IDLE_SCREEN_HOOK
LV_FONT_DECLARE(font_puhui_20_4);

// Extended SpiLcdDisplay with idle screen support
class SpiLcdDisplayEx : public SpiLcdDisplay {
public:
    SpiLcdDisplayEx(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy) :
                  SpiLcdDisplay(panel_io, panel,
                  width, height, offset_x, offset_y,
                  mirror_x, mirror_y, swap_xy) {
        DisplayLockGuard lock(this);
        lv_obj_set_style_pad_left(status_bar_, 20, 0);
        lv_obj_set_style_pad_right(status_bar_, 20, 0);
    }

    virtual void OnStateChanged() override {
        DisplayLockGuard lock(this);
        auto& app = Application::GetInstance();
        auto device_state = app.GetDeviceState();
        switch (device_state) {
            case kDeviceStateIdle:
                ESP_LOGI(TAG, "hide xiaozhi, show idle screen");
                if (!lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
                }
                _lcdScnIdle.ui_showScreen(true);
                break;

            case kDeviceStateListening:
            case kDeviceStateConnecting:
            case kDeviceStateSpeaking:
                ESP_LOGI(TAG, "show xiaozhi, hide idle screen");
                _lcdScnIdle.ui_showScreen(false);
                if (lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
                }
                break;

            default:
                break;
        }
    }

    virtual void OnClockTimer() override {
        DisplayLockGuard lock(this);
        _lcdScnIdle.ui_update(); // update screen every 1s
    }

    void IdleScrSetupUi() {
        DisplayLockGuard lock(this);  // ← 必须加锁！LVGL不是线程安全的
        ESP_LOGI(TAG, "IdleScrSetupUi()");
        // Get ThemeColors from current theme
        ThemeColors theme_colors;
        theme_colors.background = lv_color_hex(0x000000);
        theme_colors.text = lv_color_hex(0xFFFFFF);
        theme_colors.border = lv_color_hex(0x444444);
        theme_colors.chat_background = lv_color_hex(0x111111);
        theme_colors.user_bubble = lv_color_hex(0x0078D4);
        theme_colors.assistant_bubble = lv_color_hex(0x2D2D2D);
        theme_colors.system_bubble = lv_color_hex(0x1A1A1A);
        theme_colors.system_text = lv_color_hex(0xFFFFFF);
        theme_colors.low_battery = lv_color_hex(0xFF0000);
        
        _lcdScnIdle.ui_init(&theme_colors);
    }
    
    void UpdateTheme() {
        DisplayLockGuard lock(this);  // ← 必须加锁！
        ThemeColors theme_colors;
        theme_colors.background = lv_color_hex(0x000000);
        theme_colors.text = lv_color_hex(0xFFFFFF);
        theme_colors.border = lv_color_hex(0x444444);
        theme_colors.chat_background = lv_color_hex(0x111111);
        theme_colors.user_bubble = lv_color_hex(0x0078D4);
        theme_colors.assistant_bubble = lv_color_hex(0x2D2D2D);
        theme_colors.system_bubble = lv_color_hex(0x1A1A1A);
        theme_colors.system_text = lv_color_hex(0xFFFFFF);
        theme_colors.low_battery = lv_color_hex(0xFF0000);
        
        _lcdScnIdle.ui_updateTheme(&theme_colors);
    }

    // Override alarm display methods
    virtual void ShowAlarmOnIdleScreen(const char* alarm_message) override {
        DisplayLockGuard lock(this);
        ESP_LOGI(TAG, "ShowAlarmOnIdleScreen: %s", alarm_message);
        _lcdScnIdle.ui_showAlarmInfo(alarm_message);
        
        // Make sure idle screen is visible
        if (!_lcdScnIdle.ui_shown) {
            _lcdScnIdle.ui_showScreen(true);
        }
        
        // Hide xiaozhi interface
        if (!lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    virtual void HideAlarmOnIdleScreen() override {
        DisplayLockGuard lock(this);
        ESP_LOGI(TAG, "HideAlarmOnIdleScreen");
        _lcdScnIdle.ui_hideAlarmInfo();
    }
    
    void InitWeatherService() {
        ESP_LOGI(TAG, "Initializing weather service");
        
        // Start weather update task
        xTaskCreate([](void* param) {
            auto* self = static_cast<SpiLcdDisplayEx*>(param);
            ESP_LOGI(TAG, "Weather update task started");
            
            // Wait 5 seconds for WiFi to connect
            vTaskDelay(pdMS_TO_TICKS(5000));
            
            // Auto-detect city code by IP (leave empty string for auto-detect)
            // Or specify a city code like "101010100" for Beijing
            self->weather_service_.Initialize("");  // Empty = auto-detect
            
            // Set callback to update UI when weather data is received
            self->weather_service_.SetWeatherCallback([self](const WeatherData& weather) {
                DisplayLockGuard lock(self);
                self->_lcdScnIdle.ui_updateWeather(weather);
            });
            
            // Fetch weather immediately after initialization
            self->weather_service_.FetchWeather();
            
            // Continue updating weather every 10 minutes
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(600000));  // 10 minutes
                self->weather_service_.FetchWeather();
            }
        }, "weather_task", 8192, this, 5, NULL);  // Increased stack size for HTTP operations
    }

private:
    IdleScreen _lcdScnIdle;
    WeatherService weather_service_;
};
#endif // IDLE_SCREEN_HOOK

class SparkBotEs8311AudioCodec : public Es8311AudioCodec {
    private:    
    
    public:
        SparkBotEs8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
                            gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                            gpio_num_t pa_pin, uint8_t es8311_addr, bool use_mclk = true)
            : Es8311AudioCodec(i2c_master_handle, i2c_port, input_sample_rate, output_sample_rate,
                                 mclk,  bclk,  ws,  dout,  din,pa_pin,  es8311_addr,  use_mclk = true) {}
    
        void EnableOutput(bool enable) override {
            if (enable == output_enabled_) {
                return;
            }
            if (enable) {
                Es8311AudioCodec::EnableOutput(enable);
            } else {
               // Nothing todo because the display io and PA io conflict
            }
        }
    };

class GenJuTech_s3_1_54TFT : public WifiBoard {
private:
    // i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
#if IDLE_SCREEN_HOOK
    SpiLcdDisplayEx* display_;
#else
    LcdDisplay* display_;
#endif
    i2c_master_bus_handle_t codec_i2c_bus_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_16);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        // 第一个参数不为 -1 时，进入睡眠会关闭音频输入
        power_save_timer_ = new PowerSaveTimer(240, 60);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            
            // 如果有闹钟正在播放，优先停止闹钟，而不是切换界面
            auto& alarm_manager = AlarmManager::GetInstance();
            auto active_alarms = alarm_manager.GetActiveAlarms();
            if (!active_alarms.empty()) {
                ESP_LOGI(TAG, "Boot button pressed during alarm, stopping alarm");
                for (const auto& alarm : active_alarms) {
                    alarm_manager.StopAlarm(alarm.id);
                }
                return;  // 不切换界面
            }
            
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        // boot_button_.OnPressDown([this]() {
        //     Application::GetInstance().StartListening();
        // });
        // boot_button_.OnPressUp([this]() {
        //     Application::GetInstance().StopListening();
        // });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSt7789Display() {
        gpio_config_t config = {
            .pin_bit_mask = (1ULL << DISPLAY_RES),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(DISPLAY_RES, 0);
        vTaskDelay(20);
        gpio_set_level(DISPLAY_RES, 1);

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

#if IDLE_SCREEN_HOOK
        display_ = new SpiLcdDisplayEx(panel_io, panel,
                            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#else
        display_ = new SpiLcdDisplay(panel_io, panel,
                            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
    }

public:
    GenJuTech_s3_1_54TFT() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing GenJuTech S3 1.54 Board");
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeCodecI2c();
        InitializeSpi();
        InitializeButtons();
        InitializeSt7789Display();
        GetBacklight()->RestoreBrightness();
        
#if IDLE_SCREEN_HOOK
        auto* display_ex = static_cast<SpiLcdDisplayEx*>(display_);
        display_ex->IdleScrSetupUi();
        display_ex->InitWeatherService();
#endif
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static SparkBotEs8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(GenJuTech_s3_1_54TFT);
