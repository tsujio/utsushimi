#include "behavior/body_behavior.h"
#include "display/eye_renderer.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
using namespace utsushimi;
namespace {
uint32_t never(void *) { return UINT32_MAX; }
EyeState eyes(const BodyBehavior &body) {
    EyeState s;
    s.openness = body.blink_openness();
    s.upper_lid = s.lower_lid = 1 - body.expression_openness();
    return s;
}
void compare_frame(const EyeState &state) {
    std::vector<uint16_t> frame(kFramePixels + 2, 0x1234);
    render_eye_frame(frame.data() + 1, state);
    assert(frame.front() == 0x1234 && frame.back() == 0x1234);
    for (int row = 0; row < kFrameHeight; ++row)
        for (int col = 0; col < kFrameWidth; ++col) {
            const auto p = eye_pixel(kFaceWidth - 1 - (kFrameY + row), kFrameX + col, state);
            assert(frame[1 + row*kFrameWidth+col] == static_cast<uint16_t>((p << 8) | (p >> 8)));
        }
}
void geometry() {
    // 通常時は旧来の円と同じ。快は白目の部分集合で、瞳サイズ・視線に依らずクリップ。
    for (float q : {0.0f, 0.001f, 0.25f, 0.5f, 1.0f}) {
        EyeState s; s.upper_lid = s.lower_lid = q;
        for (float blink : {1.0f, 0.5f, 0.02f, 0.0f}) {
            s.openness = blink;
            compare_frame(s);
        }
        s.openness = 1;
        unsigned white = 0;
        for (int y = 138; y <= 312; ++y) for (int x = 68; x <= 242; ++x) {
            const auto pixel = eye_pixel(x,y,s);
            assert(pixel <= eye_pixel(x,y,{}));
            white += pixel != 0;
        }
        assert(white > 1000); // 最大値でも弧が消失・分断するほど潰れない。
        if (q >= 0.5f) {
            // 標準瞳の領域はまぶたに覆われ、上側の弧が残る。
            assert(eye_pixel(155, 225, s) == 0);
            assert(eye_pixel(155, 170, s) != 0);
        }
    }
    EyeState a; a.upper_lid = 0.24f; a.lower_lid = 0.24f;
    EyeState b = a; b.upper_lid += 0.001f; b.lower_lid += 0.001f;
    unsigned changes = 0;
    for (int y = 138; y <= 312; ++y) for (int x = 68; x <= 242; ++x)
        changes += eye_pixel(x,y,a) != eye_pixel(x,y,b);
    assert(changes > 0 && changes < 600); // 微小な変化で全体の画像を切り替えない。
    for (float gaze : {-1.0f, 1.0f}) {
        EyeState s; s.upper_lid = s.lower_lid = 0.5f;
        s.gaze_x = s.gaze_y = gaze; s.pupil_size = 0.6f;
        compare_frame(s);
        // 円の外、上下のまぶたで隠す領域は黒いまま。
        assert(eye_pixel(155, 225,s) == 0 && eye_pixel(155, 140,s) == 0);
    }
    // 最大快の中央から側方まで、上下の弧の間隔がほぼ一定になる。
    EyeState parallel; parallel.upper_lid = parallel.lower_lid = 0.5f;
    int min_thickness = 200, max_thickness = 0;
    for (int dx : {0, 20, 40, 60}) {
        int first = -1, last = -1;
        for (int y = 138; y <= 312; ++y) {
            if (eye_pixel(155 + dx, y, parallel)) {
                if (first < 0) first = y;
                last = y;
            }
        }
        assert(first >= 0);
        const int thickness = last - first + 1;
        min_thickness = std::min(min_thickness, thickness);
        max_thickness = std::max(max_thickness, thickness);
    }
    assert(max_thickness - min_thickness <= 1);
    // 両目の外側だけが上がる。上下の幅と鏡像対称を維持する。
    auto span = [&](int x) {
        int first = -1, last = -1;
        for (int y=138;y<=312;++y) if (eye_pixel(x,y,parallel)) {
            if(first<0) first=y;
            last=y;
        }
        return std::pair<int,int>{first,last};
    };
    const auto outer_left=span(95), inner_left=span(215);
    const auto outer_right=span(505), inner_right=span(385);
    assert(outer_left==outer_right && inner_left==inner_right);
    assert(inner_left.first-outer_left.first>=4 && inner_left.first-outer_left.first<=8);
    assert(std::abs((outer_left.second-outer_left.first)-(inner_left.second-inner_left.first))<=1);
    EyeState bad; bad.upper_lid = std::numeric_limits<float>::quiet_NaN();
    bad.lower_lid = std::numeric_limits<float>::infinity(); compare_frame(bad);
}
void hysteresis() {
    Expression e;
    e.advance(.299f, 1); assert(e.openness()==1);
    e.advance(.3f, 0); assert(e.state()==ExpressionState::comfortable && e.openness()==1);
    e.advance(.2f, .125f); assert(std::abs(e.openness()-.75f)<1e-6);
    e.advance(.2f, .125f); assert(e.openness()==.5f);
    for (int i=0;i<100;++i) e.advance(i%2 ? .299f : .301f,.01f);
    assert(e.openness()==.5f);
    e.advance(.151f, 1); assert(e.openness()==.5f);
    e.advance(.15f, 0); assert(e.state()==ExpressionState::normal && e.openness()==.5f);
    e.advance(.2f, .2f); assert(std::abs(e.openness()-.75f)<1e-6);
    e.advance(.2f, .2f); assert(e.openness()==1);
    for (int i=0;i<100;++i) e.advance(i%2 ? .149f : .151f,.01f);
    assert(e.openness()==1);
    // 両方向の途中反転で値が飛ばず、新しい目標まで全遷移時間を使う。
    e.advance(.35f,.1f);
    float before=e.openness();
    e.advance(.1f,0); assert(e.openness()==before);
    e.advance(.1f,.1f); assert(e.openness()>before && e.openness()<1);
    before=e.openness();
    e.advance(.35f,0); assert(e.openness()==before);
    e.advance(.35f,.24f); assert(e.openness()>.5f);
    e.advance(.35f,.01f); assert(e.openness()==.5f);
    BodyBehavior b({never,nullptr});
    b.input(BodyEvent::petting_detected);
    for(int i=0;i<24;++i) b.advance(10000);
    assert(b.expression_openness()>.5f);
    b.advance(10000); assert(b.expression_openness()==.5f);
    b.input(BodyEvent::petting_detected);
    for(int i=0;i<25;++i) b.advance(10000);
    assert(b.expression_openness()==.5f);
    while(b.state().valence>.15f) b.advance(10000);
    assert(b.expression_openness()>.5f && b.expression_openness()<1);
    for(int i=0;i<38;++i) b.advance(10000);
    assert(b.expression_openness()<1);
    b.advance(10000); assert(b.expression_openness()==1);
}
void animation() {
    // 入力がない通常動作では周期的な快は発生しない。
    BodyBehavior quiet({never, nullptr});
    for (int i = 0; i < 2000; ++i) {
        quiet.advance(10000);
        const auto s = eyes(quiet);
        assert(s.openness == 1 && s.upper_lid == 0 && s.lower_lid == 0);
    }
    {
        BehaviorParameters params;
        // 固定乱数0で実際の瞬き行動を発生させる。
        BodyBehavior body({[](void *) -> uint32_t { return 0; }, nullptr}, params);
        BodyBehavior reference({never, nullptr}, params);
        for (int i = 0; i < 3; ++i) {
            body.input(BodyEvent::petting_detected);
            reference.input(BodyEvent::petting_detected);
        }
        bool closed = false, reopened = false, waiting = false;
        for (int i = 0; i < 800; ++i) {
            body.advance(10000);
            reference.advance(10000);
            // 閉眼中も表情は独立して追従し、再開時はその時点の表情に戻る。
            assert(body.expression_openness() == reference.expression_openness());
            const auto s = eyes(body);
            assert(s.upper_lid >= 0 && s.upper_lid <= .5f);
            assert(s.upper_lid == s.lower_lid);
            if (s.openness == 0) {
                closed = true;
                waiting = true;
                for (int y = 138; y <= 312; ++y) for (int x = 68; x <= 242; ++x)
                    assert(eye_pixel(x,y,s) == 0);
            } else if (waiting && s.openness == 1) {
                reopened = true;
                waiting = false;
                assert(s.upper_lid > 0);
                assert(s.upper_lid == eyes(reference).upper_lid);
            }
        }
        assert(closed && reopened);
    }
    BodyBehavior decay({never, nullptr});
    decay.input(BodyEvent::petting_detected);
    for (int i = 0; i < 1000; ++i) decay.advance(10000);
    assert(eyes(decay).upper_lid > 0);
    for (int i = 0; i < 60000; ++i) decay.advance(10000);
    assert(eyes(decay).upper_lid < 0.001f);
}
}
int main() {
    hysteresis();
    geometry();
    animation();
}
