#pragma once
#include <cstdint>
#include "automatic_blink.h"

namespace utsushimi {
// 時間は秒。7項目の初期値は身体反応仕様書に対応する。
struct BehaviorParameters {
    float blink_interval = 5;
    float blink_duration = 0.150f;
    float comfort_decay_time = 20;
    float petting_sensitivity = 0.35f;
    float holding_sensitivity = 0.02f;
    float squint_strength = 0.5f;
    float expression_response_time = 0.200f;
    bool valid() const;
};
struct BodyState { float valence = 0; };
enum class BodyEvent { touch_started, touch_ended, petting_detected };
struct Stimulus { bool holding = false; };
void update_body(BodyState &state, const Stimulus &stimulus,
                 const BehaviorParameters &params, float dt);

// Expressionはvalenceを継続的に追従する。入力イベントや瞬きを参照しない。
class Expression {
public:
    void advance(float valence, float dt, const BehaviorParameters &params);
    float openness() const { return openness_; }
private:
    float openness_ = 1;
};

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
    // 不正設定を既定値に戻したことを呼び出し側が検出できる。
    bool used_default_parameters() const { return used_default_parameters_; }
    float expression_openness() const { return expression_.openness(); }
    float blink_openness() const { return movement_.openness(now_us_); }
    uint64_t now_us() const { return now_us_; }
    uint64_t action_count() const { return movement_.action_count(); }
    uint64_t double_count() const { return movement_.double_count(); }
    uint64_t last_start_us() const { return movement_.last_start_us(); }
    uint64_t refractory_until_us() const { return movement_.refractory_until_us(); }
private:
    void step();
    BehaviorParameters params_;
    bool used_default_parameters_;
    BodyState state_;
    Stimulus stimulus_;
    Expression expression_;
    AutomaticBlink movement_;
    uint64_t pending_us_ = 0, now_us_ = 0;
};
} // namespace utsushimi
