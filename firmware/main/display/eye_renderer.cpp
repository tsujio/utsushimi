#include "eye_renderer.h"
#include <algorithm>
#include <cmath>

namespace utsushimi {
namespace {
float bound(float x, float lo, float hi) {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : lo;
}
// 画像座標は下向き。上まぶたは少し下げ、下まぶたの中央を持ち上げる。
void lid_edges(float dx, bool left, float upper, float lower, float &top, float &bottom) {
    const float dx2 = dx * dx;
    const float scale = 1 - 0.4f * upper;
    const float curve = (87 - std::sqrt(std::max(0.0f, 87 * 87 - dx2))) * scale;
    // 同じ楕円弧を上下へ移動する。中央だけ薄く、両端が厚くなる形を避ける。
    top = -87 * scale + curve;
    bottom = 87 - lower * 3.0f * 86 + curve;
    // 目尻側だけをなだらかに持ち上げる。通常時・目頭・中央は動かさない。
    const float outward = std::clamp((left ? -dx : dx) / 87.0f, 0.0f, 1.0f);
    constexpr float kOuterLiftPixels = 8.0f; // 実機で印象を調整する描画値
    const float lift = kOuterLiftPixels * (lower / 0.5f) * outward * outward * (3 - 2 * outward);
    top -= lift;
    bottom -= lift;
}
float lid_coverage(float y, float top, float bottom) {
    return std::clamp(std::min(y - top + 0.5f, bottom - y + 0.5f), 0.0f, 1.0f);
}
uint16_t gray(float brightness) {
    const int v = static_cast<int>(255 * std::clamp(brightness, 0.0f, 1.0f));
    return ((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3);
}
}
uint16_t eye_pixel(int x, int y, const EyeState &s) {
    if (x < 0 || x >= kFaceWidth || y < 0 || y >= kFaceHeight) return 0;
    const float cx = x < kFaceWidth / 2 ? 155.0f : 445.0f;
    const float dx = x - cx;
    const float dy = y - 225.0f;
    const float open = bound(s.openness, 0, 1);
    if (open <= 0.005f || std::abs(dx) >= 87 || std::abs(dy) >= 87) return 0;
    // 瞬きでは白目を縦に閉じ、丸い瞳を白目の輪郭でクリップする。
    const float ey = dy / open;
    const float eye_distance = std::sqrt(dx * dx + ey * ey);
    float white = std::clamp(86.5f - eye_distance, 0.0f, 1.0f);
    float top, bottom;
    lid_edges(dx, x < kFaceWidth / 2, bound(s.upper_lid, 0, 0.5f), bound(s.lower_lid, 0, 0.5f), top, bottom);
    white = std::min(white, lid_coverage(ey, top, bottom));
    if (white == 0) return 0;
    // ほんの少し内向きの瞳で柔らかい表情にする。
    const float inset = x < kFaceWidth / 2 ? 5.0f : -5.0f;
    const float px = dx - inset - 20 * bound(s.gaze_x, -1, 1);
    const float py = dy + 3 - 20 * bound(s.gaze_y, -1, 1);
    const float pupil = 86 * bound(s.pupil_size, 0.2f, 0.6f);
    const float outside_pupil = std::clamp(std::sqrt(px * px + py * py) - pupil + 0.5f, 0.0f, 1.0f);
    return gray(white * outside_pupil);
}
void render_eye_frame(uint16_t *pixels, const EyeState &s) {
    std::fill_n(pixels, kFramePixels, uint16_t{0});
    const float open = bound(s.openness, 0, 1);
    if (open <= 0.005f) return;
    const float gaze_x = 20 * bound(s.gaze_x, -1, 1);
    const float gaze_y = 20 * bound(s.gaze_y, -1, 1);
    const float pupil = 86 * bound(s.pupil_size, 0.2f, 0.6f);
    const float pupil_inner = (pupil - 0.5f) * (pupil - 0.5f);
    const float pupil_outer = (pupil + 0.5f) * (pupil + 0.5f);
    const float upper = bound(s.upper_lid, 0, 0.5f), lower = bound(s.lower_lid, 0, 0.5f);
    const bool shaped = upper > 0 || lower > 0;
    float eye_y2[kFrameWidth], pupil_y2[kFrameWidth], eye_y[kFrameWidth];
    for (int col = 0; col < kFrameWidth; ++col) {
        const float dy = kFrameX + col - 225.0f;
        const float ey = dy / open;
        const float py = dy + 3 - gaze_y;
        if (shaped) eye_y[col] = ey;
        eye_y2[col] = ey * ey;
        pupil_y2[col] = py * py;
    }
    for (int row = 0; row < kFrameHeight; ++row) {
        const int x = kFaceWidth - 1 - (kFrameY + row);
        const bool left = x < kFaceWidth / 2;
        const float dx = x - (left ? 155.0f : 445.0f);
        if (std::abs(dx) >= 87) continue;
        const float dx2 = dx * dx;
        float top = -87, bottom = 87;
        if (shaped) lid_edges(dx, left, upper, lower, top, bottom);
        const float px = dx - (left ? 5.0f : -5.0f) - gaze_x;
        const float px2 = px * px;
        for (int col = 0; col < kFrameWidth; ++col) {
            const float eye_d2 = dx2 + eye_y2[col];
            if (eye_d2 >= 86.5f * 86.5f) continue;
            const float pupil_d2 = px2 + pupil_y2[col];
            if (pupil_d2 <= pupil_inner) continue;
            // 平方根は輪郭の1pxだけ。白目内部・黒背景は二乗距離で判定。
            float white = eye_d2 <= 85.5f * 85.5f ? 1 :
                std::clamp(86.5f - std::sqrt(eye_d2), 0.0f, 1.0f);
            if (shaped) white = std::min(white, lid_coverage(eye_y[col], top, bottom));
            const float outside = pupil_d2 >= pupil_outer ? 1 :
                std::clamp(std::sqrt(pupil_d2) - pupil + 0.5f, 0.0f, 1.0f);
            const uint16_t pixel = gray(white * outside);
            pixels[row * kFrameWidth + col] = (pixel << 8) | (pixel >> 8);
        }
    }
}
}  // namespace utsushimi
