#include "wake_word.h"
#include "sdkconfig.h"
#if CONFIG_UTSUSHIMI_WAKE_WORD
#include <algorithm>
#include <cstring>
#include "input_rms.h"
#include "pcm_decimator.h"
#include "wake_result.h"
#include "esp_wn_models.h"
#include "dl_model_base.hpp"
#include "model_path.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
namespace utsushimi {
namespace {
// ESP-SR 2.6.1's prebuilt S3 WakeNetX allocates 144 bytes for dl::Model.
// A newer ESP-DL layout can link successfully yet overwrite the next heap block.
static_assert(configNUM_CORES > 1, "WakeNet/render CPU separation requires dual-core FreeRTOS");
static_assert(sizeof(dl::Model) == 144, "ESP-SR 2.6.1 WakeNet ABI requires a 144-byte dl::Model");
static_assert(WAKENET_CHANNEL_VERIFIED == -1 && WAKENET_NO_DETECT == 0 && WAKENET_DETECTED == 1,
              "Review WakeNet result policy after an API change");
constexpr char tag[] = "wake_word";
struct Block { uint32_t sequence, epoch; int64_t end_us; size_t count; int16_t pcm[240]; };
QueueHandle_t audio_queue, event_queue;
// Only RX producer writes these after startup.
uint32_t sequence = 0, epoch = 0, queue_drops = 0;
// ESP-SR's loader trusts the packed header. Reject erased/unflashed or
// mismatched model headers before handing them to the upstream parser.
bool model_present() {
    const auto *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");
    uint8_t header[120];
    if (!part || esp_partition_read(part, 0, header, sizeof(header)) != ESP_OK) return false;
    const auto le32 = [&header](unsigned at) {
        return uint32_t(header[at]) | uint32_t(header[at+1]) << 8 |
               uint32_t(header[at+2]) << 16 | uint32_t(header[at+3]) << 24;
    };
    if (le32(0) != 1 || std::memcmp(header+4, "wn10_ja_airisu", sizeof("wn10_ja_airisu")) || le32(36) != 2)
        return false;
    bool data = false, info = false;
    for (unsigned at : {40u, 80u}) {
        const auto offset = le32(at+32), size = le32(at+36);
        if (offset < sizeof(header) || offset > part->size || !size || size > part->size-offset) return false;
        if (!std::memcmp(header+at, "wn10_data", sizeof("wn10_data"))) data = true;
        else if (!std::memcmp(header+at, "_MODEL_INFO_", sizeof("_MODEL_INFO_")) && size < 1024) info = true;
        else return false;
    }
    return data && info;
}
void worker(void *) {
    const auto internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const auto psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    auto *models = model_present() ? esp_srmodel_init("model") : nullptr;
    char *name = models ? esp_srmodel_filter(models, "wn10_ja_airisu", nullptr) : nullptr;
    const auto *api = name && std::strcmp(name, "wn10_ja_airisu") == 0 ? esp_wn_handle_from_name(name) : nullptr;
    auto *model = api ? api->create(name, DET_MODE_90) : nullptr;
    const int chunk = model ? api->get_samp_chunksize(model) : 0;
    auto *frame = chunk > 0 && chunk <= 4096 ? static_cast<int16_t *>(heap_caps_malloc(
        chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) : nullptr;
    if (!model || !frame || api->get_samp_rate(model) != 16000 || api->get_channel_num(model) != 1) {
        ESP_LOGE(tag, "Model load/PCM contract failed (requires flashed wn10_ja_airisu only, 16kHz mono, chunk<=4096)");
        if (frame) heap_caps_free(frame);
        if (model) api->destroy(model);
        if (models) esp_srmodel_deinit(models);
        // Drain without recognizing: keep RX nonblocking and avoid repeated errors.
        Block discard;
        for (;;) xQueueReceive(audio_queue, &discard, portMAX_DELAY);
    }
    ESP_LOGI(tag, "Loaded %s mode=90 rate=16000 channels=1 chunk=%d internal_delta=%ld psram_delta=%ld",
             name, chunk, long(internal_before) - long(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             long(psram_before) - long(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    WakePcmInput stream(frame, chunk);
    Block block;
    int64_t last_report = 0, worst_detect = 0, worst_age = 0, convert_us = 0;
    uint32_t frames = 0, converted_samples = 0;
    uint32_t resets = 0;
    int64_t last_error = -1000000, last_event_us = -int64_t(CONFIG_UTSUSHIMI_WAKE_COOLDOWN_MS) * 1000;
    for (;;) {
        xQueueReceive(audio_queue, &block, portMAX_DELAY);
        const auto now = esp_timer_get_time();
        if (!stream.begin(block.sequence, block.epoch, block.end_us, now)) {
            api->clean(model); ++resets;
            if (now - last_error >= 1000000) {
                ESP_LOGW(tag, "Audio discontinuity/stale block: resets=%lu sequence=%lu epoch=%lu age_us=%lld stack_free=%u",
                         (unsigned long)resets, (unsigned long)block.sequence, (unsigned long)block.epoch,
                         (long long)(now - block.end_us), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
                last_error = now;
            }
            // Reject the discontinuous/stale block as well as any partial frame.
            continue;
        }
        worst_age = std::max(worst_age, now - block.end_us);
        auto convert_start = esp_timer_get_time();
        for (size_t i = 0; i < block.count; ++i) {
            if (!stream.add(block.pcm[i])) continue;
            convert_us += esp_timer_get_time() - convert_start;
            ++frames;
            const auto start = esp_timer_get_time();
            const int result = api->detect(model, frame);
            const auto detected = esp_timer_get_time();
            worst_detect = std::max(worst_detect, detected - start);
            if (classify_wake_result(result) == WakeResult::detected && detected - last_event_us >= int64_t(CONFIG_UTSUSHIMI_WAKE_COOLDOWN_MS) * 1000) {
                last_event_us = detected;
                WakeWordEvent event{block.end_us, detected};
                ESP_LOGI(tag, "wake_word_detected audio_end_us=%lld detected_us=%lld index=%d",
                         (long long)event.audio_end_us, (long long)event.detected_us, result);
                xQueueOverwrite(event_queue, &event);
            } else if (classify_wake_result(result) == WakeResult::error) {
                if (detected - last_error >= 1000000) {
                    ESP_LOGE(tag, "Unexpected detect result=%d; resetting stream", result);
                    last_error = detected;
                }
                api->clean(model); stream.reset();
            }
            convert_start = esp_timer_get_time();
        }
        convert_us += esp_timer_get_time() - convert_start;
        converted_samples += block.count;
        if (now - last_report >= 10000000) {
            ESP_LOGI(tag, "Health core=%d converted_samples=%lu frames=%lu convert_us=%lld resets=%lu queued=%u age_max_us=%lld detect_max_us=%lld internal_free=%u psram_free=%u internal_min=%u stack_free=%u",
                     xPortGetCoreID(), (unsigned long)converted_samples, (unsigned long)frames,
                     (long long)convert_us, (unsigned long)resets, unsigned(uxQueueMessagesWaiting(audio_queue)),
                     (long long)worst_age, (long long)worst_detect,
                     unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                     unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                     unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            last_report = now; worst_detect = worst_age = convert_us = 0; frames = converted_samples = 0;
        }
    }
}
}
esp_err_t start_wake_word() {
    if (audio_queue) return ESP_ERR_INVALID_STATE;
    audio_queue = xQueueCreate(32, sizeof(Block)); // 160ms, bounded; zero wait on RX.
    event_queue = xQueueCreate(1, sizeof(WakeWordEvent));
    // Recognition keeps its low priority, but no longer shares a core with rendering.
    if (!audio_queue || !event_queue ||
        xTaskCreatePinnedToCore(worker, "wake_word", 8192, nullptr, 0, nullptr, 1) != pdPASS) {
        if (audio_queue) vQueueDelete(audio_queue);
        if (event_queue) vQueueDelete(event_queue);
        audio_queue = event_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void wake_audio_gap() { ++epoch; }
void submit_wake_audio(const int32_t *stereo, size_t frames, int64_t end_us) {
    if (!audio_queue) return;
    if (frames > 240 || frames == 0) { wake_audio_gap(); return; }
    Block block{};
    block.sequence = sequence++; block.epoch = epoch; block.end_us = end_us; block.count = frames;
    for (size_t i = 0; i < frames; ++i)
        block.pcm[i] = static_cast<int16_t>(InputRms::decode(stereo[2*i]) / 256);
    if (xQueueSend(audio_queue, &block, 0) != pdTRUE) {
        ++queue_drops;
        static int64_t last_report = -1000000;
        if (end_us - last_report >= 1000000) {
            ESP_LOGW(tag, "Audio queue overflow drops=%lu", (unsigned long)queue_drops);
            last_report = end_us;
        }
    }
}
bool take_wake_word_event(WakeWordEvent &event) {
    return event_queue && xQueueReceive(event_queue, &event, 0) == pdTRUE;
}
}
#else
namespace utsushimi {
esp_err_t start_wake_word() { return ESP_OK; }
void submit_wake_audio(const int32_t *, size_t, int64_t) {}
void wake_audio_gap() {}
bool take_wake_word_event(WakeWordEvent &) { return false; }
}
#endif
