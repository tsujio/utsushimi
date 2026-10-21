#include "body_behavior.h"
#include <algorithm>
#include <cmath>

namespace utsushimi {
namespace {
constexpr float kStep = 0.01f;
bool positive(float x) { return std::isfinite(x) && x >= 0.01f && x <= 3600; }
bool unit(float x) { return std::isfinite(x) && x >= 0 && x <= 1; }
}
bool BehaviorParameters::valid() const {
    return valid_blink_timing(blink_interval, blink_duration) && positive(comfort_decay_time) &&
           unit(petting_sensitivity) && unit(holding_sensitivity);
}
void update_body(BodyState &s, const Stimulus &stimulus,
                 const BehaviorParameters &p, float dt) {
    const float drive = stimulus.holding ? p.holding_sensitivity : 0;
    const float blend = -std::expm1(-dt / p.comfort_decay_time);
    s.valence = std::clamp(s.valence + (drive * p.comfort_decay_time - s.valence) * blend, -1.0f, 1.0f);
}
void Expression::advance(float valence, float dt) {
    if (!std::isfinite(valence) || !std::isfinite(dt) || dt < 0) return;
    auto next = state_;
    if (valence >= ExpressionSettings::enter_valence) next = ExpressionState::comfortable;
    else if (valence <= ExpressionSettings::exit_valence) next = ExpressionState::normal;
    if (next != state_) {
        state_ = next;
        start_openness_ = openness_;
        elapsed_ = 0;
    }
    const bool comfortable = state_ == ExpressionState::comfortable;
    const double duration = comfortable ? ExpressionSettings::enter_seconds : ExpressionSettings::exit_seconds;
    const float target = comfortable ? ExpressionSettings::comfortable_openness : ExpressionSettings::normal_openness;
    elapsed_ = std::min(duration, elapsed_ + dt);
    // floatの10ms入力の丸めを吸収して指定時間で目標に一致させる。
    if (elapsed_ + 1e-7 >= duration) { openness_ = target; return; }
    const double u = elapsed_ / duration;
    const double blend = u * u * (3 - 2 * u);
    openness_ = static_cast<float>(start_openness_ + (target - start_openness_) * blend);
}
BodyBehavior::BodyBehavior(RandomSource random, BehaviorParameters params)
    : params_(params.valid() ? params : BehaviorParameters{}), used_default_parameters_(!params.valid()),
      movement_(random, params_.blink_interval, params_.blink_duration) {}
void BodyBehavior::input(BodyEvent event) {
    switch (event) {
    case BodyEvent::touch_started:
        if (!stimulus_.holding && gaze_elapsed_us_ == 3500000) {
            gaze_elapsed_us_ = 0;
            gaze_return_start_ = -0.3f;
        }
        stimulus_.holding = true;
        break;
    case BodyEvent::touch_ended:
        // 上昇・保持中なら現在位置から復帰。復帰中の離脱は期限を延ばさない。
        if (gaze_elapsed_us_ < 3150000) {
            gaze_return_start_ = touch_gaze_offset();
            gaze_elapsed_us_ = 3150000;
        }
        stimulus_.holding = false;
        break;
    case BodyEvent::petting_detected:
        state_.valence = std::clamp(state_.valence + params_.petting_sensitivity, -1.0f, 1.0f);
        break;
    }
}
bool BodyBehavior::advance(uint64_t elapsed) {
    gaze_elapsed_us_ += std::min<uint64_t>(elapsed, 3500000 - gaze_elapsed_us_);
    const bool limited = elapsed > 100000;
    pending_us_ += std::min<uint64_t>(elapsed, 100000);
    while (pending_us_ >= 10000) {
        pending_us_ -= 10000;
        step();
    }
    return limited;
}
float BodyBehavior::touch_gaze_offset() const {
    const auto t = gaze_elapsed_us_;
    if (t >= 3500000) return 0;
    if (t >= 150000 && t < 3150000) return -0.3f;
    const float u = t < 150000 ? static_cast<float>(t) / 150000
                              : static_cast<float>(t - 3150000) / 350000;
    const float blend = u * u * (3 - 2 * u);
    return t < 150000 ? -0.3f * blend : gaze_return_start_ * (1 - blend);
}
void BodyBehavior::step() {
    now_us_ += 10000;
    update_body(state_, stimulus_, params_, kStep);
    expression_.advance(state_.valence, kStep);
    movement_.advance(now_us_);
}
} // namespace utsushimi
