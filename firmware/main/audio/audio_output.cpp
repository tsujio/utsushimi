#include "audio_output.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include "tone.h"
#include "input_rms.h"
#include "diagnostics/runtime_progress.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace utsushimi {
namespace {
constexpr char tag[] = "audio";
constexpr size_t block_frames = 240; // 5ms per DMA descriptor.
struct Tone { float hz; uint32_t duration_ms; float volume; };
QueueHandle_t requests;
i2s_chan_handle_t tx;
i2s_chan_handle_t rx;
portMUX_TYPE rx_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t rx_overflows = 0;
bool IRAM_ATTR on_rx_overflow(i2s_chan_handle_t, i2s_event_data_t *, void *) {
    portENTER_CRITICAL_ISR(&rx_lock);
    ++rx_overflows;
    portEXIT_CRITICAL_ISR(&rx_lock);
    return false;
}
#if CONFIG_UTSUSHIMI_AUDIO_INPUT
constexpr bool input_enabled = true;
#else
constexpr bool input_enabled = false;
#endif
constexpr unsigned dma_descriptors = input_enabled ? 8 : 4;

void input_task(void *) {
    // Single reader owns this buffer; keep 1920 bytes off the task stack.
    static int32_t pcm[block_frames * 2];
    InputRms meter;
    int64_t last_error = -1000000;
    int64_t last_stack_report = 0;
    for (;;) {
        size_t bytes = 0;
        const auto err = i2s_channel_read(rx, pcm, sizeof(pcm), &bytes, 1000);
        portENTER_CRITICAL(&rx_lock);
        const auto dropped = rx_overflows;
        rx_overflows = 0;
        portEXIT_CRITICAL(&rx_lock);
        const auto now = esp_timer_get_time();
        if (now - last_stack_report >= 10000000) {
            ESP_LOGI("audio_input", "Stack minimum free: %u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            last_stack_report = now;
        }
        const bool invalid_read = err != ESP_OK || bytes == 0 || bytes % (2 * sizeof(int32_t)) != 0;
        if (dropped || invalid_read) {
            meter = {};
            if (now - last_error >= 1000000) {
                ESP_LOGE("audio_input", "Read failed: %s bytes=%u overflows=%lu", esp_err_to_name(err), unsigned(bytes),
                         static_cast<unsigned long>(dropped));
                last_error = now;
            }
            // A successful read with dropped DMA blocks must drain the queue
            // immediately. Sleeping here fills it again and prevents recovery.
            if (invalid_read) vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        for (size_t i = 0; i < bytes / sizeof(int32_t); i += 2) {
            meter.add(pcm[i]); // L/R=GND: left only; right slot is undriven.
            if (meter.count() == kAudioSampleRate / 10) {
                const double rms = meter.rms();
                ESP_LOGI("audio_input", "rms=%.6f dBFS=%.1f", rms,
                         rms > 0 ? 20 * std::log10(rms) : -INFINITY);
                meter = {};
            }
        }
    }
}

esp_err_t write_block(const int32_t *pcm) {
    size_t written = 0;
    const auto err = i2s_channel_write(tx, pcm, block_frames * 2 * sizeof(int32_t), &written, 1000);
    if (err != ESP_OK) return err;
    return written == block_frames * 2 * sizeof(int32_t) ? ESP_OK : ESP_FAIL;
}
#if CONFIG_UTSUSHIMI_AUDIO_TEST_PROGRESS
struct ProgressReport {
    uint32_t body = RuntimeProgress::body_updates.load();
    uint32_t requests = RuntimeProgress::display_requests.load();
    uint32_t completed = RuntimeProgress::display_completed.load();
    uint32_t scans = RuntimeProgress::touch_scans.load();
    int64_t time_us = esp_timer_get_time();

    void report(bool final = false) {
        const int64_t now = esp_timer_get_time();
        if (!final && now - time_us < 500000) return;
        const uint32_t next_body = RuntimeProgress::body_updates.load();
        const uint32_t next_requests = RuntimeProgress::display_requests.load();
        const uint32_t next_completed = RuntimeProgress::display_completed.load();
        const uint32_t next_scans = RuntimeProgress::touch_scans.load();
        ESP_LOGI(tag, "Progress: window_ms=%lu body_updates=%lu display_requests=%lu display_completed=%lu touch_scans=%lu",
                 static_cast<unsigned long>((now - time_us) / 1000),
                 static_cast<unsigned long>(next_body - body),
                 static_cast<unsigned long>(next_requests - requests),
                 static_cast<unsigned long>(next_completed - completed),
                 static_cast<unsigned long>(next_scans - scans));
        body = next_body;
        requests = next_requests;
        completed = next_completed;
        scans = next_scans;
        time_us = now;
    }
};
#else
struct ProgressReport {
    void report(bool = false) {}
};
#endif
void audio_task(void *) {
    Tone tone;
    // Single writer owns this buffer, including across blocking DMA writes.
    static int32_t pcm[block_frames * 2]{};
    for (;;) {
        if (xQueueReceive(requests, &tone, input_enabled ? 0 : portMAX_DELAY) != pdTRUE) {
            const auto err = write_block(pcm); // Continuous silence keeps the shared clocks running.
            if (err != ESP_OK) {
                ESP_LOGE(tag, "Silence failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }
        ESP_LOGI(tag, "Tone start: %.1fHz %lums PCM=%.3f BCLK=38 WS=39 DOUT=41 priority=%u",
                 static_cast<double>(tone.hz), static_cast<unsigned long>(tone.duration_ms),
                 static_cast<double>(tone.volume), static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
        ProgressReport progress;
        auto err = input_enabled ? ESP_OK : i2s_channel_enable(tx);
        if (err == ESP_OK) {
            // Silence allows the amplifier's startup time to elapse before the tone.
            for (int i = 0; i < 8 && err == ESP_OK; ++i) err = write_block(pcm);
            const uint32_t frames = kAudioSampleRate * tone.duration_ms / 1000;
            SineTone wave(tone.hz, frames, tone.volume);
            for (uint32_t base = 0; base < frames && err == ESP_OK; base += block_frames) {
                for (size_t i = 0; i < block_frames; ++i) {
                    const int32_t sample = wave.next();
                    pcm[2 * i] = pcm[2 * i + 1] = sample; // Same signal for every SD channel selection.
                }
                err = write_block(pcm);
                progress.report();
            }
            std::fill(std::begin(pcm), std::end(pcm), 0);
            // Drain the tone with more than a full DMA ring of zeroes.
            for (unsigned i = 0; i < dma_descriptors + 4 && err == ESP_OK; ++i) err = write_block(pcm);
            // write() queues data; it is not a hardware transmit completion barrier.
            vTaskDelay(pdMS_TO_TICKS(30));
            const auto stop_err = input_enabled ? ESP_OK : i2s_channel_disable(tx);
            if (err == ESP_OK) err = stop_err;
        }
        progress.report(true);
        if (err != ESP_OK) ESP_LOGE(tag, "Tone failed: %s", esp_err_to_name(err));
        else ESP_LOGI(tag, "Tone sent: %.1fHz %lums PCM=%.3f",
                      static_cast<double>(tone.hz), static_cast<unsigned long>(tone.duration_ms),
                      static_cast<double>(tone.volume));
    }
}
}
esp_err_t start_audio_output() {
    if (requests) return ESP_ERR_INVALID_STATE;
    i2s_chan_config_t config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    config.dma_desc_num = dma_descriptors;
    config.dma_frame_num = block_frames;
    config.auto_clear_after_cb = true;
    auto err = i2s_new_channel(&config, &tx, input_enabled ? &rx : nullptr);
    if (err != ESP_OK) return err;
    i2s_std_config_t standard{};
    standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRate);
    standard.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    standard.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    standard.gpio_cfg.bclk = GPIO_NUM_38;
    standard.gpio_cfg.ws = GPIO_NUM_39;
    standard.gpio_cfg.dout = GPIO_NUM_41;
    standard.gpio_cfg.din = input_enabled ? GPIO_NUM_40 : I2S_GPIO_UNUSED;
    err = i2s_channel_init_std_mode(tx, &standard);
    if (err == ESP_OK && input_enabled) err = i2s_channel_init_std_mode(rx, &standard);
    if (err == ESP_OK && input_enabled) {
        i2s_event_callbacks_t callbacks{};
        callbacks.on_recv_q_ovf = on_rx_overflow;
        err = i2s_channel_register_event_callback(rx, &callbacks, nullptr);
    }
    bool tx_running = false, rx_running = false;
    TaskHandle_t reader = nullptr;
    if (err == ESP_OK && input_enabled) {
        err = i2s_channel_enable(tx);
        tx_running = err == ESP_OK;
        if (err == ESP_OK) {
            err = i2s_channel_enable(rx);
            rx_running = err == ESP_OK;
        }
    }
    if (err == ESP_OK) {
        requests = xQueueCreate(1, sizeof(Tone));
        if (!requests) err = ESP_ERR_NO_MEM;
    }
    // Float formatting and log10 need stack beyond the read/compute loop.
    if (err == ESP_OK && input_enabled &&
        xTaskCreate(input_task, "audio_input", 8192, nullptr, tskIDLE_PRIORITY, &reader) != pdPASS)
        err = ESP_ERR_NO_MEM;
    // Body updates run at priority 1. Audio must never outrank that loop,
    // including when synthesis falls behind and DMA writes stop blocking.
    if (err == ESP_OK && xTaskCreate(audio_task, "audio_output", 4096, nullptr, tskIDLE_PRIORITY, nullptr) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        if (reader) vTaskDelete(reader);
        if (rx_running) i2s_channel_disable(rx);
        if (tx_running) i2s_channel_disable(tx);
        if (rx) i2s_del_channel(rx);
        rx = nullptr;
        if (requests) vQueueDelete(requests);
        requests = nullptr;
        i2s_del_channel(tx);
        tx = nullptr;
    }
    return err;
}
esp_err_t play_tone(float hz, uint32_t duration_ms, float volume) {
    if (!std::isfinite(hz) || hz < 20 || hz > 20000 || duration_ms < 1 || duration_ms > 2000 ||
        !std::isfinite(volume) || volume < 0 || volume > 0.1f) return ESP_ERR_INVALID_ARG;
    if (!requests) return ESP_ERR_INVALID_STATE;
    const Tone tone{hz, duration_ms, volume};
    return xQueueSend(requests, &tone, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}
}
