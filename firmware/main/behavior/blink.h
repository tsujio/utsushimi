#pragma once
#include <cstdint>

namespace utsushimi {
// 選択済みの瞬きのアニメーション。身体状態・発生判定・乱数には依存しない。
class Blink {
public:
    void start(uint64_t now_us, uint64_t duration_us, bool double_blink);
    bool active(uint64_t now_us) const;
    float openness(uint64_t now_us) const;
    uint64_t end_us() const { return end_us_; }
private:
    uint64_t start_us_ = 0, end_us_ = 0, duration_us_ = 150000;
    bool double_ = false;
};
}  // namespace utsushimi
