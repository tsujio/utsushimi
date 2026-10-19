#include "automatic_blink.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace utsushimi {
namespace {
constexpr double kDoubleProbability = 0.1;
constexpr double kGammaThreeHalves = 0.886226925452758; // Gamma(1 + 1/2)
constexpr uint64_t kRefractoryUs = 1000000;
uint64_t duration_us(float duration) {
    return static_cast<uint64_t>(std::llround(static_cast<double>(duration) * 1000000));
}
}
double expected_blink_duration(float duration) {
    const double single = duration_us(duration) / 1e6;
    return (1 - kDoubleProbability) * single + kDoubleProbability * (2 * single + 0.1);
}
bool valid_blink_timing(float interval, float duration) {
    if (!std::isfinite(interval) || !std::isfinite(duration) ||
        interval < 0.01f || interval > 3600 || duration < 0.01f || duration > 3600) return false;
    return interval > expected_blink_duration(duration) + kRefractoryUs / 1e6;
}
double blink_wait_scale(float interval, float duration) {
    assert(valid_blink_timing(interval, duration));
    return (interval - expected_blink_duration(duration) - kRefractoryUs / 1e6) / kGammaThreeHalves;
}
double weibull_blink_probability(double t, double dt, double scale) {
    assert(t >= 0 && scale > 0 && std::isfinite(scale));
    if (dt <= 0) return 0;
    // ((t+dt)^2-t^2)/scale^2を展開し、大きいtでの桁落ちを避ける。
    return -std::expm1(-(2 * t + dt) * dt / (scale * scale));
}
AutomaticBlink::AutomaticBlink(RandomSource random, float interval, float duration)
    : random_(random), scale_(blink_wait_scale(interval, duration)), duration_us_(duration_us(duration)) {
    assert(random_.next != nullptr);
}
double AutomaticBlink::uniform() {
    return static_cast<double>(random_.next(random_.context)) / 4294967296.0;
}
void AutomaticBlink::advance(uint64_t now) {
    assert(now >= previous_us_);
    const uint64_t begin = std::max(previous_us_, refractory_until_us_);
    previous_us_ = now;
    if (now <= begin || animation_.active(now)) return;
    // 不応期の終了をまたぐ更新では、終了後の区間だけを判定する。
    const double t = (begin - refractory_until_us_) / 1e6;
    const double dt = (now - begin) / 1e6;
    if (uniform() < weibull_blink_probability(t, dt, scale_)) {
        const bool twice = uniform() < kDoubleProbability;
        animation_.start(now, duration_us_, twice);
        refractory_until_us_ = animation_.end_us() + kRefractoryUs;
        last_start_us_ = now;
        ++action_count_;
        if (twice) ++double_count_;
    }
}
} // namespace utsushimi
