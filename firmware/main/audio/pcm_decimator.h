#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace utsushimi {
// 48kHz -> 16kHz, 127-tap Blackman-windowed sinc, cutoff 6.5kHz.
// Unity DC gain. Group delay 63 input samples (1.3125ms).
// Reset discards FIR warmup; never synthesize a frame across an input gap.
class PcmDecimator {
public:
    void reset() { *this = {}; }
    bool add(int16_t sample, int16_t &out) {
        history_[position_] = history_[position_ + 127] = sample;
        position_ = (position_ + 1) % taps.size();
        if (warmup_ < taps.size()) ++warmup_;
        phase_ = (phase_ + 1) % 3;
        if (phase_ || warmup_ < taps.size()) return false;
        float sum = 0;
        // Two contiguous mirrored copies avoid a wrap branch per tap.
        // Symmetric taps need only 64 multiplications per output sample.
        const auto *samples = history_.data() + position_;
        for (unsigned i = 0; i < 63; ++i)
            sum += taps[i] * (int(samples[i]) + int(samples[126-i]));
        sum += taps[63] * samples[63];
        out = static_cast<int16_t>(std::clamp(std::lround(sum), -32768L, 32767L));
        return true;
    }
private:
    inline static constexpr std::array<float, 127> taps = {
        1.3679450132e-20f,
        7.0009895750e-07f,
        4.6780812396e-06f,
        7.6167728822e-06f,
        -1.2815277147e-06f,
        -2.4901084584e-05f,
        -4.5529655387e-05f,
        -3.2505483136e-05f,
        2.8129734495e-05f,
        1.0562312147e-04f,
        1.3085423931e-04f,
        4.7294537602e-05f,
        -1.2520572359e-04f,
        -2.7204388427e-04f,
        -2.4845117759e-04f,
        2.0283670891e-18f,
        3.5076706347e-04f,
        5.4342760149e-04f,
        3.5547142017e-04f,
        -1.9219960002e-04f,
        -7.6897775105e-04f,
        -9.1025406998e-04f,
        -3.6234373277e-04f,
        6.4228064133e-04f,
        1.4303801418e-03f,
        1.3090969316e-03f,
        1.2171375568e-04f,
        -1.4796680512e-03f,
        -2.3413960035e-03f,
        -1.5975246731e-03f,
        5.7118193655e-04f,
        2.8224286516e-03f,
        3.4280599130e-03f,
        1.5358910729e-03f,
        -1.9644272719e-03f,
        -4.7399022424e-03f,
        -4.5011304547e-03f,
        -7.7847820331e-04f,
        4.3274509817e-03f,
        7.2154251387e-03f,
        5.2260361370e-03f,
        -1.1357842628e-03f,
        -7.9400065015e-03f,
        -1.0121319749e-02f,
        -5.0860220650e-03f,
        4.8315230360e-03f,
        1.3136484174e-02f,
        1.3216894233e-02f,
        3.2809479393e-03f,
        -1.1307033867e-02f,
        -2.0529529811e-02f,
        -1.6174798085e-02f,
        1.6714359838e-03f,
        2.2791560335e-02f,
        3.1925969487e-02f,
        1.8633139061e-02f,
        -1.3902541415e-02f,
        -4.7243576357e-02f,
        -5.5658044655e-02f,
        -2.0262741096e-02f,
        5.8409538472e-02f,
        1.5715212292e-01f,
        2.3907586334e-01f,
        2.7083526301e-01f,
        2.3907586334e-01f,
        1.5715212292e-01f,
        5.8409538472e-02f,
        -2.0262741096e-02f,
        -5.5658044655e-02f,
        -4.7243576357e-02f,
        -1.3902541415e-02f,
        1.8633139061e-02f,
        3.1925969487e-02f,
        2.2791560335e-02f,
        1.6714359838e-03f,
        -1.6174798085e-02f,
        -2.0529529811e-02f,
        -1.1307033867e-02f,
        3.2809479393e-03f,
        1.3216894233e-02f,
        1.3136484174e-02f,
        4.8315230360e-03f,
        -5.0860220650e-03f,
        -1.0121319749e-02f,
        -7.9400065015e-03f,
        -1.1357842628e-03f,
        5.2260361370e-03f,
        7.2154251387e-03f,
        4.3274509817e-03f,
        -7.7847820331e-04f,
        -4.5011304547e-03f,
        -4.7399022424e-03f,
        -1.9644272719e-03f,
        1.5358910729e-03f,
        3.4280599130e-03f,
        2.8224286516e-03f,
        5.7118193655e-04f,
        -1.5975246731e-03f,
        -2.3413960035e-03f,
        -1.4796680512e-03f,
        1.2171375568e-04f,
        1.3090969316e-03f,
        1.4303801418e-03f,
        6.4228064133e-04f,
        -3.6234373277e-04f,
        -9.1025406998e-04f,
        -7.6897775105e-04f,
        -1.9219960002e-04f,
        3.5547142017e-04f,
        5.4342760149e-04f,
        3.5076706347e-04f,
        2.0283670891e-18f,
        -2.4845117759e-04f,
        -2.7204388427e-04f,
        -1.2520572359e-04f,
        4.7294537602e-05f,
        1.3085423931e-04f,
        1.0562312147e-04f,
        2.8129734495e-05f,
        -3.2505483136e-05f,
        -4.5529655387e-05f,
        -2.4901084584e-05f,
        -1.2815277147e-06f,
        7.6167728822e-06f,
        4.6780812396e-06f,
        7.0009895750e-07f,
        1.3679450132e-20f
    };
    std::array<int16_t, 254> history_{};
    unsigned position_ = 0, phase_ = 0, warmup_ = 0;
};
// Producer sequence advances even when a queue send fails. Epoch also
// invalidates the first block after a DMA error. Caller cleans model + FIR.
struct AudioContinuity {
    bool accept(uint32_t sequence, uint32_t epoch, int64_t end_us, int64_t now_us) {
        const bool contiguous = initialized_ && sequence == next_ && epoch == epoch_;
        next_ = sequence + 1; epoch_ = epoch; initialized_ = true;
        return contiguous && end_us <= now_us && now_us - end_us <= 250000;
    }
private:
    bool initialized_ = false;
    uint32_t next_ = 0, epoch_ = 0;
};
// Caller owns the fixed frame; this class never allocates. A false begin()
// requires WakeNet clean() and discards both partial PCM and FIR history.
class WakePcmInput {
public:
    WakePcmInput(int16_t *frame, unsigned size) : frame_(frame), size_(size) {}
    bool begin(uint32_t sequence, uint32_t epoch, int64_t end_us, int64_t now_us) {
        if (continuity_.accept(sequence, epoch, end_us, now_us)) return true;
        reset();
        return false;
    }
    void reset() { decimator_.reset(); used_ = 0; }
    bool add(int16_t sample) {
        int16_t output;
        if (!decimator_.add(sample, output)) return false;
        frame_[used_++] = output;
        if (used_ < size_) return false;
        used_ = 0;
        return true;
    }
private:
    AudioContinuity continuity_;
    PcmDecimator decimator_;
    int16_t *frame_;
    unsigned size_, used_ = 0;
};

}
