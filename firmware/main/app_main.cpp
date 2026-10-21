#include <algorithm>
#include <cstring>
#include "board/amoled.h"
#include "behavior/body_behavior.h"
#include "display/eye_renderer.h"
#include "display/frame_timing.h"
#include "touch/touch_sensor.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {
QueueHandle_t expressions;
void display_task(void *) {
    static_assert(utsushimi::kEyeTilePixels * sizeof(uint16_t) <= AMOLED_MAX_TRANSFER_BYTES);
    // 2枚を連続した内部DMAバッファへ直接描画し、転送完了まで変更しない。
    auto *frame = static_cast<uint16_t *>(heap_caps_malloc(
        2 * utsushimi::kEyeTilePixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!frame) ESP_LOGE("utsushimi", "Frame allocation failed; largest DMA block: %u bytes",
                         static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)));
    ESP_ERROR_CHECK(frame ? ESP_OK : ESP_ERR_NO_MEM);
    static_assert(AMOLED_WIDTH * AMOLED_STRIP_ROWS <= 2 * utsushimi::kEyeTilePixels);
    amoled_init();
    std::memset(frame, 0, AMOLED_WIDTH * AMOLED_STRIP_ROWS * sizeof(uint16_t));
    for (int y = 0; y < AMOLED_HEIGHT; y += AMOLED_STRIP_ROWS)
        amoled_write_rect(0, y, AMOLED_WIDTH, AMOLED_STRIP_ROWS, frame);

    bool visible = false;
    utsushimi::EyeState eyes;
    int64_t render_sum = 0, transfer_sum = 0, worst = 0, last_report = esp_timer_get_time();
    unsigned frames = 0;
    uint64_t transferred_pixels = 0;
    const int64_t frame_epoch = esp_timer_get_time();
    for (;;) {
        xQueueReceive(expressions, &eyes, portMAX_DELAY);
        const int64_t start = esp_timer_get_time();
        utsushimi::render_eye_tiles(frame, eyes);
        const int64_t rendered = esp_timer_get_time();
        for (int eye = 0; eye < 2; ++eye) {
            amoled_write_rect(utsushimi::kEyeTileX, utsushimi::kEyeTileY[eye],
                              utsushimi::kEyeTileSize, utsushimi::kEyeTileSize,
                              frame + eye * utsushimi::kEyeTilePixels);
            transferred_pixels += utsushimi::kEyeTilePixels;
        }
        const int64_t finished = esp_timer_get_time();
        render_sum += rendered - start;
        transfer_sum += finished - rendered;
        worst = std::max(worst, finished - start);
        ++frames;
        if (finished - last_report >= 2000000) {
            // アイドル時間を含むFPSではなく、1フレームの処理時間を計測。
            ESP_LOGI("eyes_perf", "frames=%u render_avg=%lldus transfer_avg=%lldus work_max=%lldus pixels_avg=%llu (budget=%lldus)",
                     frames, static_cast<long long>(render_sum / frames),
                     static_cast<long long>(transfer_sum / frames), static_cast<long long>(worst),
                     static_cast<unsigned long long>(transferred_pixels / frames),
                     static_cast<long long>(utsushimi::kDisplayFrameBudgetUs));
            render_sum = transfer_sum = worst = 0;
            frames = 0;
            transferred_pixels = 0;
            last_report = finished;
        }
        if (!visible) {
            amoled_show();
            visible = true;
            ESP_LOGI("utsushimi", "Eyes ready (logical 600x450), randomized blinking active.");
        }
        // 目標30Hz。絶対期限により端数・tick丸めの累積を防ぎ、遅れた表示枠は飛ばす。
        const int64_t deadline = utsushimi::next_display_frame_us(frame_epoch, esp_timer_get_time());
        for (;;) {
            const int64_t remaining = deadline - esp_timer_get_time();
            if (remaining <= 0) break;
            // tick境界で早く起床した場合も時刻を再確認する。ビジーウェイトしない。
            const auto pause = static_cast<TickType_t>(
                (remaining * configTICK_RATE_HZ + 999999) / 1000000);
            vTaskDelay(pause);
        }
    }
}
}
extern "C" void app_main(void) {
    uint32_t flash_bytes = 0;
    ESP_ERROR_CHECK(esp_flash_get_size(nullptr, &flash_bytes));
    ESP_LOGI("utsushimi", "Flash: %lu bytes, PSRAM: %u bytes",
             static_cast<unsigned long>(flash_bytes), static_cast<unsigned>(esp_psram_get_size()));
    expressions = xQueueCreate(1, sizeof(utsushimi::EyeState));
    ESP_ERROR_CHECK(expressions ? ESP_OK : ESP_ERR_NO_MEM);
    utsushimi::EyeState eyes{};
    xQueueOverwrite(expressions, &eyes);
    ESP_ERROR_CHECK(xTaskCreate(display_task, "eyes_display", 4096, nullptr, 4, nullptr) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
    utsushimi::start_touch_diagnostics();
    // 5つの調整項目はここで上書きする。単位は秒。
    const utsushimi::BehaviorParameters parameters{};
    ESP_ERROR_CHECK(parameters.valid() ? ESP_OK : ESP_ERR_INVALID_ARG);
    utsushimi::BodyBehavior body({[](void *) { return esp_random(); }, nullptr}, parameters);
    int64_t last_update = esp_timer_get_time();
    TickType_t tick = xTaskGetTickCount();
    utsushimi::EyeState previous;
    uint64_t previous_actions = 0;
    bool holding = false;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        if (body.advance(static_cast<uint64_t>(now - last_update)))
            ESP_LOGW("body", "Update delayed; discarded elapsed time beyond 100ms");
        last_update = now;
        if (body.action_count() != previous_actions) {
            ESP_LOGI("body", "actions=%llu doubles=%llu model_start_us=%llu valence=%.3f",
                     static_cast<unsigned long long>(body.action_count()),
                     static_cast<unsigned long long>(body.double_count()),
                     static_cast<unsigned long long>(body.last_start_us()),
                     static_cast<double>(body.state().valence));
            previous_actions = body.action_count();
        }
        const bool contact = utsushimi::touch_is_held();
        if (contact != holding) {
            body.input(contact ? utsushimi::BodyEvent::touch_started : utsushimi::BodyEvent::touch_ended);
            holding = contact;
            ESP_LOGI("body", "touch holding=%d valence=%.3f expression_openness=%.3f",
                     holding, static_cast<double>(body.state().valence),
                     static_cast<double>(body.expression_openness()));
        }
        utsushimi::PettingEvent petting;
        for (unsigned i = 0; i < 8 && utsushimi::take_petting_event(petting); ++i) {
            body.input(utsushimi::BodyEvent::petting_detected);
            ESP_LOGI("body", "petting_detected direction=%s valence=%.3f",
                     petting.direction == utsushimi::PettingDirection::a_to_c ? "A->B->C" : "C->B->A",
                     static_cast<double>(body.state().valence));
        }
        utsushimi::EyeState pose = eyes;
        pose.gaze_y = body.touch_gaze_offset() * utsushimi::kEyeRadius * pose.pupil_size / utsushimi::kGazePixels;
        pose.openness = body.blink_openness();
        pose.upper_lid = pose.lower_lid = 1 - body.expression_openness();
        if (pose.openness != previous.openness || pose.upper_lid != previous.upper_lid ||
            pose.lower_lid != previous.lower_lid || pose.gaze_y != previous.gaze_y) {
            eyes.gaze_y = pose.gaze_y;
            eyes.openness = pose.openness;
            eyes.upper_lid = pose.upper_lid;
            eyes.lower_lid = pose.lower_lid;
            // 古いフレームを溜めず最新状態のみ渡す。表示DMAの待機から独立。
            xQueueOverwrite(expressions, &eyes);
            previous = pose;
        }
        vTaskDelayUntil(&tick, std::max<TickType_t>(1, pdMS_TO_TICKS(10)));
    }
}
