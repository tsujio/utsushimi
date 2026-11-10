#include "task_load.h"
#include "sdkconfig.h"
#if CONFIG_UTSUSHIMI_TASK_LOAD_DIAGNOSTICS
#include <cstdint>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#error Task load diagnostics require the ESP timer run-time statistics clock
#endif
namespace utsushimi {
namespace {
struct Slot {
    const char *name;
    configRUN_TIME_COUNTER_TYPE previous = 0;
    bool primed = false;
};
Slot slots[] = {{"main"}, {"eyes_display"}, {"wake_word"}, {"audio_input"},
                {"audio_output"}, {"touch_raw"}, {"IDLE0"}, {"IDLE1"}};
int64_t last_sample = 0, last_log = 0;
bool search_seen = false;
}
void report_task_load(bool search_active) {
    search_seen |= search_active;
    const auto now = esp_timer_get_time();
    // Wait until the delayed audio startup has completed. All named tasks are
    // long-lived thereafter. Look up handles anew; never retain deleted handles.
    if (now < 7000000 || now - last_sample < 1000000) return;
    const auto window = now - last_sample;
    const bool log = search_seen || now - last_log >= 10000000;
    const auto sample_start = esp_timer_get_time();
    struct Sample {
        const char *name;
        uint64_t cpu_us;
        unsigned priority;
        int affinity;
    } samples[8];
    unsigned count = 0;
    for (auto &slot : slots) {
        const auto handle = xTaskGetHandle(slot.name);
        if (!handle) { slot.primed = false; continue; }
        TaskStatus_t status{};
        // Do not scan stacks or snapshot/suspend the entire task list.
        vTaskGetInfo(handle, &status, pdFALSE, eInvalid);
        const auto delta = static_cast<configRUN_TIME_COUNTER_TYPE>(status.ulRunTimeCounter - slot.previous);
        if (slot.primed && log) {
            const auto affinity = xTaskGetCoreID(handle);
            samples[count++] = {slot.name, static_cast<uint64_t>(delta),
                                unsigned(status.uxCurrentPriority), affinity == tskNO_AFFINITY ? -1 : int(affinity)};
        }
        slot.previous = status.ulRunTimeCounter;
        slot.primed = true;
    }
    const auto sample_us = esp_timer_get_time() - sample_start;
    if (log && count) {
        ESP_LOGI("task_load", "window_us=%lld search=%d sample_us=%lld affinity=-1 means any core",
                 (long long)window, search_seen, (long long)sample_us);
        for (unsigned i = 0; i < count; ++i) {
            const auto &s = samples[i];
            ESP_LOGI("task_load", "task=%s affinity=%d priority=%u cpu_us=%llu cpu_permille=%llu",
                     s.name, s.affinity, s.priority, (unsigned long long)s.cpu_us,
                     (unsigned long long)(s.cpu_us * 1000 / window));
        }
        last_log = now;
    }
    last_sample = now;
    search_seen = false;
}
}
#else
namespace utsushimi { void report_task_load(bool) {} }
#endif
