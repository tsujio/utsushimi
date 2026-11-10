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
BodyBehavior::BodyBehavior(RandomSource random, BehaviorParameters params, SearchSettings search)
    : search_random_(random), search_settings_(search), params_(params.valid() ? params : BehaviorParameters{}), used_default_parameters_(!params.valid()),
      movement_(random, params_.blink_interval, params_.blink_duration) {
    if (!std::isfinite(search_settings_.amplitude)) search_settings_.amplitude = 0.8f;
    search_settings_.amplitude = std::clamp(search_settings_.amplitude, 0.0f, 1.0f);
    search_settings_.cooldown_us = std::max<uint64_t>(1000000, search_settings_.cooldown_us);
}
void BodyBehavior::input(BodyEvent event) {
    switch (event) {
    case BodyEvent::touch_started:
        if (searching()) {
            return_start_ = search_gaze_x();
            search_elapsed_us_ = 700000; // Yield to touch, smoothly return horizontally.
        }
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
    case BodyEvent::wake_word_detected:
        if (!stimulus_.holding && gaze_elapsed_us_ == 3500000 && !searching() && !cooldown_remaining_us_) {
            search_direction_ = search_random_.next(search_random_.context) & 1 ? 1.0f : -1.0f;
            return_start_ = -search_direction_ * search_settings_.amplitude;
            search_elapsed_us_ = 0;
            cooldown_remaining_us_ = search_settings_.cooldown_us;
            ++search_count_;
        }
        break;
    case BodyEvent::petting_detected:
        state_.valence = std::clamp(state_.valence + params_.petting_sensitivity, -1.0f, 1.0f);
        break;
    }
}
bool BodyBehavior::advance(uint64_t elapsed) {
    search_elapsed_us_ += std::min<uint64_t>(elapsed, 1000000 - search_elapsed_us_);
    cooldown_remaining_us_ -= std::min(elapsed, cooldown_remaining_us_);
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
float BodyBehavior::search_gaze_x() const {
    const auto t = search_elapsed_us_;
    if (t >= 1000000) return 0;
    const float side = search_direction_ * search_settings_.amplitude;
    const float u = t < 300000 ? float(t) / 300000 :
                    t < 700000 ? float(t - 300000) / 400000 : float(t - 700000) / 300000;
    const float blend = u * u * (3 - 2 * u);
    return t < 300000 ? side * blend : t < 700000 ? side * (1 - 2 * blend) : return_start_ * (1 - blend);
}
void BodyBehavior::step() {
    now_us_ += 10000;
    update_body(state_, stimulus_, params_, kStep);
    expression_.advance(state_.valence, kStep);
    movement_.advance(now_us_);
}
} // namespace utsushimi
