#include "body_behavior.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace utsushimi {
namespace {
constexpr float kStep = 0.01f;
// 初期パラメータの開始→開始間隔をホストシミュレーションで較正。
constexpr float kHazardGain = 2.03f;
float clamp(float x, float low, float high) { return std::clamp(x, low, high); }
bool positive(float x) { return std::isfinite(x) && x >= 0.01f && x <= 3600; }
bool unit(float x) { return std::isfinite(x) && x >= 0 && x <= 1; }
}
bool BehaviorParameters::valid() const {
    return positive(blink_interval) && positive(blink_duration) &&
           unit(blink_recovery) && positive(comfort_decay_time) &&
           unit(petting_sensitivity) && unit(holding_sensitivity) &&
           unit(squint_strength) && positive(expression_response_time) &&
           unit(dryness_discomfort_strength);
}
void update_body(BodyState &s, const Stimulus &stimulus,
                 const BehaviorParameters &p, float dt) {
    // 乾燥は厳密解。快不快は区間中点の乾燥を使う指数積分。
    const float kd = -std::log(0.4f) / p.blink_interval;
    const float midpoint = 1 - (1 - s.eye_dryness) * std::exp(-kd * dt * 0.5f);
    s.eye_dryness = clamp(1 - (1 - s.eye_dryness) * std::exp(-kd * dt), 0, 1);
    const float discomfort = std::max(0.0f, (midpoint - 0.8f) / 0.2f);
    const float drive = (stimulus.holding ? p.holding_sensitivity : 0) -
                        p.dryness_discomfort_strength * discomfort * discomfort;
    const float blend = -std::expm1(-dt / p.comfort_decay_time);
    s.valence = clamp(s.valence + (drive * p.comfort_decay_time - s.valence) * blend, -1, 1);
}
float eye_moisture_need(float d) {
    const float scaled = clamp((d - 0.3f) / 0.7f, 0, 1);
    return scaled * scaled;
}
float blink_probability(float need, float dt, const BehaviorParameters &p) {
    return -std::expm1(-kHazardGain * (5 / p.blink_interval) * clamp(need, 0, 1) *
                       std::max(0.0f, dt));
}
float update_expression(float openness, float valence, float dt,
                        const BehaviorParameters &p) {
    const float target = 1 - p.squint_strength * std::max(0.0f, valence);
    return clamp(openness + (target - openness) * -std::expm1(-dt / p.expression_response_time), 0, 1);
}
BodyBehavior::BodyBehavior(RandomSource random, BehaviorParameters params)
    : random_(random), params_(params.valid() ? params : BehaviorParameters{}) {
    assert(random_.next != nullptr);
}
double BodyBehavior::uniform() {
    return static_cast<double>(random_.next(random_.context)) / 4294967296.0;
}
void BodyBehavior::input(BodyEvent event) {
    switch (event) {
    case BodyEvent::touch_started: stimulus_.holding = true; break;
    case BodyEvent::touch_ended: stimulus_.holding = false; break;
    case BodyEvent::petting_detected:
        state_.valence = clamp(state_.valence + params_.petting_sensitivity, -1, 1);
        break;
    }
}
bool BodyBehavior::advance(uint64_t elapsed) {
    const bool limited = elapsed > 100000;
    pending_us_ += std::min<uint64_t>(elapsed, 100000);
    while (pending_us_ >= 10000) {
        pending_us_ -= 10000;
        step();
    }
    return limited;
}
void BodyBehavior::step() {
    now_us_ += 10000;
    update_body(state_, stimulus_, params_, kStep);
    if (now_us_ >= refractory_until_us_ && !blink_.active(now_us_)) {
        const float probability = blink_probability(eye_moisture_need(state_.eye_dryness), kStep, params_);
        if (uniform() < probability) {
            const bool twice = uniform() < 0.1;
            blink_.start(now_us_, static_cast<uint64_t>(std::llround(params_.blink_duration * 1000000)), twice);
            refractory_until_us_ = blink_.end_us() + 1000000;
            last_start_us_ = now_us_;
            ++action_count_;
            if (twice) ++double_count_;
            // 回復は行動開始時の1回のみ。二連内部では追加しない。
            state_.eye_dryness *= 1 - params_.blink_recovery;
        }
    }
    expression_ = update_expression(expression_, state_.valence, kStep, params_);
}
}  // namespace utsushimi
