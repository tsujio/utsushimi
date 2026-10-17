#include "blink.h"

namespace utsushimi {
namespace {
float smooth(float t) { return t * t * (3 - 2 * t); }
float envelope(uint64_t elapsed) {
    if (elapsed < 70) return 1 - smooth(static_cast<float>(elapsed) / 70);
    if (elapsed < 110) return 0;
    if (elapsed < 260) return smooth(static_cast<float>(elapsed - 110) / 150);
    return 1;
}
}
uint64_t Blink::interval(uint32_t random) { return 2000 + random % 4501; }
Blink::Blink(uint64_t now, uint32_t random) : next_ms_(now + interval(random)) {}
float Blink::update(uint64_t now, uint32_t random) {
    if (!active_) {
        if (now < next_ms_) return 1;
        active_ = true;
        start_ms_ = now;
        double_ = random % 4 == 0; // 約25%で二連。
    }
    const uint64_t elapsed = now - start_ms_;
    const uint64_t duration = double_ ? 620 : 260;
    if (elapsed >= duration) {
        active_ = false;
        next_ms_ = now + interval(random);
        return 1;
    }
    // 二連では一度開いて100ms置いてから、もう一度閉じる。
    return envelope(double_ && elapsed >= 360 ? elapsed - 360 : elapsed);
}
}  // namespace utsushimi
