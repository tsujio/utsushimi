#include "touch_sensor.h"
#include "contact_detector.h"
#include "adaptive_contact.h"
#include "sdkconfig.h"
#include "driver/touch_sens.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/touch_sensor_channel.h"

namespace utsushimi {
namespace {
constexpr const char *kTag = "touch";
struct ContactSnapshot { bool held; int64_t measured_us; };
QueueHandle_t contact_snapshot = nullptr;
QueueHandle_t petting_events = nullptr;
constexpr int64_t kContactStaleUs = 500000;
constexpr int kPeriodMs = 50;
constexpr int kLogPeriodMs = 250;
// 3チャンネル共通の期限。測定中はドライバーがvTaskDelay(1)でCPUを譲る。
constexpr int kScanTimeoutMs = 100;
// 描画(4)より高くし、各チャンネルの測定完了確認を描画負荷で遅らせない。
constexpr UBaseType_t kTouchPriority = 5;
static_assert(SOC_TOUCH_SENSOR_VERSION == 2);
static_assert(TOUCH_PAD_NUM1_GPIO_NUM == 1 && TOUCH_PAD_NUM7_GPIO_NUM == 7 && TOUCH_PAD_NUM8_GPIO_NUM == 8);
struct Electrode {
    const char *tag;
    int channel_id;
    int header_pin;
    touch_channel_handle_t channel = nullptr;
    ContactDetector detector{};
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
    AdaptiveContact trial{};
#endif
    unsigned warmup = 3;
    uint64_t last_log_ms = 0;
};

bool check(esp_err_t error, const char *operation, const char *tag = kTag) {
    if (error == ESP_OK) return true;
    ESP_LOGE(tag, "elapsed_ms=%lld %s failed: %s (0x%x)",
             static_cast<long long>(esp_timer_get_time() / 1000), operation,
             esp_err_to_name(error), static_cast<unsigned>(error));
    return false;
}

// このタスクだけがコントローラーを所有する。後続の電極も同じコントローラーへ追加できる。
struct Sensor {
    touch_sensor_handle_t controller = nullptr;
    Electrode electrodes[3] = {
        {"touch_a", TOUCH_PAD_GPIO1_CHANNEL, 3},
        {"touch_b", TOUCH_PAD_GPIO7_CHANNEL, 13},
        {"touch_c", TOUCH_PAD_GPIO8_CHANNEL, 15},
    };

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
        for (auto &e : electrodes) {
            if (!check(touch_sensor_new_channel(controller, e.channel_id, &channel_config, &e.channel),
                       "new_channel", e.tag)) return false;
            ESP_LOGI(e.tag, "GPIO%d TOUCH%d (J8 pin %d), sampling every %dms; keep untouched for calibration",
                     e.channel_id, e.channel_id, e.header_pin, kPeriodMs);
        }
        if (!check(touch_sensor_config_filter(controller, nullptr), "disable_filter")) return false;
        return check(touch_sensor_enable(controller), "enable");
    }

    // 初期化失敗時のみ使用（enable成功後はタスクが所有し続ける）。
    void release_failed_init() {
        bool released = true;
        for (auto &e : electrodes)
            if (e.channel && !check(touch_sensor_del_channel(e.channel), "delete_channel", e.tag)) released = false;
        if (!released) return;
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
    PettingDetector petting;
    unsigned scan_failures = 0;
    int64_t last_scan_warning_us = 0;
    TickType_t tick = xTaskGetTickCount();
    for (;;) {
        // 完了した新規測定だけを読む。期限は100msだが、スケジューリングで超過し得る。
        // タイムアウト時は古いレジスター値を正常なサンプルとして出力しない。
        const int64_t scan_start = esp_timer_get_time();
        const auto scan_result = touch_sensor_trigger_oneshot_scanning(sensor.controller, kScanTimeoutMs);
        const int64_t scan_end = esp_timer_get_time();
        const long long scan_us = static_cast<long long>(scan_end - scan_start);
        if (check(scan_result, "scan")) {
            if (scan_failures) {
                ESP_LOGI(kTag, "Scan recovered after %u failures; scan_us=%lld", scan_failures, scan_us);
                scan_failures = 0;
            }
            if (scan_us > kPeriodMs * 1000 && scan_end - last_scan_warning_us >= 2000000) {
                ESP_LOGW(kTag, "Slow scan: scan_us=%lld period_ms=%d", scan_us, kPeriodMs);
                last_scan_warning_us = scan_end;
            }
            // 同一走査の完了時刻を各検出器へ渡す。A/B/Cは順次走査する。
            const uint64_t now = esp_timer_get_time() / 1000;
            bool valid = true;
            uint8_t mask = 0, bit = 1;
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
            uint32_t trace_raw[3]{};
            unsigned trace_index = 0;
            unsigned trial_mask = 0, fault_mask = 0;
#endif
            for (auto &e : sensor.electrodes) {
                auto &detector = e.detector;
                auto &warmup = e.warmup;
                auto &last_log_ms = e.last_log_ms;
                uint32_t raw = 0;
                if (check(touch_channel_read_data(e.channel, TOUCH_CHAN_DATA_TYPE_RAW, &raw), "read_raw", e.tag)) {
                    if (warmup) {
                        --warmup;
                        if (!warmup) ESP_LOGI(e.tag, "Warmup complete; keep untouched for calibration (~2s)");
                    } else {
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
                        trace_raw[trace_index] = raw;
                        e.trial.update(raw, now);
                        if (e.trial.touching()) trial_mask |= bit;
                        if (e.trial.fault()) fault_mask |= bit;
#endif
                        const auto result = detector.update(raw, now);
                        const char *event = nullptr;
                        switch (result) {
                        case ContactUpdate::calibrated: event = "calibrated"; break;
                        case ContactUpdate::calibration_rejected:
                            ESP_LOGW(e.tag, "Calibration unstable/zero; keep untouched, retrying"); break;
                        case ContactUpdate::touch_started: event = "touch_started"; break;
                        case ContactUpdate::touch_ended: event = "touch_ended"; break;
                        case ContactUpdate::none: break;
                        }
                        if (event || now - last_log_ms >= kLogPeriodMs) {
                            ESP_LOGI(e.tag, "elapsed_ms=%llu raw=%lu baseline=%.1f delta=%.1f state=%s event=%s",
                                     static_cast<unsigned long long>(now), static_cast<unsigned long>(raw),
                                     detector.baseline(), detector.ready() ? detector.delta(raw) : 0,
                                     !detector.ready() ? "calibrating" : detector.touching() ? "touched" : "idle",
                                     event ? event : "none");
                            last_log_ms = now;
                        }
                    }
                } else {
                    detector.missing(); valid = false;
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
                    e.trial.missing();
#endif
                }
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
                ++trace_index;
#endif
                valid = valid && !warmup && detector.ready();
                if (detector.touching()) mask |= bit;
                bit <<= 1;
            }
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
            ESP_LOGI("touch_trace", "t=%llu a=%lu b=%lu c=%lu old=%u trial=%u fault=%u",
                     static_cast<unsigned long long>(now),
                     static_cast<unsigned long>(trace_raw[0]), static_cast<unsigned long>(trace_raw[1]),
                     static_cast<unsigned long>(trace_raw[2]), static_cast<unsigned>(mask), trial_mask, fault_mask);
#endif
            if (valid) {
                const ContactSnapshot snapshot{mask != 0, static_cast<int64_t>(now * 1000)};
                xQueueOverwrite(contact_snapshot, &snapshot);
                const auto direction = petting.update(mask, now);
                if (direction != PettingDirection::none) {
                    const PettingEvent event{direction, snapshot.measured_us};
                    if (xQueueSend(petting_events, &event, 0) != pdTRUE)
                        ESP_LOGW(kTag, "Petting queue full; dropped newest event");
                    else ESP_LOGI(kTag, "elapsed_ms=%llu event=petting_detected direction=%s",
                                  static_cast<unsigned long long>(now),
                                  direction == PettingDirection::a_to_c ? "A->B->C" : "C->B->A");
                }
            } else petting.missing();
        } else {
            ++scan_failures;
            if (scan_failures == 1 || scan_end - last_scan_warning_us >= 2000000) {
                ESP_LOGW(kTag, "Scan failures=%u scan_us=%lld timeout_ms=%d", scan_failures, scan_us, kScanTimeoutMs);
                last_scan_warning_us = scan_end;
            }
            // 走査が失敗した場合、未完了チャンネルを区別できないため全電極を欠落扱いにする。
            for (auto &e : sensor.electrodes) {
                e.detector.missing();
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
                e.trial.missing();
#endif
            }
#if CONFIG_UTSUSHIMI_TOUCH_TRACE
            ESP_LOGI("touch_trace", "t=%llu a=0 b=0 c=0 old=0 trial=0 fault=7",
                     static_cast<unsigned long long>(scan_end / 1000));
#endif
            petting.missing();
        }
        // 遅延時に過去の測定を追いかけて連続実行しない。
        if (xTaskDelayUntil(&tick, pdMS_TO_TICKS(kPeriodMs)) == pdFALSE) tick = xTaskGetTickCount();
    }
}
}

void start_touch_diagnostics() {
    contact_snapshot = xQueueCreate(1, sizeof(ContactSnapshot));
    petting_events = xQueueCreate(8, sizeof(PettingEvent));
    if (!contact_snapshot || !petting_events) {
        ESP_LOGE(kTag, "Contact queue allocation failed; eyes/body continue without touch");
        if (contact_snapshot) vQueueDelete(contact_snapshot);
        if (petting_events) vQueueDelete(petting_events);
        contact_snapshot = petting_events = nullptr;
        return;
    }
    if (xTaskCreate(touch_task, "touch_raw", 4096, nullptr, kTouchPriority, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Task allocation failed; eyes/body continue without touch diagnostics");
        vQueueDelete(contact_snapshot);
        vQueueDelete(petting_events);
        contact_snapshot = petting_events = nullptr;
    }
}
bool touch_is_held() {
    ContactSnapshot snapshot{};
    if (!contact_snapshot || xQueuePeek(contact_snapshot, &snapshot, 0) != pdTRUE) return false;
    const int64_t age = esp_timer_get_time() - snapshot.measured_us;
    // 通信・測定が止まっても保持刺激を永久に継続しない。検出器の状態とは別の鮮度判定。
    return age >= 0 && age <= kContactStaleUs && snapshot.held;
}
bool take_petting_event(PettingEvent &event) {
    if (!petting_events) return false;
    // 固定長だけ処理し、入力が続いても身体タスクを占有しない。
    for (unsigned i = 0; i < 8; ++i) {
        if (xQueueReceive(petting_events, &event, 0) != pdTRUE) return false;
        const int64_t age = esp_timer_get_time() - event.measured_us;
        if (age >= 0 && age <= kContactStaleUs) return true;
        ESP_LOGW(kTag, "Discarded stale petting event");
    }
    return false;
}
} // namespace utsushimi
