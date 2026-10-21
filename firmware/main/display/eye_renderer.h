#pragma once
#include <cstdint>

namespace utsushimi {
constexpr int kFaceWidth = 600;
constexpr int kFaceHeight = 450;
struct EyeState {
    float gaze_x = 0.0f;       // -1..1。今回は正面で固定。
    float gaze_y = 0.0f;       // -1..1
    float openness = 1.0f;     // 瞬きの開眼率、0..1
    float pupil_size = 0.40f;  // 白目半径に対する比率、0.2..0.6
    float upper_lid = 0.0f;   // 快のまぶた形状。0で従来と画素一致。
    float lower_lid = 0.0f;   // 形状は0..0.5で連続変化、それ以上は飽和。
};
// ハードウェアに依存しないRGB565。白目は半径86の円、瞳は黒い円。
uint16_t eye_pixel(int x, int y, const EyeState &state);
// 両目を含むネイティブ座標の矩形。論理座標からネイティブ座標へ反時計回り90度回転する。
constexpr int kFrameX = 132, kFrameY = 62;
constexpr int kFrameWidth = 186, kFrameHeight = 476;
constexpr int kFramePixels = kFrameWidth * kFrameHeight;
// kFramePixels個のバッファへ、送信用ビッグエンディアンRGB565を生成する。
void render_eye_frame(uint16_t *pixels, const EyeState &state);
// ネイティブ座標の左右2矩形。偶数境界で輪郭全体を含む。
constexpr int kEyeTileSize = 174, kEyeTilePixels = kEyeTileSize * kEyeTileSize;
constexpr int kEyeTileX = 138;
constexpr int kEyeTileY[2] = {68, 358};
// 連続する2枚分の送信用バッファへ直接描画。閉眼時も全画素を黒で上書きする。
void render_eye_tiles(uint16_t *pixels, const EyeState &state);
}  // namespace utsushimi
