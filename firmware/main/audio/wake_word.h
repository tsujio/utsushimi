#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
namespace utsushimi {
struct WakeWordEvent {
    int64_t audio_end_us; // RX observation time of last input block, monotonic boot clock.
    int64_t detected_us;
};
// Start once, before I2S reader. No hardware ownership. Disabled builds are no-ops.
esp_err_t start_wake_word();
void submit_wake_audio(const int32_t *stereo, size_t frames, int64_t end_us);
void wake_audio_gap();
bool take_wake_word_event(WakeWordEvent &event);
}
