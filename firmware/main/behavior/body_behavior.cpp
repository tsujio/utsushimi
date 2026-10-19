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
           unit(petting_sensitivity) && unit(holding_sensitivity) &&
           unit(squint_strength) && positive(expression_response_time);
}
void update_body(BodyState &s, const Stimulus &stimulus,
                 const BehaviorParameters &p, float dt) {
    const float drive = stimulus.holding ? p.holding_sensitivity : 0;
    const float blend = -std::expm1(-dt / p.comfort_decay_time);
    s.valence = std::clamp(s.valence + (drive * p.comfort_decay_time - s.valence) * blend, -1.0f, 1.0f);
}
void Expression::advance(float valence, float dt, const BehaviorParameters &p) {
    const float target = 1 - p.squint_strength * std::max(0.0f, valence);
    openness_ = std::clamp(openness_ + (target - openness_) * -std::expm1(-dt / p.expression_response_time), 0.0f, 1.0f);
}
BodyBehavior::BodyBehavior(RandomSource random, BehaviorParameters params)
    : params_(params.valid() ? params : BehaviorParameters{}), used_default_parameters_(!params.valid()),
      movement_(random, params_.blink_interval, params_.blink_duration) {}
void BodyBehavior::input(BodyEvent event) {
    switch (event) {
    case BodyEvent::touch_started: stimulus_.holding = true; break;
    case BodyEvent::touch_ended: stimulus_.holding = false; break;
    case BodyEvent::petting_detected:
        state_.valence = std::clamp(state_.valence + params_.petting_sensitivity, -1.0f, 1.0f);
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
    expression_.advance(state_.valence, kStep, params_);
    movement_.advance(now_us_);
}
} // namespace utsushimi
