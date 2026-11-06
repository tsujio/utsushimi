#pragma once
#include <algorithm>
#include <cstdint>

namespace utsushimi {
constexpr uint32_t kAudioSampleRate = 48000;

// Quarter-wave lookup table with linear interpolation. No trigonometry or
// double-precision arithmetic in the streaming loop. Validated by play_tone().
class SineTone {
public:
    SineTone(float hz, uint32_t frames, float volume)
        : frames_(frames), amplitude_(volume * 2147483648.0f),
          step_(static_cast<uint32_t>(hz * (4294967296.0 / kAudioSampleRate))) {}

    int32_t next() {
        if (frames_ < 2 || frame_ >= frames_) return 0;
        const uint32_t index = phase_ >> 24;
        const float fraction = (phase_ & 0x00ffffffu) * (1.0f / 16777216.0f);
        const float first = sine(index);
        const float wave = first + (sine(index + 1) - first) * fraction;
        constexpr uint32_t ramp = kAudioSampleRate / 200;
        const float envelope = std::min(1.0f,
            static_cast<float>(std::min(frame_, frames_ - 1 - frame_)) / ramp);
        ++frame_;
        phase_ += step_; // Unsigned wrap keeps phase bounded, even for long tones.
        return static_cast<int32_t>(wave * envelope * amplitude_);
    }
private:
    static float sine(uint32_t index) {
        static constexpr float quarter[] = {
            0.000000000f, 0.024541229f, 0.049067674f, 0.073564564f,
            0.098017140f, 0.122410675f, 0.146730474f, 0.170961889f,
            0.195090322f, 0.219101240f, 0.242980180f, 0.266712757f,
            0.290284677f, 0.313681740f, 0.336889853f, 0.359895037f,
            0.382683432f, 0.405241314f, 0.427555093f, 0.449611330f,
            0.471396737f, 0.492898192f, 0.514102744f, 0.534997620f,
            0.555570233f, 0.575808191f, 0.595699304f, 0.615231591f,
            0.634393284f, 0.653172843f, 0.671558955f, 0.689540545f,
            0.707106781f, 0.724247083f, 0.740951125f, 0.757208847f,
            0.773010453f, 0.788346428f, 0.803207531f, 0.817584813f,
            0.831469612f, 0.844853565f, 0.857728610f, 0.870086991f,
            0.881921264f, 0.893224301f, 0.903989293f, 0.914209756f,
            0.923879533f, 0.932992799f, 0.941544065f, 0.949528181f,
            0.956940336f, 0.963776066f, 0.970031253f, 0.975702130f,
            0.980785280f, 0.985277642f, 0.989176510f, 0.992479535f,
            0.995184727f, 0.997290457f, 0.998795456f, 0.999698819f,
            1.000000000f
        };
        const uint32_t offset = index & 63u;
        const float value = quarter[(index & 64u) ? 64u - offset : offset];
        return (index & 128u) ? -value : value;
    }
    uint32_t frames_;
    float amplitude_;
    uint32_t step_;
    uint32_t phase_ = 0;
    uint32_t frame_ = 0;
};
}
