#include "petting_detector.h"
namespace utsushimi {
void PettingDetector::missing() {
    have_time_ = releasing_ = armed_ = false;
    previous_mask_ = origin_ = 0;
    stage_ = 0;
}
PettingDirection PettingDetector::update(uint8_t mask, uint64_t now) {
    if (mask > 7) { missing(); return PettingDirection::none; }
    if (have_time_ && (now < previous_ms_ || now-previous_ms_ > PettingSettings::max_sample_gap_ms)) missing();
    previous_ms_ = now; have_time_ = true;
    const uint8_t rising = mask & ~previous_mask_;
    previous_mask_ = mask;
    // 成立・失敗後は全離脱が100ms続くまで再開しない。
    if (!armed_) {
        if (mask) releasing_ = false;
        else {
            if (!releasing_) { release_since_ = now; releasing_ = true; }
            if (now-release_since_ >= PettingSettings::release_ms) { armed_ = true; stage_ = 0; }
        }
        return PettingDirection::none;
    }
    auto cancel = [&]() { armed_ = releasing_ = false; stage_ = 0; };
    if (stage_ && (now-step_ms_ > PettingSettings::max_step_ms ||
                   now-start_ms_ > PettingSettings::max_total_ms)) {
        cancel(); return PettingDirection::none;
    }
    if (!rising) return PettingDirection::none;
    if (!stage_) {
        if (mask != 1 && mask != 4) { cancel(); return PettingDirection::none; }
        origin_ = mask; stage_ = 1; start_ms_ = step_ms_ = now;
        return PettingDirection::none;
    }
    const uint8_t target = stage_ == 1 ? 2 : (origin_ == 1 ? 4 : 1);
    if (rising != target || mask == 7 || now-step_ms_ < PettingSettings::min_step_ms ||
        (stage_ == 2 && (mask & origin_))) {
        cancel(); return PettingDirection::none;
    }
    step_ms_ = now;
    if (stage_ == 1) { stage_ = 2; return PettingDirection::none; }
    const auto direction = origin_ == 1 ? PettingDirection::a_to_c : PettingDirection::c_to_a;
    cancel();
    return direction;
}
}
