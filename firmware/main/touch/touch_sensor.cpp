#include "touch_sensor.h"
#include "driver/touch_sens.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/touch_sensor_channel.h"

namespace utsushimi {
namespace {
constexpr const char *kTag = "touch_a";
constexpr int kChannel = TOUCH_PAD_GPIO1_CHANNEL;
constexpr int kPeriodMs = 250;
constexpr int kScanTimeoutMs = 50;
static_assert(TOUCH_PAD_NUM1_GPIO_NUM == 1 && SOC_TOUCH_SENSOR_VERSION == 2);

bool check(esp_err_t error, const char *operation) {
    if (error == ESP_OK) return true;
    ESP_LOGE(kTag, "elapsed_ms=%lld %s failed: %s (0x%x)",
             static_cast<long long>(esp_timer_get_time() / 1000), operation,
             esp_err_to_name(error), static_cast<unsigned>(error));
    return false;
}

// このタスクだけがコントローラーを所有する。後続の電極も同じコントローラーへ追加できる。
struct Sensor {
    touch_sensor_handle_t controller = nullptr;
    touch_channel_handle_t channel = nullptr;

    bool init() {
        // ESP-IDF v5.5.6 touch_sens_basicのV2測定設定を使用。
        touch_sensor_sample_config_t sample = TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(
            500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2);
        touch_sensor_config_t config = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, &sample);
        if (!check(touch_sensor_new_controller(&config, &controller), "new_controller")) return false;
        touch_channel_config_t channel_config{};
        channel_config.charge_speed = TOUCH_CHARGE_SPEED_7;
        channel_config.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
        // active_threshは未使用。フィルター・接触イベントを有効にせずRAWのみ取得する。
        if (!check(touch_sensor_new_channel(controller, kChannel, &channel_config, &channel), "new_channel")) return false;
        if (!check(touch_sensor_config_filter(controller, nullptr), "disable_filter")) return false;
        return check(touch_sensor_enable(controller), "enable");
    }

    // 初期化失敗時のみ使用（enable成功後はタスクが所有し続ける）。
    void release_failed_init() {
        if (channel && !check(touch_sensor_del_channel(channel), "delete_channel")) return;
        if (controller) check(touch_sensor_del_controller(controller), "delete_controller");
    }
};

void touch_task(void *) {
    Sensor sensor;
    if (!sensor.init()) {
        sensor.release_failed_init();
        ESP_LOGE(kTag, "Diagnostics stopped; eyes/body continue. Reset after resolving the error.");
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag, "GPIO1 TOUCH1 (J8 pin 3), raw sampling every %dms; no touch classification", kPeriodMs);
    unsigned warmup = 3;
    TickType_t tick = xTaskGetTickCount();
    for (;;) {
        // 完了した新規測定だけを読む。待機はこのタスク内で最大50ms+RTOS tick。
        // タイムアウト時は古いレジスター値を正常なサンプルとして出力しない。
        if (check(touch_sensor_trigger_oneshot_scanning(sensor.controller, kScanTimeoutMs), "scan")) {
            uint32_t raw = 0;
            if (check(touch_channel_read_data(sensor.channel, TOUCH_CHAN_DATA_TYPE_RAW, &raw), "read_raw")) {
                if (warmup) {
                    --warmup;
                    if (!warmup) ESP_LOGI(kTag, "Warmup complete; raw observation ready");
                } else {
                    ESP_LOGI(kTag, "elapsed_ms=%lld raw=%lu status=ok",
                             static_cast<long long>(esp_timer_get_time() / 1000),
                             static_cast<unsigned long>(raw));
                }
            }
        }
        // 遅延時に過去の測定を追いかけて連続実行しない。
        if (xTaskDelayUntil(&tick, pdMS_TO_TICKS(kPeriodMs)) == pdFALSE) tick = xTaskGetTickCount();
    }
}
}

void start_touch_diagnostics() {
    if (xTaskCreate(touch_task, "touch_raw", 4096, nullptr, 1, nullptr) != pdPASS)
        ESP_LOGE(kTag, "Task allocation failed; eyes/body continue without touch diagnostics");
}
} // namespace utsushimi
