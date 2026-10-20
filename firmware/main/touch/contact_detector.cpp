#include "contact_detector.h"
#include <algorithm>

namespace utsushimi {
void ContactDetector::missing() {
    candidate_ = false;
    have_time_ = false;
    samples_ = 0;
    sum_ = 0;
    minimum_ = UINT32_MAX;
    maximum_ = 0;
    // 測定欠落を離脱とは扱わず、確定状態と校正済み基準値を保つ。
}
ContactUpdate ContactDetector::update(uint32_t raw, uint64_t now) {
    if (have_time_ && (now < last_ms_ || now - last_ms_ > ContactSettings::max_gap_ms)) missing();
    have_time_ = true;
    last_ms_ = now;
    if (!ready_) {
        if (!samples_) calibration_start_ms_ = now;
        ++samples_; sum_ += raw;
        minimum_ = std::min(minimum_, raw); maximum_ = std::max(maximum_, raw);
        if (samples_ < ContactSettings::calibration_samples ||
            now - calibration_start_ms_ < ContactSettings::calibration_ms) return ContactUpdate::none;
        if (minimum_ == 0 || maximum_ - minimum_ > ContactSettings::calibration_range) {
            missing();
            return ContactUpdate::calibration_rejected;
        }
        baseline_ = static_cast<double>(sum_) / samples_;
        ready_ = true;
        return ContactUpdate::calibrated;
    }
    const bool crossing = touching_ ? delta(raw) <= ContactSettings::end_delta
                                    : delta(raw) >= ContactSettings::start_delta;
    if (!crossing) { candidate_ = false; return ContactUpdate::none; }
    if (!candidate_) { candidate_ = true; since_ms_ = now; }
    if (now - since_ms_ < ContactSettings::debounce_ms) return ContactUpdate::none;
    candidate_ = false;
    touching_ = !touching_;
    return touching_ ? ContactUpdate::touch_started : ContactUpdate::touch_ended;
}
}
