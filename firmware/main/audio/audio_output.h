#pragma once
#include <cstdint>
#include "esp_err.h"

namespace utsushimi {
// Call start once before submitting requests. Pins are a provisional Rev2.0 wiring.
esp_err_t start_audio_output();
// Nonblocking, copies the request. Queue full returns ESP_ERR_TIMEOUT.
// hz: 20..20000, duration: 1..2000ms, volume: PCM peak fraction 0..0.1.
esp_err_t play_tone(float hz = 440.0f, uint32_t duration_ms = 200, float volume = 0.01f);
}
