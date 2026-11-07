#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace utsushimi {
// INMP441: signed 24-bit sample in bits 31..8. Ignore undriven low bits.
class InputRms {
public:
    static int32_t decode(int32_t word) {
        const uint32_t bits = static_cast<uint32_t>(word) >> 8;
        return static_cast<int32_t>(bits & 0x7fffffu) -
               static_cast<int32_t>(bits & 0x800000u);
    }
    void add(int32_t word) {
        const double sample = decode(word) / 8388608.0;
        // Welford's central moment avoids cancellation with large DC offsets.
        const double delta = sample - mean_;
        mean_ += delta / ++count_;
        m2_ += delta * (sample - mean_);
    }
    uint32_t count() const { return count_; }
    double rms() const { return count_ ? std::sqrt(std::max(0.0, m2_ / count_)) : 0; }
private:
    uint32_t count_ = 0;
    double mean_ = 0;
    double m2_ = 0;
};
}
