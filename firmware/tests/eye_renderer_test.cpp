#include "display/eye_renderer.h"
#include <cassert>
#include <fstream>
#include <limits>
#include <vector>
using namespace utsushimi;
int main(int argc, char **argv) {
    const EyeState normal{};
    EyeState closed{}; closed.openness = 0;
    EyeState changed{}; changed.gaze_x = 1; changed.pupil_size = 0.6f;
    EyeState invalid{}; invalid.pupil_size = std::numeric_limits<float>::quiet_NaN();
    int left = 0, right = 0, differences = 0;
    for (int y = 0; y < kFaceHeight; ++y) {
        for (int x = 0; x < kFaceWidth; ++x) {
            const auto pixel = eye_pixel(x, y, normal);
            assert(eye_pixel(x, y, closed) == 0);
            if (pixel) (x < 300 ? left : right)++;
            if (pixel != eye_pixel(x, y, changed)) ++differences;
            if (x == 0 || y == 0 || x == 599 || y == 449) assert(pixel == 0);
            (void)eye_pixel(x, y, invalid);
        }
    }
    assert(left > 10000 && right > 10000);
    assert(eye_pixel(155, 225, normal) == 0); // 左瞳
    assert(eye_pixel(445, 225, normal) == 0); // 右瞳
    assert(eye_pixel(155, 165, normal) == 0xffff); // 白目
    assert(eye_pixel(155, 135, normal) == 0); // 円の外側
    assert(differences > 1000);
    assert(eye_pixel(-1, 0, normal) == 0);
    assert(eye_pixel(600, 450, normal) == 0);
    // 高速フレーム生成を参照描画と照合。回転・バイト順・残像消去も検証。
    std::vector<uint16_t> frame(kFramePixels + 2, 0x1234);
    for (const EyeState state : {normal, closed, changed, invalid,
                                EyeState{0, 0, 0.5f, 0.4f}, EyeState{0, 0, 0.02f, 0.4f}}) {
        render_eye_frame(frame.data() + 1, state);
        assert(frame.front() == 0x1234 && frame.back() == 0x1234);
        for (int row = 0; row < kFrameHeight; ++row) {
            for (int col = 0; col < kFrameWidth; ++col) {
                const uint16_t p = eye_pixel(kFrameY + row, kFaceHeight - 1 - (kFrameX + col), state);
                const uint16_t wire = (p << 8) | (p >> 8);
                assert(frame[1 + row * kFrameWidth + col] == wire);
            }
        }
    }
    if (argc == 2) {
        std::ofstream out(argv[1], std::ios::binary);
        out << "P6\n600 450\n255\n";
        for (int y = 0; y < kFaceHeight; ++y) for (int x = 0; x < kFaceWidth; ++x) {
            const auto p = eye_pixel(x, y, normal);
            const char rgb[] = {static_cast<char>(((p >> 11) & 31) * 255 / 31),
                                static_cast<char>(((p >> 5) & 63) * 255 / 63),
                                static_cast<char>((p & 31) * 255 / 31)};
            out.write(rgb, 3);
        }
        assert(out.good());
    }
}
