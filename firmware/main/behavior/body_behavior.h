#pragma once
#include <cstdint>
#include "blink.h"

namespace utsushimi {
// 時間は秒。9項目の初期値は身体反応仕様書に対応する。
struct BehaviorParameters {
    float blink_interval = 5;
    float blink_duration = 0.150f;
    float blink_recovery = 0.8f;
    float comfort_decay_time = 20;
    float petting_sensitivity = 0.35f;
    float holding_sensitivity = 0.02f;
    float squint_strength = 0.5f;
    float expression_response_time = 0.200f;
    float dryness_discomfort_strength = 0.1f;
    bool valid() const;
};
struct BodyState { float eye_dryness = 0; float valence = 0; };
enum class BodyEvent { touch_started, touch_ended, petting_detected };
struct Stimulus { bool holding = false; };
// 乱数源は呼び出し側が所有。nextは一様な32bit値を返すこと。
struct RandomSource { uint32_t (*next)(void *); void *context; };

void update_body(BodyState &state, const Stimulus &stimulus,
                 const BehaviorParameters &params, float dt);
float eye_moisture_need(float dryness);
float blink_probability(float need, float dt, const BehaviorParameters &params);
float update_expression(float openness, float valence, float dt,
                        const BehaviorParameters &params);

class BodyBehavior {
public:
    explicit BodyBehavior(RandomSource random, BehaviorParameters params = {});
    // 同一タスクから呼ぶ。イベントは現在のモデル時刻で適用する。
    void input(BodyEvent event);
    // 内部10ms刻み。1回100msまで処理し、それ以上の休止時間は捨てる。
    // trueなら時間を制限した。待機・動的確保はしない。
    bool advance(uint64_t elapsed_us);
    const BodyState &state() const { return state_; }
    const Stimulus &stimulus() const { return stimulus_; }
    const BehaviorParameters &parameters() const { return params_; }
    float expression_openness() const { return expression_; }
    float blink_openness() const { return blink_.openness(now_us_); }
    uint64_t now_us() const { return now_us_; }
    uint64_t action_count() const { return action_count_; }
    uint64_t double_count() const { return double_count_; }
    uint64_t last_start_us() const { return last_start_us_; }
    uint64_t refractory_until_us() const { return refractory_until_us_; }
private:
    void step();
    double uniform();
    RandomSource random_;
    BehaviorParameters params_;
    BodyState state_;
    Stimulus stimulus_;
    Blink blink_;
    float expression_ = 1;
    uint64_t pending_us_ = 0, now_us_ = 0, refractory_until_us_ = 0;
    uint64_t action_count_ = 0, double_count_ = 0, last_start_us_ = 0;
};
}  // namespace utsushimi
