#include "blink.h"
#include <algorithm>

namespace utsushimi {
namespace {
float smooth(float t) { return t * t * (3 - 2 * t); }
}
void Blink::start(uint64_t now, uint64_t duration, bool twice) {
    start_us_ = now;
    duration_us_ = std::max<uint64_t>(duration, 1);
    double_ = twice;
    end_us_ = now + duration_us_ * (twice ? 2 : 1) + (twice ? 100000 : 0);
}
bool Blink::active(uint64_t now) const { return now >= start_us_ && now < end_us_; }
float Blink::openness(uint64_t now) const {
    if (!active(now)) return 1;
    uint64_t elapsed = now - start_us_;
    if (double_ && elapsed >= duration_us_ + 100000) elapsed -= duration_us_ + 100000;
    if (elapsed >= duration_us_) return 1; // 二連内部の開眼した100ms。
    const float phase = static_cast<float>(elapsed) / duration_us_;
    // 150msの内訳：閉45ms、保持30ms、開75ms。
    if (phase < 0.3f) return 1 - smooth(phase / 0.3f);
    if (phase < 0.5f) return 0;
    return smooth((phase - 0.5f) / 0.5f);
}
}  // namespace utsushimi
