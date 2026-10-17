#pragma once
#include <cstdint>

namespace utsushimi {
// 時刻と乱数を入力するだけの非ブロッキングな瞬き状態機械。
class Blink {
public:
    Blink(uint64_t now_ms, uint32_t random);
    float update(uint64_t now_ms, uint32_t random);
private:
    static uint64_t interval(uint32_t random);
    uint64_t next_ms_;
    uint64_t start_ms_ = 0;
    bool active_ = false;
    bool double_ = false;
};
}  // namespace utsushimi
