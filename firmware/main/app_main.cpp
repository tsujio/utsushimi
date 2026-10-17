#include <algorithm>
#include <cstring>
#include "board/amoled.h"
#include "behavior/blink.h"
#include "display/eye_renderer.h"
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
    static_assert(utsushimi::kFramePixels * sizeof(uint16_t) <= AMOLED_MAX_TRANSFER_BYTES);
    // DMAが直接参照できる内部RAM。描画完了後に一括転送し、完了まで変更しない。
    auto *frame = static_cast<uint16_t *>(heap_caps_malloc(
        utsushimi::kFramePixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!frame) ESP_LOGE("utsushimi", "Frame allocation failed; largest DMA block: %u bytes",
                         static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)));
    ESP_ERROR_CHECK(frame ? ESP_OK : ESP_ERR_NO_MEM);
    amoled_init();
    std::memset(frame, 0, AMOLED_WIDTH * AMOLED_STRIP_ROWS * sizeof(uint16_t));
    for (int y = 0; y < AMOLED_HEIGHT; y += AMOLED_STRIP_ROWS)
        amoled_write_rect(0, y, AMOLED_WIDTH, AMOLED_STRIP_ROWS, frame);

    bool visible = false;
    utsushimi::EyeState eyes;
    int64_t render_sum = 0, transfer_sum = 0, worst = 0, last_report = esp_timer_get_time();
    unsigned frames = 0;
    for (;;) {
        xQueueReceive(expressions, &eyes, portMAX_DELAY);
        const int64_t start = esp_timer_get_time();
        utsushimi::render_eye_frame(frame, eyes);
        const int64_t rendered = esp_timer_get_time();
        amoled_write_rect(utsushimi::kFrameX, utsushimi::kFrameY,
                          utsushimi::kFrameWidth, utsushimi::kFrameHeight, frame);
        const int64_t finished = esp_timer_get_time();
        render_sum += rendered - start;
        transfer_sum += finished - rendered;
        worst = std::max(worst, finished - start);
        ++frames;
        if (finished - last_report >= 2000000) {
            // アイドル時間を含むFPSではなく、1フレームの処理時間を計測。
            ESP_LOGI("eyes_perf", "frames=%u render_avg=%lldus transfer_avg=%lldus work_max=%lldus (budget=20000us)",
                     frames, static_cast<long long>(render_sum / frames),
                     static_cast<long long>(transfer_sum / frames), static_cast<long long>(worst));
            render_sum = transfer_sum = worst = 0;
            frames = 0;
            last_report = finished;
        }
        if (!visible) {
            amoled_show();
            visible = true;
            ESP_LOGI("utsushimi", "Eyes ready (logical 600x450), randomized blinking active.");
        }
        // 目安50Hz（RTOS tick単位に丸め）。転送が遅い場合は古い状態を溜めず次の最新状態を使う。
        const int64_t remaining = 20000 - (esp_timer_get_time() - start);
        if (remaining > 0) {
            const TickType_t pause = pdMS_TO_TICKS((remaining + 999) / 1000);
            if (pause > 0) vTaskDelay(pause);
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
    utsushimi::Blink blink(esp_timer_get_time() / 1000, esp_random());
    TickType_t tick = xTaskGetTickCount();
    float previous = 1;
    for (;;) {
        const float openness = blink.update(esp_timer_get_time() / 1000, esp_random());
        if (openness != previous) {
            eyes.openness = openness;
            // 古いフレームを溜めず最新状態のみ渡す。表示DMAの待機から独立。
            xQueueOverwrite(expressions, &eyes);
            previous = openness;
        }
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(10));
    }
}
