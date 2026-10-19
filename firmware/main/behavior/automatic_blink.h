#pragma once
#include <cstdint>
#include "blink.h"

namespace utsushimi {
// コールバックとcontextは呼び出し側が所有。一様な32bit値を返すこと。
struct RandomSource { uint32_t (*next)(void *); void *context; };

bool valid_blink_timing(float interval, float duration);
double expected_blink_duration(float duration);
double blink_wait_scale(float interval, float duration);
// k=2の条件付き発生確率。tとdtとscaleの単位は秒。
double weibull_blink_probability(double t, double dt, double scale);

// Step 1のAutomatic Movement。身体状態・表情を参照しない。
class AutomaticBlink {
public:
    // 有効な時間設定と非nullの乱数コールバックが必要。
    AutomaticBlink(RandomSource random, float interval, float duration);
    // 共通モデル時計の単調な時刻を受け取る。呼び出し元は10ms更新を行う。
    void advance(uint64_t now_us);
    float openness(uint64_t now_us) const { return animation_.openness(now_us); }
    uint64_t action_count() const { return action_count_; }
    uint64_t double_count() const { return double_count_; }
    uint64_t last_start_us() const { return last_start_us_; }
    uint64_t refractory_until_us() const { return refractory_until_us_; }
private:
    double uniform();
    RandomSource random_;
    Blink animation_;
    double scale_;
    uint64_t duration_us_, previous_us_ = 0, refractory_until_us_ = 0;
    uint64_t action_count_ = 0, double_count_ = 0, last_start_us_ = 0;
};
} // namespace utsushimi
