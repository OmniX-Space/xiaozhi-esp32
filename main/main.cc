#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>

#include "application.h"
#include "system_info.h"

#include <esp_psram.h>

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Detecting PSRAM
    if (esp_psram_is_initialized())
    {
        size_t psram_size = esp_psram_get_size();
        ESP_LOGI("PSRAM", "PSRAM is available and initialized. Total size: %zu bytes", psram_size);
    }
    else
    {
        ESP_LOGE("PSRAM", "PSRAM not found or not initialized.");
    }

    // Launch the application
    auto &app = Application::GetInstance();
    app.Start();
    app.MainEventLoop();
}
