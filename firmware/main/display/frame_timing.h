#pragma once
#include <cstdint>

namespace utsushimi {
inline constexpr int64_t kDisplayFps = 30;
static_assert(kDisplayFps > 0 && kDisplayFps <= 1000000);
inline constexpr int64_t kDisplayFrameBudgetUs = 1000000 / kDisplayFps;

// 起点からの絶対時刻で端数を保持し、過ぎた表示枠は再生しない。
// now_us >= epoch_us。返す時刻は常にnow_usより後。
inline int64_t next_display_frame_us(int64_t epoch_us, int64_t now_us) {
    const int64_t slot = (now_us - epoch_us) * kDisplayFps / 1000000 + 1;
    return epoch_us + (slot * 1000000 + kDisplayFps - 1) / kDisplayFps;
}
} // namespace utsushimi
