#pragma once
#include <cstdint>
#include "sdkconfig.h"
#if CONFIG_UTSUSHIMI_AUDIO_TEST_PROGRESS
#include "freertos/FreeRTOS.h"
#endif

namespace utsushimi {
// Counters protect only a single integer load/increment. Never do logging or
// driver calls inside these short critical sections. Observations are diagnostic
// and are not a coherent snapshot or control inputs.
#if CONFIG_UTSUSHIMI_AUDIO_TEST_PROGRESS
class ProgressCounter {
public:
    void increment() {
        portENTER_CRITICAL(&mux_);
        ++value_;
        portEXIT_CRITICAL(&mux_);
    }
    uint32_t load() {
        portENTER_CRITICAL(&mux_);
        const auto value = value_;
        portEXIT_CRITICAL(&mux_);
        return value;
    }
private:
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t value_ = 0;
};
#else
class ProgressCounter {
public:
    void increment() {}
    uint32_t load() { return 0; }
};
#endif
struct RuntimeProgress {
    inline static ProgressCounter body_updates;
    inline static ProgressCounter display_requests;
    inline static ProgressCounter display_completed;
    inline static ProgressCounter touch_scans;
};
}
