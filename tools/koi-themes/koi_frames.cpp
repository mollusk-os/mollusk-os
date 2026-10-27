// Mollusk OS - koi pond backgrounds for RetroFE's <animatedImage> (koi theme drafts).
//
// Renders seamless 240x240 loops of a koi pond, writes them as headerless RGB565
// little-endian frames (the format the animatedImage component maps) and as PNG
// frames for previews.
//
// The koi are drawn by Glass Wings' koi renderer (windhawk/scenes/koi_pond.h, MIT),
// included unchanged. Its own steering is not used: every koi here follows a fixed
// swim lane, so the loop repeats exactly. A lane is a path from out of sight to out
// of sight (or a closed loop). It carries `clones` identical koi spaced evenly along
// it; in one loop each moves on to where the next one started, so a lane can be
// `clones` times longer than one loop's worth of swimming and the koi stay calm.
// Everything else that moves (tail beat, fins, caustics, ripples, lanterns,
// fireflies, shimmer) turns a whole number of times per loop.
//
// Vector layers (koi, lily pads, ripples, lanterns) are drawn with Direct2D at 3x
// and box-filtered down; water, shadows, blur, light and the glass are done per
// pixel on the CPU at 240x240.
//
// Usage:
//   koi_frames.exe render <lily|glass|night> <outdir> [--frames N]
//       outdir\anim.rgb565, outdir\frames\fNNN.png, outdir\loopcheck.txt
//   koi_frames.exe icons <devicesdir> <outdir> <iconRRGGBB> <haloRRGGBB> <haloAlpha> <size> <fileName>
//       menu icons: the stock device silhouettes recolored, with a soft halo
//   koi_frames.exe contrast <framesdir> <overlay.png> <boxes.txt> <out.txt>
//       text contrast over every frame (see Contrast below)
//   koi_frames.exe catalog <out.png>
//       the koi looks used by the drafts, posed large (for picking)

#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "koi_pond.h"

namespace {

using gwscene::KoiFish;
using gwscene::KoiVec;
using gwscene::operator+;
using gwscene::operator-;
using gwscene::operator*;

constexpr int W = 240;   // screen size
constexpr int SS = 3;    // supersampling of the vector layers
constexpr int WS = W * SS;
constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

IWICImagingFactory* g_wic = nullptr;
ID2D1Factory* g_d2d = nullptr;

template <class T> void Release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// ----------------------------------------------------------------------------
// Colors and images
// ----------------------------------------------------------------------------

struct C3 {
    float r, g, b;
};
C3 Hex(unsigned v) { return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f}; }
C3 operator+(C3 a, C3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
C3 operator-(C3 a, C3 b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
C3 operator*(C3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
C3 operator*(C3 a, C3 b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
C3 Mix(C3 a, C3 b, float t) { return a + (b - a) * t; }
float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float Smooth(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
float Frac(float x) { return x - floorf(x); }
D2D1_COLOR_F DC(C3 c, float a = 1.0f) { return {c.r, c.g, c.b, a}; }
const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES* RadialProps(D2D1_POINT_2F c, D2D1_POINT_2F o, float rx, float ry) {
    static D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES p;
    p = {c, o, rx, ry};
    return &p;
}
const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES* LinearProps(D2D1_POINT_2F a, D2D1_POINT_2F b) {
    static D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES p;
    p = {a, b};
    return &p;
}
D2D1::Matrix3x2F Mx(const D2D1_MATRIX_3X2_F& m) { return D2D1::Matrix3x2F(m._11, m._12, m._21, m._22, m._31, m._32); }

// RGBA, premultiplied, 0..1.
struct Img {
    int w = 0, h = 0;
    std::vector<float> p;
    Img() = default;
    Img(int W_, int H_) : w(W_), h(H_), p(static_cast<size_t>(W_) * H_ * 4, 0.0f) {}
    float* at(int x, int y) { return &p[(static_cast<size_t>(y) * w + x) * 4]; }
    const float* at(int x, int y) const { return &p[(static_cast<size_t>(y) * w + x) * 4]; }
};

// RGB, 0..1 (the opaque scene).
struct Rgb {
    int w = W, h = W;
    std::vector<C3> p;
    Rgb() : p(static_cast<size_t>(W) * W, C3{0, 0, 0}) {}
    C3& at(int x, int y) { return p[static_cast<size_t>(y) * w + x]; }
    const C3& at(int x, int y) const { return p[static_cast<size_t>(y) * w + x]; }
};

// Single channel 0..1.
struct Mask {
    int w = W, h = W;
    std::vector<float> v;
    Mask() : v(static_cast<size_t>(W) * W, 0.0f) {}
    float& at(int x, int y) { return v[static_cast<size_t>(y) * w + x]; }
    float at(int x, int y) const { return v[static_cast<size_t>(y) * w + x]; }
    // Bilinear sample with zero outside.
    float sample(float x, float y) const {
        const int x0 = static_cast<int>(floorf(x)), y0 = static_cast<int>(floorf(y));
        const float fx = x - x0, fy = y - y0;
        auto g = [&](int X, int Y) { return (X < 0 || Y < 0 || X >= w || Y >= h) ? 0.0f : at(X, Y); };
        return (g(x0, y0) * (1 - fx) + g(x0 + 1, y0) * fx) * (1 - fy) + (g(x0, y0 + 1) * (1 - fx) + g(x0 + 1, y0 + 1) * fx) * fy;
    }
};

Mask AlphaOf(const Img& im) {
    Mask m;
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) m.at(x, y) = im.at(x, y)[3];
    return m;
}

// Separable Gaussian blur.
std::vector<float> Kernel(float sigma) {
    const int r = std::max(1, static_cast<int>(ceilf(sigma * 3.0f)));
    std::vector<float> k(2 * r + 1);
    float s = 0;
    for (int i = -r; i <= r; ++i) s += (k[i + r] = expf(-0.5f * i * i / (sigma * sigma)));
    for (float& v : k) v /= s;
    return k;
}
void BlurMask(Mask& m, float sigma) {
    if (sigma <= 0.05f) return;
    const std::vector<float> k = Kernel(sigma);
    const int r = static_cast<int>(k.size() / 2);
    std::vector<float> tmp(m.v.size());
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) {
                const int xx = std::clamp(x + i, 0, m.w - 1);
                s += m.at(xx, y) * k[i + r];
            }
            tmp[static_cast<size_t>(y) * m.w + x] = s;
        }
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) {
                const int yy = std::clamp(y + i, 0, m.h - 1);
                s += tmp[static_cast<size_t>(yy) * m.w + x] * k[i + r];
            }
            m.at(x, y) = s;
        }
}
void BlurRgb(Rgb& im, float sigma) {
    if (sigma <= 0.05f) return;
    const std::vector<float> k = Kernel(sigma);
    const int r = static_cast<int>(k.size() / 2);
    std::vector<C3> tmp(im.p.size());
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            C3 s{0, 0, 0};
            for (int i = -r; i <= r; ++i) s = s + im.at(std::clamp(x + i, 0, W - 1), y) * k[i + r];
            tmp[static_cast<size_t>(y) * W + x] = s;
        }
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            C3 s{0, 0, 0};
            for (int i = -r; i <= r; ++i) s = s + tmp[static_cast<size_t>(std::clamp(y + i, 0, W - 1)) * W + x] * k[i + r];
            im.at(x, y) = s;
        }
}

// Premultiplied layer over the scene.
void Over(Rgb& dst, const Img& src, float opacity = 1.0f) {
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            const float* s = src.at(x, y);
            const float a = s[3] * opacity;
            if (a <= 0.0f) continue;
            C3& d = dst.at(x, y);
            d = C3{s[0] * opacity, s[1] * opacity, s[2] * opacity} + d * (1.0f - a);
        }
}

// Darkens the scene toward `color` where the mask is set (a soft shadow), the mask
// read `dx, dy` px away.
void Shade(Rgb& dst, const Mask& m, float dx, float dy, C3 color, float strength) {
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            const float a = m.sample(x - dx, y - dy) * strength;
            if (a <= 0.0005f) continue;
            C3& d = dst.at(x, y);
            d = Mix(d, color, Clamp01(a));
        }
}

// ----------------------------------------------------------------------------
// Noise and caustics
// ----------------------------------------------------------------------------

float Hash2(int x, int y, unsigned seed) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777215.0f;
}
float ValueNoise(float x, float y, unsigned seed) {
    const int x0 = static_cast<int>(floorf(x)), y0 = static_cast<int>(floorf(y));
    const float fx = x - x0, fy = y - y0;
    const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const float a = Hash2(x0, y0, seed), b = Hash2(x0 + 1, y0, seed);
    const float c = Hash2(x0, y0 + 1, seed), d = Hash2(x0 + 1, y0 + 1, seed);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}
float Fbm(float x, float y, unsigned seed, int octaves = 4) {
    float s = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        s += amp * ValueNoise(x, y, seed + i * 17u);
        norm += amp;
        x = x * 2.03f + 7.1f;
        y = y * 2.03f + 3.7f;
        amp *= 0.5f;
    }
    return s / norm;
}

// Tileable water caustic (the AnimatedDemo's function, tools/make-caustics-animation.py):
// brightness 0..1 at (px, py) in [0, tau), t in [0, tau) turns whole numbers of times.
float Caustic(float px, float py, float t) {
    static const int kTurns[4] = {-1, 1, -1, 1};
    constexpr float kIntensity = 0.005f;
    const float p_x = px - 250.0f, p_y = py - 250.0f;
    float i_x = p_x, i_y = p_y, c = 1.0f;
    for (int turns : kTurns) {
        const float tt = t * turns;
        const float nx = p_x + cosf(tt - i_x) + sinf(tt + i_y);
        const float ny = p_y + sinf(tt - i_y) + cosf(tt + i_x);
        i_x = nx;
        i_y = ny;
        float s = sinf(i_x + tt), k = cosf(i_y + tt);
        if (fabsf(s) < 1e-9f) s = 1e-9f;
        if (fabsf(k) < 1e-9f) k = 1e-9f;
        c += 1.0f / hypotf(p_x * kIntensity / s, p_y * kIntensity / k);
    }
    c /= 4.0f;
    c = 1.17f - powf(c, 1.4f);
    return std::min(powf(fabsf(c), 8.0f), 1.0f);
}

// ----------------------------------------------------------------------------
// Direct2D target (WS x WS, drawn, then box-filtered to W x W)
// ----------------------------------------------------------------------------

struct Target {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    void* bits = nullptr;
    ID2D1DCRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1RadialGradientBrush* sheen = nullptr;
    int size = WS;

    bool Create(int px = WS) {
        size = px;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = px;
        bi.bmiHeader.biHeight = -px;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bmp) return false;
        old = SelectObject(dc, bmp);
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
            96.0f);
        if (FAILED(g_d2d->CreateDCRenderTarget(&props, &rt))) return false;
        const RECT rc = {0, 0, px, px};
        if (FAILED(rt->BindDC(dc, &rc))) return false;
        const D2D1_COLOR_F white = {1, 1, 1, 1};
        rt->CreateSolidColorBrush(&white, nullptr, &brush);
        // The koi's gloss, as Glass Wings' AcquireRtCache makes it.
        const D2D1_GRADIENT_STOP stops[3] = {
            {0.0f, {1.0f, 1.0f, 1.0f, 0.6f}}, {0.55f, {1.0f, 1.0f, 1.0f, 0.2f}}, {1.0f, {1.0f, 1.0f, 1.0f, 0.0f}}};
        ID2D1GradientStopCollection* coll = nullptr;
        if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) && coll) {
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {{0.0f, 0.0f}, {0.0f, 0.0f}, 1.0f, 1.0f};
            rt->CreateRadialGradientBrush(&rp, nullptr, coll, &sheen);
            coll->Release();
        }
        return brush && sheen;
    }
    ~Target() {
        Release(sheen);
        Release(brush);
        Release(rt);
        if (dc && old) SelectObject(dc, old);
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
    }
    // scaled: draw in 240 px units; otherwise in WS px units.
    void Begin(bool scaled) {
        rt->BeginDraw();
        rt->SetTransform(scaled ? D2D1::Matrix3x2F::Scale(static_cast<float>(SS), static_cast<float>(SS))
                                : D2D1::Matrix3x2F::Identity());
        rt->Clear(D2D1::ColorF(0, 0.0f));
        brush->SetOpacity(1.0f);
    }
    Img End() {
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        const HRESULT hr = rt->EndDraw();
        if (FAILED(hr)) printf("  ! EndDraw failed 0x%08lX\n", static_cast<unsigned long>(hr));
        GdiFlush();
        const int f = size / W;
        Img out(W, W);
        const uint8_t* src = static_cast<const uint8_t*>(bits);
        const float norm = 1.0f / (255.0f * f * f);
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int j = 0; j < f; ++j) {
                    const uint8_t* row = src + (static_cast<size_t>(y * f + j) * size + x * f) * 4;
                    for (int i = 0; i < f; ++i) {
                        acc[0] += row[i * 4 + 2];
                        acc[1] += row[i * 4 + 1];
                        acc[2] += row[i * 4 + 0];
                        acc[3] += row[i * 4 + 3];
                    }
                }
                float* d = out.at(x, y);
                for (int c = 0; c < 4; ++c) d[c] = acc[c] * norm;
            }
        return out;
    }
    void Fill(ID2D1Geometry* g, D2D1_COLOR_F c) {
        if (!g) return;
        brush->SetColor(&c);
        rt->FillGeometry(g, brush, nullptr);
    }
    void Ellipse(float x, float y, float rx, float ry, D2D1_COLOR_F c) {
        brush->SetColor(&c);
        const D2D1_ELLIPSE e = {{x, y}, rx, ry};
        rt->FillEllipse(&e, brush);
    }
    void Ring(float x, float y, float r, float width, D2D1_COLOR_F c) {
        brush->SetColor(&c);
        const D2D1_ELLIPSE e = {{x, y}, r, r};
        rt->DrawEllipse(&e, brush, width, nullptr);
    }
    // A radial glow: color fading out from the center (alpha a at the center).
    void Glow(float x, float y, float rx, float ry, C3 c, float a, float mid = 0.35f) {
        const D2D1_GRADIENT_STOP stops[3] = {{0.0f, DC(c, a)}, {mid, DC(c, a * 0.45f)}, {1.0f, DC(c, 0.0f)}};
        ID2D1GradientStopCollection* coll = nullptr;
        ID2D1RadialGradientBrush* rb = nullptr;
        if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) &&
            SUCCEEDED(rt->CreateRadialGradientBrush(RadialProps({x, y}, {0, 0}, rx, ry), nullptr, coll, &rb))) {
            const D2D1_ELLIPSE e = {{x, y}, rx, ry};
            rt->FillEllipse(&e, rb);
        }
        Release(rb);
        Release(coll);
    }
};

// ----------------------------------------------------------------------------
// Koi (Glass Wings' renderer) and swim lanes
// ----------------------------------------------------------------------------

gwscene::Pond* g_pond = nullptr;  // only its params are used (size scale 1, dpi scale SS)

void DrawKoi(Target& t, const KoiFish& f) {
    gwscene::DrawCtx c{t.rt, g_d2d, t.brush, t.sheen, static_cast<float>(SS), 1.0f};
    t.brush->SetOpacity(1.0f);
    gwscene::KoiFrame fr;
    gwscene::ComputeFrame(*g_pond, f, static_cast<float>(SS), fr);
    gwscene::DrawFish(c, f, fr);
}

// The koi's silhouette (body solid, fins and tail half) for its shadow.
void DrawKoiShadow(Target& t, const KoiFish& f) {
    gwscene::KoiFrame fr;
    gwscene::ComputeFrame(*g_pond, f, static_cast<float>(SS), fr);
    gwscene::KoiFins fins;
    gwscene::PoseFins(fr, f, fins);
    ID2D1PathGeometry* fin = gwscene::BuildFinsAndTail(g_d2d, fr, fins);
    t.Fill(fin, {0, 0, 0, 0.45f});
    Release(fin);
    ID2D1PathGeometry* body = gwscene::BuildOutline(g_d2d, fr, 0.0f);
    t.Fill(body, {0, 0, 0, 1.0f});
    Release(body);
}

// A koi look: Glass Wings' variety and pattern from a seed, then optional tweaks.
KoiFish MakeLook(int variety, unsigned seed, float L, const std::function<void(KoiFish&)>& tweak = nullptr) {
    KoiFish f = {};
    unsigned r = gwscene::MixSeed(seed);
    f.active = true;
    f.baseLen = L;  // px on the 240 screen; drawn at SS
    f.girth = gwscene::RandRange(r, 0.104f, 0.116f);
    f.tailScale = gwscene::RandRange(r, 0.95f, 1.08f);
    f.speedFactor = 1.0f;
    gwscene::InitLook(f, variety, r);
    if (tweak) tweak(f);
    return f;
}

// Path through control points (Catmull-Rom), arc-length parametrized.
struct Path {
    std::vector<KoiVec> pts;
    std::vector<float> cum;
    float len = 0;
    bool closed = false;

    void Build(const std::vector<KoiVec>& c, bool loop) {
        closed = loop;
        pts.clear();
        const int n = static_cast<int>(c.size());
        const int segs = loop ? n : n - 1;
        auto P = [&](int i) {
            if (loop) return c[(i % n + n) % n];
            if (i < 0) return c[0] * 2.0f - c[1];
            if (i >= n) return c[n - 1] * 2.0f - c[n - 2];
            return c[i];
        };
        for (int s = 0; s < segs; ++s) {
            const KoiVec p0 = P(s - 1), p1 = P(s), p2 = P(s + 1), p3 = P(s + 2);
            for (int k = 0; k < 64; ++k) {
                const float t = k / 64.0f, t2 = t * t, t3 = t2 * t;
                const KoiVec a = p1 * 2.0f, b = p2 - p0, cc = p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3,
                             d = p1 * 3.0f - p0 - p2 * 3.0f + p3;
                pts.push_back((a + b * t + cc * t2 + d * t3) * 0.5f);
            }
        }
        pts.push_back(loop ? c[0] : c[n - 1]);
        cum.assign(pts.size(), 0.0f);
        for (size_t i = 1; i < pts.size(); ++i) cum[i] = cum[i - 1] + gwscene::Len(pts[i] - pts[i - 1]);
        len = cum.back();
    }
    KoiVec Pos(float s) const {
        if (closed) {
            s = fmodf(s, len);
            if (s < 0) s += len;
        } else if (s < 0) {
            return pts[0] + gwscene::Normalize(pts[0] - pts[4]) * (-s);
        } else if (s > len) {
            return pts.back() + gwscene::Normalize(pts.back() - pts[pts.size() - 5]) * (s - len);
        }
        const size_t i = std::upper_bound(cum.begin(), cum.end(), s) - cum.begin();
        const size_t j = std::min(std::max<size_t>(i, 1), pts.size() - 1);
        const float seg = cum[j] - cum[j - 1];
        const float f = seg > 1e-6f ? (s - cum[j - 1]) / seg : 0.0f;
        return pts[j - 1] + (pts[j] - pts[j - 1]) * f;
    }
    float Heading(float s) const {
        const KoiVec d = Pos(s + 1.0f) - Pos(s - 1.0f);
        return atan2f(d.y, d.x);
    }
};

struct LaneSpec {
    std::vector<KoiVec> ctrl;  // 240 px; open lanes start and end fully out of sight
    bool closed = false;
    int clones = 2;
    KoiFish look;
    float depth = 0.3f;        // 0 just under the surface .. 1 deep
    float offset = 0.0f;       // fraction of the lane cycle
    float wobble = 0.08f;      // speed variation
    int wobbleK = 1;
    std::vector<std::array<float, 2>> glides;  // (center, half width) in lane-cycle fractions
    std::vector<float> ripples;                // lane-cycle fractions where the koi kisses the surface
};

struct Lane {
    LaneSpec s;
    Path path;
    float C = 0;  // lane cycle, s (clones x loop)
    float T = 0;
    static constexpr int kN = 4096;
    std::vector<float> S, F, Ff, G, V;  // position, tail-beat and fin integrals, glide, speed (px/s)
    int beats = 0, finBeats = 0;
    float phase0 = 0, finPhase0 = 0;

    float Glide(float u) const {
        float g = 0;
        for (const auto& gl : s.glides) {
            float d = fabsf(u - gl[0]);
            d = std::min(d, 1.0f - d);
            if (d < gl[1]) g = std::max(g, 0.5f + 0.5f * cosf(kPi * d / gl[1]));
        }
        return g;
    }
    void Init(float loopSeconds) {
        T = loopSeconds;
        C = T * s.clones;
        std::vector<KoiVec> c = s.ctrl;
        path.Build(c, s.closed);
        std::vector<float> v(kN + 1);
        G.assign(kN + 1, 0);
        for (int i = 0; i <= kN; ++i) {
            const float u = static_cast<float>(i) / kN;
            G[i] = Glide(u);
            v[i] = (1.0f + s.wobble * sinf(kTau * s.wobbleK * u + 1.3f)) * (1.0f - 0.45f * G[i]);
        }
        S.assign(kN + 1, 0);
        for (int i = 1; i <= kN; ++i) S[i] = S[i - 1] + 0.5f * (v[i] + v[i - 1]) / kN;
        const float mean = S[kN];
        for (float& x : S) x /= mean;
        // px/s at each u
        V.assign(kN + 1, 0);
        for (int i = 0; i <= kN; ++i) V[i] = path.len * (v[i] / mean) / C;
        const float L = s.look.baseLen;
        F.assign(kN + 1, 0);
        Ff.assign(kN + 1, 0);
        auto freq = [&](int i) {
            const float bl = V[i] / L;
            return (1.0f - G[i]) * (0.55f + 1.25f * bl) + G[i] * (0.3f + 0.4f * bl);
        };
        auto ffreq = [&](int i) { return (1.0f - G[i]) * 1.25f + G[i] * 0.55f; };
        for (int i = 1; i <= kN; ++i) {
            F[i] = F[i - 1] + 0.5f * (freq(i) + freq(i - 1)) / kN;
            Ff[i] = Ff[i - 1] + 0.5f * (ffreq(i) + ffreq(i - 1)) / kN;
        }
        beats = std::max(1, static_cast<int>(lroundf(F[kN] * C)));
        finBeats = std::max(1, static_cast<int>(lroundf(Ff[kN] * C)));
        unsigned r = gwscene::MixSeed(static_cast<unsigned>(s.look.variety * 977 + static_cast<int>(s.offset * 1000)));
        phase0 = gwscene::RandRange(r, 0, kTau);
        finPhase0 = gwscene::RandRange(r, 0, kTau);
    }
    float Tab(const std::vector<float>& t, float u) const {
        const float x = u * kN;
        const int i = std::min(static_cast<int>(x), kN - 1);
        const float f = x - i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }
    float U(float t, int clone) const { return Frac((t + clone * T) / C + s.offset); }
    float HeadS(float u) const { return path.len * Tab(S, u); }

    KoiFish Pose(float t, int clone) const {
        KoiFish f = s.look;
        const float u = U(t, clone);
        const float sigma = HeadS(u);
        const float L = f.baseLen;
        const float sb = L * gwscene::kBodyFrac / (gwscene::kKoiBodyPts - 1);
        const float st = L * gwscene::kTailFrac * f.tailScale / gwscene::kKoiTailPts;
        float d = 0;
        for (int j = 0; j < gwscene::kKoiChainPts; ++j) {
            if (j > 0) d += j < gwscene::kKoiBodyPts ? sb : st;
            f.chain[j] = path.Pos(sigma - d) * static_cast<float>(SS);
        }
        f.heading = path.Heading(sigma);
        const float vpx = Tab(V, u);
        float dh = gwscene::WrapAngle(path.Heading(sigma + 3.0f) - path.Heading(sigma - 3.0f)) / 6.0f;
        f.omega = std::clamp(dh * vpx, -2.0f, 2.0f);
        f.speed = vpx * SS;
        const float g = Tab(G, u);
        f.glide = g;
        f.gliding = g > 0.5f;
        const float ampSwim = 0.86f + std::min(0.35f, fabsf(f.omega) * 0.3f);
        f.amp = ampSwim * (1.0f - g) + 0.14f * g;
        f.phase = fmodf(phase0 + kTau * beats * Tab(F, u) / F[kN], kTau);
        f.finPhase = fmodf(finPhase0 + kTau * finBeats * Tab(Ff, u) / Ff[kN], kTau);
        return f;
    }
};

struct Ripple {
    float x, y, t0, life, r1, strength;
};

// Ripples where lanes ask for them; each repeats once per loop.
void LaneRipples(const Lane& ln, std::vector<Ripple>& out, float life, float r1, float strength) {
    for (float ue : ln.s.ripples) {
        const float sigma = ln.HeadS(ue);
        const KoiVec at = ln.path.Pos(sigma + 0.04f * ln.s.look.baseLen);
        float t0 = fmodf((ue - ln.s.offset) * ln.C, ln.T);
        if (t0 < 0) t0 += ln.T;
        out.push_back({at.x, at.y, t0, life, r1, strength});
    }
}

// Three concentric rings spreading out, as in the Firefox add-on's Ripples().
void DrawRipple(Target& tg, const Ripple& rp, float t, float T, C3 color) {
    float age = fmodf(t - rp.t0, T);
    if (age < 0) age += T;
    if (age >= rp.life) return;
    const float k = age / rp.life;
    const float scale = 0.04f + 0.96f * (1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k));
    float op;
    if (k < 0.12f) op = 0.45f * (k / 0.12f);
    else if (k < 0.5f) op = 0.45f + (0.2f - 0.45f) * (k - 0.12f) / 0.38f;
    else op = 0.2f * (1.0f - (k - 0.5f) / 0.5f);
    for (int ring = 0; ring < 3; ++ring) {
        const float r = rp.r1 * (1.0f - 0.27f * ring) * scale;
        if (r < 0.5f) continue;
        tg.Ring(rp.x, rp.y, r, 0.8f - 0.15f * ring, DC(color, op * rp.strength));
    }
}

// ----------------------------------------------------------------------------
// Lily pads and flowers (shapes after the Firefox add-on's Pond.cs)
// ----------------------------------------------------------------------------

struct Pad {
    float x, y, r, notch;
    bool old;
};

struct PadLook {
    C3 pad[3], padOld[3], rim, vein;
    C3 petal[3], core, dots;
    bool open = true;
};

ID2D1PathGeometry* PadGeometry(const Pad& p, float scale = 1.0f, float dx = 0, float dy = 0) {
    ID2D1PathGeometry* g = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) {
        Release(g);
        return nullptr;
    }
    const float n = 0.36f, a1 = p.notch + n / 2, a2 = p.notch - n / 2 + kTau;
    const float R = p.r * scale;
    auto At = [&](float a, float rr) { return D2D1::Point2F(p.x + dx + cosf(a) * rr, p.y + dy + sinf(a) * rr * 0.92f); };
    sink->BeginFigure(D2D1::Point2F(p.x + dx, p.y + dy), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(At(a1, R));
    // the arc in a few pieces (so the 0.92 squash follows the ellipse exactly)
    for (int k = 1; k <= 24; ++k) sink->AddLine(At(a1 + (a2 - a1) * k / 24.0f, R));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    Release(sink);
    return g;
}

void DrawPad(Target& tg, const Pad& p, const PadLook& lk, float rimOp = 0.55f) {
    ID2D1PathGeometry* g = PadGeometry(p);
    const C3* c = p.old ? lk.padOld : lk.pad;
    const D2D1_GRADIENT_STOP stops[3] = {{0.0f, DC(c[0])}, {0.6f, DC(c[1])}, {1.0f, DC(c[2])}};
    ID2D1GradientStopCollection* coll = nullptr;
    ID2D1RadialGradientBrush* rb = nullptr;
    if (SUCCEEDED(tg.rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) &&
        SUCCEEDED(tg.rt->CreateRadialGradientBrush(RadialProps({p.x - p.r * 0.25f, p.y - p.r * 0.3f}, {0, 0}, p.r * 1.25f, p.r * 1.2f), nullptr, coll, &rb))) {
        tg.rt->FillGeometry(g, rb, nullptr);
    }
    Release(rb);
    Release(coll);
    const D2D1_COLOR_F rim = DC(lk.rim, rimOp);
    tg.brush->SetColor(&rim);
    tg.rt->DrawGeometry(g, tg.brush, 0.9f, nullptr);
    // veins
    const float n = 0.36f, a1 = p.notch + n / 2, a2 = p.notch - n / 2 + kTau;
    const D2D1_COLOR_F vein = DC(lk.vein, 0.45f);
    tg.brush->SetColor(&vein);
    for (int i = 1; i < 11; ++i) {
        const float a = a1 + (a2 - a1) * i / 11.0f;
        tg.rt->DrawLine({p.x, p.y}, {p.x + cosf(a) * p.r * 0.88f, p.y + sinf(a) * p.r * 0.88f * 0.92f}, tg.brush, 0.6f, nullptr);
    }
    Release(g);
}

ID2D1PathGeometry* PetalGeometry(float len, float wid) {
    // M0 0 C(0.27L,-w)(0.73L,-0.83w) L 0  C(0.73L,0.83w)(0.27L,w) 0 0
    ID2D1PathGeometry* g = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) {
        Release(g);
        return nullptr;
    }
    sink->BeginFigure({0, 0}, D2D1_FIGURE_BEGIN_FILLED);
    { const D2D1_BEZIER_SEGMENT bz = D2D1::BezierSegment({len * 0.27f, -wid}, {len * 0.73f, -wid * 0.83f}, {len, 0}); sink->AddBezier(&bz); }
    { const D2D1_BEZIER_SEGMENT bz = D2D1::BezierSegment({len * 0.73f, wid * 0.83f}, {len * 0.27f, wid}, {0, 0}); sink->AddBezier(&bz); }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    Release(sink);
    return g;
}

void FillWithPetalGradient(Target& tg, ID2D1Geometry* g, const PadLook& lk, float len, float alpha) {
    const D2D1_GRADIENT_STOP stops[3] = {{0.0f, DC(lk.petal[2], alpha)}, {0.35f, DC(lk.petal[1], alpha)}, {1.0f, DC(lk.petal[0], alpha)}};
    ID2D1GradientStopCollection* coll = nullptr;
    ID2D1LinearGradientBrush* lb = nullptr;
    if (SUCCEEDED(tg.rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) &&
        SUCCEEDED(tg.rt->CreateLinearGradientBrush(LinearProps({0, 0}, {len, 0}), nullptr, coll, &lb))) {
        tg.rt->FillGeometry(g, lb, nullptr);
    }
    Release(lb);
    Release(coll);
}

// An open water lily (Pond.cs Flower), s = scale (1 = 22 px across).
void DrawFlower(Target& tg, float x, float y, float s, float rot, const PadLook& lk) {
    D2D1_MATRIX_3X2_F base;
    tg.rt->GetTransform(&base);
    ID2D1PathGeometry* outer = PetalGeometry(11.0f, 3.6f);
    ID2D1PathGeometry* inner = PetalGeometry(7.6f, 2.6f);
    // shadow
    tg.Ellipse(x + 1.5f * s, y + 2.0f * s, 10.0f * s, 10.0f * s, {0, 0.03f, 0.04f, 0.18f});
    for (int i = 0; i < 9; ++i) {
        const D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Rotation(i * 40.0f + 8.0f + rot) * D2D1::Matrix3x2F::Scale(s, s) *
                                    D2D1::Matrix3x2F::Translation(x, y) * Mx(base);
        tg.rt->SetTransform(m);
        FillWithPetalGradient(tg, outer, lk, 11.0f, 1.0f);
    }
    for (int i = 0; i < 7; ++i) {
        const D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Rotation(i * 51.4f + 30.0f + rot) * D2D1::Matrix3x2F::Scale(s, s) *
                                    D2D1::Matrix3x2F::Translation(x, y) * Mx(base);
        tg.rt->SetTransform(m);
        FillWithPetalGradient(tg, inner, lk, 7.6f, 1.0f);
    }
    tg.rt->SetTransform(base);
    tg.Ellipse(x, y, 2.8f * s, 2.8f * s, DC(lk.core));
    for (int i = 0; i < 8; ++i) {
        const float a = i * kPi / 4;
        tg.Ellipse(x + cosf(a) * 2.6f * s, y + sinf(a) * 2.6f * s, 0.7f * s, 0.7f * s, DC(lk.dots));
    }
    Release(outer);
    Release(inner);
}

// A closed bud (Pond.cs Bud).
void DrawBud(Target& tg, float x, float y, float rot, float s, const PadLook& lk) {
    D2D1_MATRIX_3X2_F base;
    tg.rt->GetTransform(&base);
    tg.rt->SetTransform(D2D1::Matrix3x2F::Rotation(rot) * D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y) * Mx(base));
    ID2D1PathGeometry* g = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(g_d2d->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&sink))) {
        sink->BeginFigure({-5, 0}, D2D1_FIGURE_BEGIN_FILLED);
        { const D2D1_BEZIER_SEGMENT bz = D2D1::BezierSegment({-3, -3.5f}, {3, -3.5f}, {6, 0}); sink->AddBezier(&bz); }
        { const D2D1_BEZIER_SEGMENT bz = D2D1::BezierSegment({3, 3.5f}, {-3, 3.5f}, {-5, 0}); sink->AddBezier(&bz); }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        Release(sink);
        tg.Ellipse(1.2f, 1.6f, 6, 3.6f, {0, 0.03f, 0.04f, 0.2f});
        FillWithPetalGradient(tg, g, lk, 6.0f, 1.0f);
        const D2D1_COLOR_F line = DC(lk.petal[2], 0.9f);
        tg.brush->SetColor(&line);
        tg.rt->DrawLine({-4.5f, 0}, {5, 0}, tg.brush, 0.6f, nullptr);
    }
    Release(g);
    tg.rt->SetTransform(base);
}

// Pads of a cluster, placed like PadClusters() (seeded, non-overlapping).
std::vector<Pad> PadCluster(float cx, float cy, int n, float spread, float rMin, float rVar, unsigned seed) {
    unsigned r = gwscene::MixSeed(seed);
    std::vector<Pad> placed;
    for (int i = 0; i < n; ++i) {
        for (int tries = 0; tries < 300; ++tries) {
            const float R = rMin + gwscene::Rand01(r) * rVar;
            const float a = gwscene::Rand01(r) * kTau, dist = i == 0 ? 0 : spread * (0.45f + gwscene::Rand01(r) * 0.6f);
            Pad p{cx + cosf(a) * dist * 1.3f, cy + sinf(a) * dist * 0.9f, R, gwscene::Rand01(r) * kTau, gwscene::Rand01(r) < 0.25f};
            bool ok = true;
            for (const Pad& q : placed) ok = ok && hypotf(q.x - p.x, q.y - p.y) >= (q.r + p.r) * 0.82f;
            if (!ok) continue;
            placed.push_back(p);
            break;
        }
    }
    return placed;
}

// A pad bobbing gently, once per loop.
Pad Bob(const Pad& p, float t, float T, int i) {
    Pad q = p;
    const float ph = kTau * t / T + i * 1.7f;
    q.x += 0.35f * sinf(ph);
    q.y += 0.3f * cosf(ph + 0.6f);
    q.notch += 0.025f * sinf(ph + 2.0f);
    return q;
}

// ----------------------------------------------------------------------------
// Frame output
// ----------------------------------------------------------------------------

// 4x4 Bayer ordered dither to RGB565 (a fixed pattern, so still areas don't shimmer).
const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void ToRgb565(const Rgb& im, std::vector<uint16_t>& out565, std::vector<uint8_t>& rgb8) {
    out565.resize(static_cast<size_t>(W) * W);
    rgb8.resize(static_cast<size_t>(W) * W * 3);
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            const C3 c = im.at(x, y);
            const float th = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f;
            const int r5 = std::min(31, static_cast<int>(Clamp01(c.r) * 255.0f / 8.0f + th));
            const int g6 = std::min(63, static_cast<int>(Clamp01(c.g) * 255.0f / 4.0f + th));
            const int b5 = std::min(31, static_cast<int>(Clamp01(c.b) * 255.0f / 8.0f + th));
            out565[static_cast<size_t>(y) * W + x] = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
            // what the device shows (5/6/5 bits expanded the usual way)
            uint8_t* d = &rgb8[(static_cast<size_t>(y) * W + x) * 3];
            d[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            d[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            d[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        }
}

bool SavePng(const std::wstring& path, int w, int h, const uint8_t* rgba, bool alpha) {
    // WIC wants BGRA / BGR
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * (alpha ? 4 : 3));
    for (int i = 0; i < w * h; ++i) {
        const int n = alpha ? 4 : 3;
        buf[i * n + 0] = rgba[i * n + 2];
        buf[i * n + 1] = rgba[i * n + 1];
        buf[i * n + 2] = rgba[i * n + 0];
        if (alpha) buf[i * n + 3] = rgba[i * n + 3];
    }
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* bag = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, &bag)) &&
        SUCCEEDED(frame->Initialize(bag)) && SUCCEEDED(frame->SetSize(w, h))) {
        WICPixelFormatGUID fmt = alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
        const WICPixelFormatGUID want = fmt;
        if (SUCCEEDED(frame->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, want) &&
            SUCCEEDED(frame->WritePixels(h, w * (alpha ? 4 : 3), static_cast<UINT>(buf.size()), buf.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
            ok = true;
        }
    }
    Release(bag);
    Release(frame);
    Release(enc);
    Release(stream);
    if (!ok) wprintf(L"  ! failed to write %ls\n", path.c_str());
    return ok;
}

// Loads a PNG as straight RGBA8.
bool LoadPng(const std::wstring& path, int& w, int& h, std::vector<uint8_t>& rgba) {
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT iw = 0, ih = 0;
        frame->GetSize(&iw, &ih);
        w = static_cast<int>(iw);
        h = static_cast<int>(ih);
        rgba.resize(static_cast<size_t>(w) * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(rgba.size()), rgba.data()));
    }
    Release(conv);
    Release(frame);
    Release(dec);
    return ok;
}

// ----------------------------------------------------------------------------
// Scenes
// ----------------------------------------------------------------------------

struct Scene {
    std::string name;
    float T = 4.0f;  // loop, s
    int frames = 36;
    std::vector<Lane> lanes;
    std::vector<Ripple> ripples;
    std::function<void(Scene&, float, Rgb&)> render;
    std::shared_ptr<Target> tg;
};

struct KoiDraw {
    KoiFish f;
    float depth;
    int lane;
    float u;
};

// Every koi of every lane at time t, deepest first (ties: lane, then progress, so
// identical clones keep their order across the loop point).
std::vector<KoiDraw> KoiAt(const Scene& sc, float t) {
    std::vector<KoiDraw> v;
    for (int li = 0; li < static_cast<int>(sc.lanes.size()); ++li) {
        const Lane& ln = sc.lanes[li];
        for (int c = 0; c < ln.s.clones; ++c) v.push_back({ln.Pose(t, c), ln.s.depth, li, ln.U(t, c)});
    }
    std::sort(v.begin(), v.end(), [](const KoiDraw& a, const KoiDraw& b) {
        if (a.depth != b.depth) return a.depth > b.depth;
        if (a.lane != b.lane) return a.lane < b.lane;
        return a.u < b.u;
    });
    return v;
}

// Is any part of the koi (with a margin for fins and shadow) on screen?
bool OnScreen(const KoiFish& f) {
    for (int j = 0; j < gwscene::kKoiChainPts; ++j) {
        const float x = f.chain[j].x / SS, y = f.chain[j].y / SS;
        if (x > -24 && y > -24 && x < W + 24 && y < W + 24) return true;
    }
    return false;
}

struct KoiPassLook {
    C3 absorb;              // the water color koi fade toward with depth
    float absorbBase, absorbDepth;
    C3 warm;
    float warmK;
    C3 shadowColor;
    float shadowOp, shadowLen, shadowBlur;
};

// Draws the koi (shadows on the floor first) into the scene.
void KoiPass(Target& tg, const Scene& sc, float t, Rgb& scene, const KoiPassLook& lk,
             const std::function<void(Img&, const KoiDraw&)>& light = nullptr) {
    const std::vector<KoiDraw> koi = KoiAt(sc, t);
    // shadows: one blurred mask per koi, offset by depth (nearer the surface = farther from its shadow)
    for (const KoiDraw& k : koi) {
        if (!OnScreen(k.f)) continue;
        tg.Begin(false);
        DrawKoiShadow(tg, k.f);
        Mask m = AlphaOf(tg.End());
        BlurMask(m, lk.shadowBlur * (1.3f - 0.6f * k.depth));
        const float sx = (1.6f + 3.4f * (1.0f - k.depth)) * lk.shadowLen, sy = (2.2f + 4.2f * (1.0f - k.depth)) * lk.shadowLen;
        Shade(scene, m, sx, sy, lk.shadowColor, lk.shadowOp * (0.75f + 0.25f * k.depth));
    }
    for (const KoiDraw& k : koi) {
        if (!OnScreen(k.f)) continue;
        tg.Begin(false);
        DrawKoi(tg, k.f);
        Img im = tg.End();
        // the water colors the koi, more the deeper it swims; the light warms it
        const float a = lk.absorbBase + lk.absorbDepth * k.depth;
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                float* p = im.at(x, y);
                if (p[3] <= 0.0f) continue;
                C3 c = {p[0] / p[3], p[1] / p[3], p[2] / p[3]};
                c = Mix(Mix(c, lk.warm, lk.warmK), lk.absorb, a);
                p[0] = c.r * p[3];
                p[1] = c.g * p[3];
                p[2] = c.b * p[3];
            }
        if (light) light(im, k);
        Over(scene, im);
    }
}

// ---- Draft 1: lily pond by day (the Firefox add-on's Day look) ----------------

PadLook DayPads() {
    PadLook lk;
    lk.pad[0] = Hex(0x5a9c58); lk.pad[1] = Hex(0x377039); lk.pad[2] = Hex(0x24532b);
    lk.padOld[0] = Hex(0x8a9f48); lk.padOld[1] = Hex(0x5d7a33); lk.padOld[2] = Hex(0x6b5a2c);
    lk.rim = Hex(0x6fae69); lk.vein = Hex(0x1c4422);
    lk.petal[0] = Hex(0xfff7f2); lk.petal[1] = Hex(0xfbe3ea); lk.petal[2] = Hex(0xef93b2);
    lk.core = Hex(0xf3c443); lk.dots = Hex(0xe39a1f);
    return lk;
}

void SetupLily(Scene& sc) {
    sc.name = "lily";
    sc.T = 4.0f;  // 36 frames at 9 fps
    auto lane = [&](LaneSpec s) {
        Lane l;
        l.s = std::move(s);
        l.Init(sc.T);
        sc.lanes.push_back(std::move(l));
    };
    // Patterned koi (kohaku, sanke) circle the lily pads one at a time; plain-colored
    // koi (ogon, platinum, chagoi) cross the pond in pairs of identical twins, which
    // keeps them calm (see the header) and looks natural for one-color koi.
    // 1 kohaku: round the pads at the top right, partly out of sight
    {
        LaneSpec s;
        s.closed = true;
        s.clones = 1;
        s.ctrl = {{158, 48}, {192, 80}, {246, 82}, {278, 36}, {252, -12}, {196, -14}};
        s.look = MakeLook(gwscene::kKoiKohaku, 11, 58);
        s.depth = 0.12f;
        s.offset = 0.05f;
        s.wobble = 0.0f;
        s.ripples = {0.2f};
        lane(s);
    }
    // 2 sanke: round the pads at the bottom left
    {
        LaneSpec s;
        s.closed = true;
        s.clones = 1;
        s.ctrl = {{64, 182}, {22, 150}, {-30, 164}, {-40, 226}, {10, 256}, {62, 232}};
        s.look = MakeLook(gwscene::kKoiSanke, 21, 50);
        s.depth = 0.3f;
        s.offset = 0.55f;
        s.wobble = 0.0f;
        lane(s);
    }
    // 3 yamabuki ogon pair: in from the right, low, out to the left
    {
        LaneSpec s;
        s.ctrl = {{330, 214}, {240, 206}, {160, 192}, {90, 200}, {20, 222}, {-95, 238}};
        s.look = MakeLook(gwscene::kKoiOgon, 4, 58);
        s.depth = 0.22f;
        s.offset = 0.4f;
        s.wobble = 0.1f;
        s.glides = {{0.3f, 0.07f}};
        s.ripples = {0.37f};
        lane(s);
    }
    // 4 platinum pair: from the top right, across the middle, out to the left
    {
        LaneSpec s;
        s.ctrl = {{320, 96}, {230, 92}, {150, 122}, {80, 128}, {10, 110}, {-95, 96}};
        s.look = MakeLook(gwscene::kKoiOgon, 2, 54, [](KoiFish& f) {
            f.body = gwscene::Hex(0xdde1e5);
            f.fin = gwscene::Hex(0xf4f6f8, 0.55f);
            f.finTone = gwscene::Hex(0xffffff, 0.35f);
            f.edge = gwscene::Hex(0x7c848c, 0.45f);
            f.net = gwscene::Hex(0xffffff, 0.3f);
        });
        s.depth = 0.35f;
        s.offset = 0.8f;
        s.glides = {{0.62f, 0.07f}};
        lane(s);
    }
    // 5 chagoi pair, deep: up from the bottom and out at the top
    {
        LaneSpec s;
        s.ctrl = {{120, 330}, {110, 250}, {120, 170}, {100, 90}, {60, 30}, {30, -100}};
        s.look = MakeLook(gwscene::kKoiChagoi, 9, 60);
        s.depth = 0.62f;
        s.offset = 0.15f;
        lane(s);
    }
    for (const Lane& l : sc.lanes) LaneRipples(l, sc.ripples, 2.2f, 24.0f, 1.0f);
    sc.ripples.push_back({198, 132, 0.6f, 2.6f, 26.0f, 0.8f});
    sc.ripples.push_back({40, 70, 2.4f, 2.4f, 22.0f, 0.7f});

    struct Static {
        std::vector<Pad> pads;
        Mask floorNoise, deepNoise;
        bool ready = false;
    };
    auto st = std::make_shared<Static>();
    sc.render = [st](Scene& sc, float t, Rgb& out) {
        Target& tg = *sc.tg;
        const PadLook pl = DayPads();
        if (!st->ready) {
            std::vector<Pad> a = PadCluster(214, 26, 4, 22, 11, 9, 5);
            std::vector<Pad> b = PadCluster(26, 214, 4, 22, 12, 9, 12);
            st->pads = a;
            st->pads.insert(st->pads.end(), b.begin(), b.end());
            st->pads.push_back({236, 150, 12, 2.6f, true});
            for (int y = 0; y < W; ++y)
                for (int x = 0; x < W; ++x) {
                    st->floorNoise.at(x, y) = Fbm(x / 46.0f, y / 30.0f, 3);
                    st->deepNoise.at(x, y) = Fbm(x / 70.0f + 9.0f, y / 52.0f, 9, 3);
                }
            st->ready = true;
        }
        // water: Day look (WaterTop/Mid/Bottom), lifted a little for a small screen
        const C3 top = Hex(0x14484b), mid = Hex(0x0f3c3f), bot = Hex(0x0b3033);
        const C3 mottle = Hex(0x2a6e64), deep = Hex(0x03181b), caust = C3{0.7f, 0.95f, 0.86f};
        const float tc = kTau * t / sc.T;
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float fy = y / (W - 1.0f);
                C3 c = fy < 0.45f ? Mix(top, mid, fy / 0.45f) : Mix(mid, bot, (fy - 0.45f) / 0.55f);
                c = Mix(c, mottle, 0.55f * Smooth(0.45f, 0.8f, st->floorNoise.at(x, y)));
                c = Mix(c, deep, 0.5f * Smooth(0.5f, 0.85f, st->deepNoise.at(x, y)));
                const float k = Caustic(kTau * x / 240.0f, kTau * y / 240.0f, tc);
                c = c + caust * (0.11f * k);
                out.at(x, y) = c;
            }
        KoiPassLook kl{Hex(0x0b3134), 0.1f, 0.5f, Hex(0xffffff), 0.0f, Hex(0x000c0e), 0.42f, 1.0f, 1.6f};
        KoiPass(tg, sc, t, out, kl);
        // pad shadows (on the floor and on the koi below)
        std::vector<Pad> pads;
        for (size_t i = 0; i < st->pads.size(); ++i) pads.push_back(Bob(st->pads[i], t, sc.T, static_cast<int>(i)));
        tg.Begin(true);
        for (const Pad& p : pads) {
            ID2D1PathGeometry* g = PadGeometry(p);
            tg.Fill(g, {0, 0, 0, 1});
            Release(g);
        }
        Mask ps = AlphaOf(tg.End());
        BlurMask(ps, 2.4f);
        Shade(out, ps, 3.5f, 5.0f, Hex(0x000c0e), 0.5f);
        // surface: ripples, pads, flowers, a few sakura petals
        tg.Begin(true);
        for (const Ripple& rp : sc.ripples) DrawRipple(tg, rp, t, sc.T, Hex(0xcfe9e4));
        for (const Pad& p : pads) DrawPad(tg, p, pl);
        DrawFlower(tg, pads[0].x + pads[0].r * 0.3f, pads[0].y + pads[0].r * 0.15f, 0.95f, 6.0f * sinf(tc), pl);
        DrawBud(tg, pads[2].x - pads[2].r * 0.1f, pads[2].y - pads[2].r * 0.2f, 35.0f, 1.1f, pl);
        DrawFlower(tg, pads[4].x + pads[4].r * 0.25f, pads[4].y - pads[4].r * 0.1f, 0.85f, 20.0f + 5.0f * sinf(tc + 1.0f), pl);
        DrawBud(tg, pads[6].x, pads[6].y - pads[6].r * 0.3f, 120.0f, 1.0f, pl);
        // sakura petals drifting in small loops
        static const float kPetals[][3] = {{150, 30, 0.3f}, {40, 150, 1.7f}, {196, 196, 3.1f}, {112, 222, 4.4f}};
        for (const auto& pe : kPetals) {
            const float a = tc + pe[2];
            const float px = pe[0] + 4.0f * cosf(a), py = pe[1] + 2.5f * sinf(a);
            D2D1_MATRIX_3X2_F base;
            tg.rt->GetTransform(&base);
            tg.rt->SetTransform(D2D1::Matrix3x2F::Rotation(pe[2] * 57.0f + 25.0f * sinf(a)) * D2D1::Matrix3x2F::Translation(px, py) * Mx(base));
            tg.Ellipse(0.8f, 1.0f, 3.4f, 2.4f, {0, 0.03f, 0.04f, 0.22f});
            tg.Ellipse(0, 0, 3.4f, 2.4f, DC(Hex(0xf6c4d0), 0.92f));
            tg.rt->SetTransform(base);
        }
        Img surf = tg.End();
        Over(out, surf);
        // soft sky reflection at the top left and a gentle vignette
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float dx = (x - 120.0f) / 120.0f, dy = (y - 120.0f) / 120.0f;
                const float vig = 1.0f - 0.22f * Smooth(0.5f, 1.45f, sqrtf(dx * dx + dy * dy));
                const float sky = 0.05f * expf(-((x - 40.0f) * (x - 40.0f) + (y - 20.0f) * (y - 20.0f)) / (2 * 70.0f * 70.0f));
                C3& c = out.at(x, y);
                c = c * vig + C3{0.75f, 0.9f, 0.9f} * sky;
            }
    };
}

// ---- Draft 2: frosted glass over a sunlit pond (Glass Wings) -------------------

void SetupGlass(Scene& sc) {
    sc.name = "glass";
    sc.T = 4.0f;  // 36 frames at 9 fps
    auto lane = [&](LaneSpec s) {
        Lane l;
        l.s = std::move(s);
        l.Init(sc.T);
        sc.lanes.push_back(std::move(l));
    };
    {
        LaneSpec s;
        s.closed = true;
        s.clones = 1;
        s.ctrl = {{80, 40}, {60, 84}, {6, 92}, {-30, 50}, {-14, -12}, {40, -14}};
        s.look = MakeLook(gwscene::kKoiKohaku, 30, 62);
        s.depth = 0.15f;
        s.offset = 0.1f;
        s.wobble = 0.0f;
        lane(s);
    }
    {
        LaneSpec s;
        s.ctrl = {{330, 170}, {240, 160}, {160, 176}, {90, 196}, {20, 190}, {-95, 168}};
        s.look = MakeLook(gwscene::kKoiOgon, 4, 60);
        s.depth = 0.2f;
        s.offset = 0.6f;
        lane(s);
    }
    {
        LaneSpec s;
        s.ctrl = {{150, 330}, {130, 250}, {80, 170}, {60, 100}, {80, 30}, {130, -100}};
        s.look = MakeLook(gwscene::kKoiChagoi, 9, 58, [](KoiFish& f) {
            // benigoi: solid red-orange
            f.body = gwscene::Hex(0xd6552c);
            f.layerCount = 0;
            f.patchCount = 0;
            f.hasNet = false;
            f.fin = gwscene::Hex(0xe8794a, 0.55f);
            f.finTone = gwscene::Hex(0xf6a070, 0.3f);
            f.edge = gwscene::Hex(0x7a2a10, 0.4f);
            f.sheen = 0.35f;
        });
        s.depth = 0.35f;
        s.offset = 0.35f;
        lane(s);
    }
    {
        LaneSpec s;
        s.ctrl = {{20, -70}, {60, 30}, {130, 70}, {190, 130}, {210, 200}, {200, 330}};
        s.look = MakeLook(gwscene::kKoiAsagi, 3, 54);
        s.depth = 0.5f;
        s.offset = 0.85f;
        lane(s);
    }
    {
        LaneSpec s;
        s.closed = true;
        s.clones = 1;
        s.ctrl = {{250, 230}, {200, 250}, {170, 300}, {230, 330}, {300, 300}, {300, 250}};
        s.look = MakeLook(gwscene::kKoiOgon, 2, 52, [](KoiFish& f) {
            // platinum
            f.body = gwscene::Hex(0xdde1e5);
            f.fin = gwscene::Hex(0xf4f6f8, 0.55f);
            f.finTone = gwscene::Hex(0xffffff, 0.35f);
            f.edge = gwscene::Hex(0x7c848c, 0.45f);
            f.net = gwscene::Hex(0xffffff, 0.3f);
        });
        s.depth = 0.25f;
        lane(s);
    }
    struct Static {
        Mask noise, sand, grain;
        std::vector<Pad> pads;
        bool ready = false;
    };
    auto st = std::make_shared<Static>();
    sc.render = [st](Scene& sc, float t, Rgb& out) {
        Target& tg = *sc.tg;
        if (!st->ready) {
            for (int y = 0; y < W; ++y)
                for (int x = 0; x < W; ++x) {
                    st->noise.at(x, y) = Fbm(x / 40.0f, y / 28.0f, 21);
                    st->sand.at(x, y) = Fbm(x / 9.0f, y / 7.0f, 33, 3);
                    st->grain.at(x, y) = Hash2(x, y, 77) - 0.5f;
                }
            st->pads = PadCluster(214, 210, 3, 20, 12, 8, 8);
            std::vector<Pad> b = PadCluster(18, 20, 3, 18, 11, 8, 15);
            st->pads.insert(st->pads.end(), b.begin(), b.end());
            st->ready = true;
        }
        // a sunlit, shallow pond: pale sand under clear jade water
        const C3 shallow = Hex(0x3aa898), deepC = Hex(0x1b6f74), sandC = Hex(0x8fd0b8);
        const float tc = kTau * t / sc.T;
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float fy = y / (W - 1.0f);
                C3 c = Mix(shallow, deepC, 0.35f + 0.4f * fy);
                c = Mix(c, sandC, 0.35f * Smooth(0.35f, 0.75f, st->noise.at(x, y)) + 0.08f * st->sand.at(x, y));
                const float k = Caustic(kTau * x / 120.0f, kTau * y / 120.0f, tc);
                c = c + C3{0.95f, 1.0f, 0.9f} * (0.24f * k);
                out.at(x, y) = c;
            }
        KoiPassLook kl{Hex(0x1b6f74), 0.03f, 0.3f, Hex(0xfff2d8), 0.06f, Hex(0x06302f), 0.45f, 1.25f, 1.3f};
        KoiPass(tg, sc, t, out, kl);
        PadLook pl = DayPads();
        std::vector<Pad> pads;
        for (size_t i = 0; i < st->pads.size(); ++i) pads.push_back(Bob(st->pads[i], t, sc.T, static_cast<int>(i)));
        tg.Begin(true);
        for (const Pad& p : pads) {
            ID2D1PathGeometry* g = PadGeometry(p);
            tg.Fill(g, {0, 0, 0, 1});
            Release(g);
        }
        Mask ps = AlphaOf(tg.End());
        BlurMask(ps, 2.0f);
        Shade(out, ps, 4.0f, 6.0f, Hex(0x0a3a3a), 0.4f);
        tg.Begin(true);
        for (const Pad& p : pads) DrawPad(tg, p, pl);
        DrawFlower(tg, pads[0].x, pads[0].y - 2, 0.9f, 10.0f * sinf(tc), pl);
        Over(out, tg.End());
        // the frosted glass: blur, a light acrylic tint, luminosity, a fine grain and a sheen
        BlurRgb(out, 2.6f);
        const C3 tint = Hex(0xeef5f3);
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                C3& c = out.at(x, y);
                const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                c = Mix(c, C3{lum, lum, lum}, 0.06f);  // a little desaturation, like acrylic's luminosity layer
                c = Mix(c, tint, 0.3f);
                const float sheen = 0.05f * Smooth(0.0f, 1.0f, 1.0f - (x + y) / 300.0f);
                c = c + C3{1, 1, 1} * sheen + C3{1, 1, 1} * (0.035f * st->grain.at(x, y));
            }
    };
}

// ---- Draft 3: moonlit pond with floating lanterns (the add-on's Night look) -----

void SetupNight(Scene& sc) {
    sc.name = "night";
    sc.T = 4.5f;  // 36 frames at 8 fps
    auto lane = [&](LaneSpec s) {
        Lane l;
        l.s = std::move(s);
        l.Init(sc.T);
        sc.lanes.push_back(std::move(l));
    };
    // 1 kohaku: round the pads at the top left
    {
        LaneSpec s;
        s.closed = true;
        s.clones = 1;
        s.ctrl = {{80, 46}, {64, 86}, {14, 92}, {-26, 52}, {-10, -10}, {46, -12}};
        s.look = MakeLook(gwscene::kKoiKohaku, 17, 58);
        s.depth = 0.2f;
        s.offset = 0.3f;
        s.wobble = 0.0f;
        s.ripples = {0.1f};
        lane(s);
    }
    // 2 platinum pair: left to right through the middle
    {
        LaneSpec s;
        s.ctrl = {{-80, 120}, {20, 120}, {100, 140}, {150, 170}, {210, 190}, {330, 196}};
        s.look = MakeLook(gwscene::kKoiOgon, 2, 62, [](KoiFish& f) {
            f.body = gwscene::Hex(0xdde1e5);
            f.fin = gwscene::Hex(0xf4f6f8, 0.55f);
            f.finTone = gwscene::Hex(0xffffff, 0.35f);
            f.edge = gwscene::Hex(0x7c848c, 0.45f);
            f.net = gwscene::Hex(0xffffff, 0.3f);
        });
        s.depth = 0.15f;
        s.offset = 0.1f;
        s.glides = {{0.5f, 0.09f}};
        s.ripples = {0.55f};
        lane(s);
    }
    // 3 yamabuki ogon pair: from the right, under the lantern, out at the top left
    {
        LaneSpec s;
        s.ctrl = {{330, 84}, {250, 70}, {170, 80}, {110, 60}, {50, 30}, {-100, -40}};
        s.look = MakeLook(gwscene::kKoiOgon, 4, 58);
        s.depth = 0.3f;
        s.offset = 0.5f;
        lane(s);
    }
    // 4 chagoi pair, deep, along the bottom
    {
        LaneSpec s;
        s.ctrl = {{-70, 250}, {20, 220}, {80, 226}, {150, 230}, {220, 214}, {320, 220}};
        s.look = MakeLook(gwscene::kKoiChagoi, 9, 54);
        s.depth = 0.65f;
        s.offset = 0.3f;
        lane(s);
    }
    // 5 sanke: round the pads at the bottom right
    {
        LaneSpec s;
        s.closed = true;
        s.clones = 1;
        s.ctrl = {{178, 222}, {196, 186}, {250, 178}, {282, 230}, {250, 280}, {196, 266}};
        s.look = MakeLook(gwscene::kKoiSanke, 21, 52);
        s.depth = 0.35f;
        s.offset = 0.7f;
        s.wobble = 0.0f;
        lane(s);
    }
    for (const Lane& l : sc.lanes) LaneRipples(l, sc.ripples, 2.6f, 24.0f, 1.0f);
    sc.ripples.push_back({60, 96, 1.4f, 2.8f, 26.0f, 0.7f});
    sc.ripples.push_back({196, 150, 3.6f, 2.8f, 22.0f, 0.6f});

    struct Static {
        Mask mottle, deep;
        std::vector<std::array<float, 5>> stars;  // x, y, size, brightness, twinkle phase
        std::vector<Pad> pads;
        bool ready = false;
    };
    auto st = std::make_shared<Static>();
    sc.render = [st](Scene& sc, float t, Rgb& out) {
        Target& tg = *sc.tg;
        const float tc = kTau * t / sc.T;
        if (!st->ready) {
            for (int y = 0; y < W; ++y)
                for (int x = 0; x < W; ++x) {
                    st->mottle.at(x, y) = Fbm(x / 50.0f, y / 34.0f, 41);
                    st->deep.at(x, y) = Fbm(x / 80.0f + 3.0f, y / 60.0f, 47, 3);
                }
            unsigned r = gwscene::MixSeed(99);
            for (int i = 0; i < 46; ++i) {
                const float b = powf(gwscene::Rand01(r), 2.2f);
                st->stars.push_back({gwscene::Rand01(r) * W, gwscene::Rand01(r) * W, 0.35f + b * 0.55f, 0.25f + b * 0.6f,
                                     gwscene::Rand01(r) * kTau});
            }
            st->pads = PadCluster(30, 40, 3, 20, 11, 8, 23);
            std::vector<Pad> b = PadCluster(222, 232, 3, 20, 12, 8, 31);
            st->pads.insert(st->pads.end(), b.begin(), b.end());
            st->ready = true;
        }
        // water: Night look
        const C3 top = Hex(0x0f2140), mid = Hex(0x0a1830), bot = Hex(0x060f20);
        const C3 mottle = Hex(0x1c3366), deep = Hex(0x01040c);
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float fy = y / (W - 1.0f);
                C3 c = fy < 0.45f ? Mix(top, mid, fy / 0.45f) : Mix(mid, bot, (fy - 0.45f) / 0.55f);
                c = Mix(c, mottle, 0.4f * Smooth(0.45f, 0.8f, st->mottle.at(x, y)));
                c = Mix(c, deep, 0.5f * Smooth(0.5f, 0.85f, st->deep.at(x, y)));
                const float k = Caustic(kTau * x / 240.0f, kTau * y / 240.0f, tc);
                c = c + C3{0.6f, 0.72f, 1.0f} * (0.035f * k);
                out.at(x, y) = c;
            }
        // two paper lanterns, bobbing; their light pools on the water and lights the koi
        struct Lantern {
            float x, y, rot;
        };
        const Lantern lan[2] = {{186 + 2.0f * sinf(tc), 52 + 1.5f * cosf(tc + 0.4f), 18.0f + 4.0f * sinf(tc + 1.0f)},
                                {44 + 1.6f * sinf(tc + 2.2f), 182 + 1.8f * cosf(tc + 1.1f), -12.0f + 4.0f * sinf(tc + 3.0f)}};
        const C3 warm = Hex(0xffb35c);
        auto lightAt = [&](float x, float y) {
            float l = 0;
            for (const Lantern& L : lan) {
                const float d2 = (x - L.x) * (x - L.x) + (y - L.y) * (y - L.y);
                l += expf(-d2 / (2 * 32.0f * 32.0f));
            }
            return l;
        };
        // moon reflection (glow) under everything that floats
        tg.Begin(true);
        const float mx = 150, my = 108;
        tg.Glow(mx, my, 70, 38, Hex(0x9fb8ff), 0.16f);
        tg.Glow(mx, my, 15, 13, Hex(0xe8eeff), 0.75f, 0.5f);
        // the moon broken into slivers by the ripples, shimmering
        unsigned r = gwscene::MixSeed(5);
        for (int row = 0; row < 9; ++row) {
            const float y = my - 18 + row * 4.4f + (gwscene::Rand01(r) - 0.5f) * 1.6f;
            const float reach = 34.0f * sqrtf(std::max(0.05f, 1.0f - powf((y - my) / 22.0f, 2.0f)));
            for (float x = -reach + gwscene::Rand01(r) * 6; x < reach;) {
                const float len = 1.5f + gwscene::Rand01(r) * 5.0f * (1.0f - fabsf(x) / reach);
                const float ph = gwscene::Rand01(r) * kTau;
                const int k = 1 + static_cast<int>(gwscene::Rand01(r) * 2.99f);
                const float sway = 1.6f * sinf(k * tc + ph);
                const float op = (0.25f + gwscene::Rand01(r) * 0.45f) * (1.0f - 0.6f * fabsf(x) / reach) *
                                 (0.55f + 0.45f * sinf(k * tc + ph + 1.2f));
                tg.Ellipse(mx + x + len + sway, y, len, 0.55f, DC(Hex(0xf4f7ff), std::max(0.0f, op)));
                x += len * 2 + 5.0f + gwscene::Rand01(r) * 9.0f;
            }
        }
        // stars reflected in the water, twinkling
        for (const auto& s : st->stars) {
            const int k = 1 + (static_cast<int>(s[4] * 10) % 2);
            const float tw = 0.6f + 0.4f * sinf(k * tc + s[4]);
            tg.Ellipse(s[0], s[1], s[2], s[2], DC(Hex(0xe6edff), s[3] * tw));
        }
        Over(out, tg.End());
        KoiPassLook kl{Hex(0x0a1a38), 0.3f, 0.42f, Hex(0xb8c8ff), 0.08f, Hex(0x000004), 0.3f, 0.7f, 1.8f};
        // koi under a lantern's pool of light glow warm
        KoiPass(tg, sc, t, out, kl, [&](Img& im, const KoiDraw&) {
            for (int y = 0; y < W; ++y)
                for (int x = 0; x < W; ++x) {
                    float* p = im.at(x, y);
                    if (p[3] <= 0.0f) continue;
                    const float l = std::min(1.0f, lightAt(x + 0.5f, y + 0.5f));
                    const C3 lit = C3{p[0], p[1], p[2]} * (0.72f + 0.6f * l) + warm * (0.22f * l * p[3]);
                    p[0] = lit.r;
                    p[1] = lit.g;
                    p[2] = lit.b;
                }
        });
        // the lantern light on the water
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float l = lightAt(x + 0.5f, y + 0.5f);
                C3& c = out.at(x, y);
                c = c * (1.0f + 0.7f * l) + warm * (0.14f * l);
            }
        std::vector<Pad> pads;
        for (size_t i = 0; i < st->pads.size(); ++i) pads.push_back(Bob(st->pads[i], t, sc.T, static_cast<int>(i)));
        tg.Begin(true);
        for (const Pad& p : pads) {
            ID2D1PathGeometry* g = PadGeometry(p);
            tg.Fill(g, {0, 0, 0, 1});
            Release(g);
        }
        Mask ps = AlphaOf(tg.End());
        BlurMask(ps, 2.2f);
        Shade(out, ps, 2.5f, 3.5f, Hex(0x000004), 0.3f);
        PadLook pl;
        pl.pad[0] = Hex(0x335a5c); pl.pad[1] = Hex(0x1f3e41); pl.pad[2] = Hex(0x152c2f);
        pl.padOld[0] = Hex(0x43554a); pl.padOld[1] = Hex(0x2e3c33); pl.padOld[2] = Hex(0x35322b);
        pl.rim = Hex(0x5a8a8a); pl.vein = Hex(0x0e2224);
        pl.petal[0] = Hex(0xeef2ff); pl.petal[1] = Hex(0xd9e0f8); pl.petal[2] = Hex(0xb8a8d8);
        pl.core = Hex(0xd8c070); pl.dots = Hex(0xc0a050);
        tg.Begin(true);
        for (const Ripple& rp : sc.ripples) DrawRipple(tg, rp, t, sc.T, Hex(0xa8bce8));
        for (const Pad& p : pads) DrawPad(tg, p, pl, 0.4f);
        DrawBud(tg, pads[0].x + pads[0].r * 0.3f, pads[0].y, 35.0f, 1.5f, pl);  // water lilies close at night
        DrawBud(tg, pads[3].x - pads[3].r * 0.2f, pads[3].y - pads[3].r * 0.2f, 150.0f, 1.3f, pl);
        // lanterns: glow, paper, cross ribs, flame
        for (const Lantern& L : lan) {
            tg.Glow(L.x, L.y, 44, 44, warm, 0.3f, 0.3f);
            D2D1_MATRIX_3X2_F base;
            tg.rt->GetTransform(&base);
            tg.rt->SetTransform(D2D1::Matrix3x2F::Rotation(L.rot) * D2D1::Matrix3x2F::Scale(1.5f, 1.5f) *
                                D2D1::Matrix3x2F::Translation(L.x, L.y) * Mx(base));
            const D2D1_GRADIENT_STOP stops[3] = {{0.0f, DC(Hex(0xfff3c8))}, {0.5f, DC(Hex(0xffc76a))}, {1.0f, DC(Hex(0xe07a2a))}};
            ID2D1GradientStopCollection* coll = nullptr;
            ID2D1RadialGradientBrush* rb = nullptr;
            if (SUCCEEDED(tg.rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) &&
                SUCCEEDED(tg.rt->CreateRadialGradientBrush(RadialProps({0, 0}, {0, 0}, 8.0f, 8.0f), nullptr, coll, &rb))) {
                const D2D1_ROUNDED_RECT rr = {{-5.5f, -5.5f, 5.5f, 5.5f}, 1.6f, 1.6f};
                tg.rt->FillRoundedRectangle(&rr, rb);
                const D2D1_COLOR_F edge = DC(Hex(0x7a4416), 0.7f);
                tg.brush->SetColor(&edge);
                tg.rt->DrawRoundedRectangle(&rr, tg.brush, 0.9f);
                const D2D1_COLOR_F rib = DC(Hex(0xa8601e), 0.35f);
                tg.brush->SetColor(&rib);
                tg.rt->DrawLine({-5.5f, 0}, {5.5f, 0}, tg.brush, 0.6f, nullptr);
                tg.rt->DrawLine({0, -5.5f}, {0, 5.5f}, tg.brush, 0.6f, nullptr);
            }
            Release(rb);
            Release(coll);
            tg.Ellipse(0, 0, 1.6f + 0.25f * sinf(3 * tc), 1.6f + 0.25f * sinf(3 * tc), DC(Hex(0xfffbe8)));
            tg.rt->SetTransform(base);
        }
        // fireflies wandering in small loops, flashing now and then
        static const float kFly[][5] = {{120, 34, 14, 7, 0.2f}, {214, 120, 10, 12, 2.0f}, {84, 206, 16, 8, 3.7f},
                                        {20, 110, 9, 10, 5.1f}, {160, 226, 12, 6, 1.1f}};
        int fi = 0;
        for (const auto& ff : kFly) {
            const float a = tc * (fi % 2 ? -1.0f : 1.0f) + ff[4];
            const float x = ff[0] + ff[2] * cosf(a) + 3.0f * sinf(2 * a), y = ff[1] + ff[3] * sinf(a);
            // one or two flashes per loop
            const float flashes = (fi % 3 == 0) ? 2.0f : 1.0f;
            const float ph = Frac(flashes * t / sc.T + ff[4] / kTau);
            const float on = Smooth(0.0f, 0.12f, ph) * (1.0f - Smooth(0.3f, 0.55f, ph));
            const float lvl = 0.15f + 0.85f * on;
            tg.Glow(x, y, 11, 11, Hex(0xd8ff8a), 0.35f * lvl, 0.25f);
            tg.Ellipse(x, y, 1.3f, 1.3f, DC(Hex(0xfbffd8), 0.4f + 0.6f * lvl));
            ++fi;
        }
        Over(out, tg.End());
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float dx = (x - 120.0f) / 120.0f, dy = (y - 120.0f) / 120.0f;
                const float vig = 1.0f - 0.3f * Smooth(0.45f, 1.45f, sqrtf(dx * dx + dy * dy));
                out.at(x, y) = out.at(x, y) * vig;
            }
    };
}

bool SetupScene(const std::string& name, Scene& sc) {
    if (name == "lily") SetupLily(sc);
    else if (name == "glass") SetupGlass(sc);
    else if (name == "night") SetupNight(sc);
    else return false;
    return true;
}

// ----------------------------------------------------------------------------
// Commands
// ----------------------------------------------------------------------------

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

int CmdRender(const std::string& draft, const std::wstring& outDir, int frames) {
    Scene sc;
    if (!SetupScene(draft, sc)) {
        printf("unknown draft %s\n", draft.c_str());
        return 2;
    }
    if (frames > 0) sc.frames = frames;
    sc.tg = std::make_shared<Target>();
    if (!sc.tg->Create()) return 1;
    CreateDirectoryW(outDir.c_str(), nullptr);
    CreateDirectoryW((outDir + L"\\frames").c_str(), nullptr);
    FILE* f = _wfopen((outDir + L"\\anim.rgb565").c_str(), L"wb");
    if (!f) return 1;
    std::vector<uint16_t> first565;
    double tsum = 0;
    for (int i = 0; i <= sc.frames; ++i) {
        const float t = sc.T * i / sc.frames;
        Rgb im;
        LARGE_INTEGER q0, q1, qf;
        QueryPerformanceFrequency(&qf);
        QueryPerformanceCounter(&q0);
        sc.render(sc, t, im);
        QueryPerformanceCounter(&q1);
        tsum += (q1.QuadPart - q0.QuadPart) * 1000.0 / qf.QuadPart;
        std::vector<uint16_t> px565;
        std::vector<uint8_t> rgb8;
        ToRgb565(im, px565, rgb8);
        if (i == sc.frames) {
            // Loop check: one frame past the end must match frame 0.
            int diff = 0, maxStep = 0;
            for (size_t k = 0; k < px565.size(); ++k) {
                if (px565[k] == first565[k]) continue;
                ++diff;
                const int a = px565[k], b = first565[k];
                maxStep = std::max({maxStep, abs((a >> 11) - (b >> 11)), abs(((a >> 5) & 63) - ((b >> 5) & 63)), abs((a & 31) - (b & 31))});
            }
            FILE* lc = _wfopen((outDir + L"\\loopcheck.txt").c_str(), L"w");
            if (lc) {
                fprintf(lc, "frame %d (t = T) vs frame 0: %d of %d pixels differ, by at most %d RGB565 step(s)\n", sc.frames, diff,
                        W * W, maxStep);
                fclose(lc);
            }
            printf("loop check: %d pixels differ between t=T and t=0 (max %d step)\n", diff, maxStep);
            break;
        }
        if (i == 0) first565 = px565;
        fwrite(px565.data(), 2, px565.size(), f);
        wchar_t name[64];
        swprintf(name, 64, L"\\frames\\f%03d.png", i);
        SavePng(outDir + name, W, W, rgb8.data(), false);
        printf("frame %d/%d\r", i + 1, sc.frames);
    }
    fclose(f);
    printf("\n%s: %d frames, %.1f s loop (%.1f fps), %d bytes, %.0f ms/frame to render\n", sc.name.c_str(), sc.frames, sc.T,
           sc.frames / sc.T, sc.frames * W * W * 2, tsum / (sc.frames + 1));
    return 0;
}

// Koi looks posed large on one sheet.
int CmdCatalog(const std::wstring& out) {
    std::vector<KoiFish> looks;
    const int vars[] = {gwscene::kKoiKohaku, gwscene::kKoiSanke, gwscene::kKoiShowa, gwscene::kKoiOgon, gwscene::kKoiAsagi, gwscene::kKoiChagoi};
    for (int v : vars)
        for (unsigned seed = 1; seed <= 8; ++seed) looks.push_back(MakeLook(v, seed * 3 + v, 64));
    const int cols = 8, cell = 80;
    const int rows = static_cast<int>((looks.size() + cols - 1) / cols);
    std::vector<uint8_t> sheet(static_cast<size_t>(cols * cell) * rows * cell * 3, 0);
    Target tg;
    if (!tg.Create()) return 1;
    for (size_t i = 0; i < looks.size(); ++i) {
        KoiFish f = looks[i];
        // straight, heading right, centered in a 240 frame (we keep the middle 80x80... scaled view)
        const float L = f.baseLen;
        const float sb = L * gwscene::kBodyFrac / (gwscene::kKoiBodyPts - 1), st = L * gwscene::kTailFrac * f.tailScale / gwscene::kKoiTailPts;
        float d = 0;
        for (int j = 0; j < gwscene::kKoiChainPts; ++j) {
            if (j > 0) d += j < gwscene::kKoiBodyPts ? sb : st;
            f.chain[j] = KoiVec{120.0f + L * 0.45f - d, 120.0f + 6.0f * sinf(d / L * 3.0f)} * static_cast<float>(SS);
        }
        f.amp = 0.6f;
        f.phase = 1.0f;
        f.finPhase = 0.5f;
        tg.Begin(false);
        DrawKoi(tg, f);
        Img im = tg.End();
        const int cx = static_cast<int>(i % cols) * cell, cy = static_cast<int>(i / cols) * cell;
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x) {
                const float* p = im.at(80 + x, 80 + y);
                C3 bg = Hex(0x0f3c3f);
                C3 c = C3{p[0], p[1], p[2]} + bg * (1 - p[3]);
                uint8_t* dst = &sheet[(static_cast<size_t>(cy + y) * cols * cell + cx + x) * 3];
                dst[0] = static_cast<uint8_t>(Clamp01(c.r) * 255);
                dst[1] = static_cast<uint8_t>(Clamp01(c.g) * 255);
                dst[2] = static_cast<uint8_t>(Clamp01(c.b) * 255);
            }
    }
    SavePng(out, cols * cell, rows * cell, sheet.data(), false);
    return 0;
}


// ---- icons ---------------------------------------------------------------------

unsigned ParseHex(const std::wstring& s) { return static_cast<unsigned>(wcstoul(s.c_str(), nullptr, 16)); }

// Straight-alpha RGBA8 -> alpha mask of `size` x `size`, the shape fitted into the
// middle (size - 2 * margin), sampled 4x4 per pixel.
std::vector<float> FitAlpha(int w, int h, const std::vector<uint8_t>& rgba, int size, int margin) {
    std::vector<float> out(static_cast<size_t>(size) * size, 0.0f);
    const float inner = static_cast<float>(size - 2 * margin);
    const float sc = inner / std::max(w, h);
    const float ox = (size - w * sc) * 0.5f, oy = (size - h * sc) * 0.5f;
    auto A = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0.0f;
        return rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] / 255.0f;
    };
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float acc = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    const float sx = (x + (i + 0.5f) / 4 - ox) / sc - 0.5f, sy = (y + (j + 0.5f) / 4 - oy) / sc - 0.5f;
                    const int x0 = static_cast<int>(floorf(sx)), y0 = static_cast<int>(floorf(sy));
                    const float fx = sx - x0, fy = sy - y0;
                    acc += (A(x0, y0) * (1 - fx) + A(x0 + 1, y0) * fx) * (1 - fy) +
                           (A(x0, y0 + 1) * (1 - fx) + A(x0 + 1, y0 + 1) * fx) * fy;
                }
            out[static_cast<size_t>(y) * size + x] = acc / 16.0f;
        }
    return out;
}

// Simple silhouettes for the systems that have no device picture.
std::vector<float> SyntheticIcon(const std::wstring& name, int size, int margin) {
    Target tg;
    std::vector<float> out(static_cast<size_t>(size) * size, 0.0f);
    if (!tg.Create()) return out;
    // Drawn in a 100 x 100 box at (10, 10), in 240-px units, in two passes: the
    // shapes, then the holes punched out of them.
    Img pass[2];
    for (int cutPass = 0; cutPass < 2; ++cutPass) {
        tg.Begin(true);
        const D2D1_COLOR_F white = {1, 1, 1, 1};
        tg.brush->SetColor(&white);
        auto rr = [&](float l, float t, float r, float b, float rad, bool cut) {
            if (cut != (cutPass == 1)) return;
            const D2D1_ROUNDED_RECT q = {{l, t, r, b}, rad, rad};
            tg.rt->FillRoundedRectangle(&q, tg.brush);
        };
        auto dot = [&](float x, float y, float r, bool cut) {
            if (cut != (cutPass == 1)) return;
            const D2D1_ELLIPSE e = {{x, y}, r, r};
            tg.rt->FillEllipse(&e, tg.brush);
        };
        if (name == L"Final Burn Alpha 2012" || name == L"MAME 2000") {
            // arcade stick: base with two buttons, shaft, ball
            rr(12, 64, 108, 100, 10, false);
            rr(45, 34, 53, 70, 3, false);
            dot(49, 30, 15, false);
            dot(76, 80, 7, true);
            dot(95, 80, 7, true);
        } else if (name == L"PICO-8") {
            // a little computer: a screen in its bezel above a keyboard
            rr(18, 12, 102, 76, 8, false);
            rr(28, 21, 92, 66, 3, true);
            rr(8, 82, 112, 106, 6, false);
            for (int i = 0; i < 6; ++i) rr(16 + i * 15.5f, 88, 27 + i * 15.5f, 99, 1.5f, true);
        } else {
            // Pokemon Mini: a small rounded handheld with a screen, a pad and a button
            rr(22, 10, 98, 110, 18, false);
            rr(32, 22, 88, 62, 4, true);
            dot(44, 86, 9, true);
            dot(77, 84, 7, true);
        }
        pass[cutPass] = tg.End();
    }
    const float sc = (size - 2.0f * margin) / 100.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // 2x2 samples of the 240-px drawing
            float acc = 0;
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const float sx = 10 + (x + (i + 0.5f) / 2 - margin) / sc, sy = 10 + (y + (j + 0.5f) / 2 - margin) / sc;
                    const int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
                    if (ix < 0 || iy < 0 || ix >= W || iy >= W) continue;
                    acc += pass[0].at(ix, iy)[3] * (1.0f - pass[1].at(ix, iy)[3]);
                }
            out[static_cast<size_t>(y) * size + x] = acc / 4.0f;
        }
    return out;
}

// koi_frames.exe icons <devicesdir> <outdir> <iconRRGGBB> <haloRRGGBB> <haloAlpha> <size> <fileName>
int CmdIcons(int argc, wchar_t** argv) {
    if (argc < 9) return 2;
    const std::wstring inDir = argv[2], outDir = argv[3];
    const C3 ic = Hex(ParseHex(argv[4])), halo = Hex(ParseHex(argv[5]));
    const float haloA = static_cast<float>(_wtof(argv[6]));
    const int size = _wtoi(argv[7]);
    const std::wstring fileName = argv[8];
    const int margin = std::max(6, size / 14);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((inDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    CreateDirectoryW(outDir.c_str(), nullptr);
    int n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const std::wstring sys = fd.cFileName;
        std::vector<float> a;
        int w = 0, hh = 0;
        std::vector<uint8_t> px;
        if (LoadPng(inDir + L"\\" + sys + L"\\device.png", w, hh, px) ||
            LoadPng(inDir + L"\\" + sys + L"\\device_W140.png", w, hh, px)) {
            a = FitAlpha(w, hh, px, size, margin);
        } else {
            a = SyntheticIcon(sys, size, margin);
        }
        Mask b;
        b.w = size;
        b.h = size;
        b.v = a;
        BlurMask(b, size / 40.0f + 1.0f);
        std::vector<uint8_t> outPx(static_cast<size_t>(size) * size * 4);
        for (int i = 0; i < size * size; ++i) {
            const float ia = a[i];
            const float ha = std::min(1.0f, b.v[i] * 2.2f) * haloA;
            const float oa = ia + ha * (1 - ia);
            const C3 c = oa > 0 ? (ic * ia + halo * (ha * (1 - ia))) * (1.0f / oa) : ic;
            outPx[i * 4 + 0] = static_cast<uint8_t>(lroundf(Clamp01(c.r) * 255));
            outPx[i * 4 + 1] = static_cast<uint8_t>(lroundf(Clamp01(c.g) * 255));
            outPx[i * 4 + 2] = static_cast<uint8_t>(lroundf(Clamp01(c.b) * 255));
            outPx[i * 4 + 3] = static_cast<uint8_t>(lroundf(Clamp01(oa) * 255));
        }
        const std::wstring d1 = outDir + L"\\" + sys, d2 = d1 + L"\\system_artwork";
        CreateDirectoryW(d1.c_str(), nullptr);
        CreateDirectoryW(d2.c_str(), nullptr);
        SavePng(d2 + L"\\" + fileName, size, size, outPx.data(), true);
        ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    printf("%d icons\n", n);
    return 0;
}

// ---- contrast --------------------------------------------------------------------
//
// koi_frames.exe contrast <framesdir> <under.png> <boxes.txt> <out.txt>
// Composites under.png (every overlay element except the text) over each frame and,
// for each box ("x0 y0 x1 y1 RRGGBB label" per line), computes the WCAG contrast
// ratio between the text color and every background pixel in the box, over all
// frames. Reports the worst pixel, the 5th percentile and the median.

float Lin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
float Luma(float r, float g, float b) { return 0.2126f * Lin(r) + 0.7152f * Lin(g) + 0.0722f * Lin(b); }

int CmdContrast(int argc, wchar_t** argv) {
    if (argc < 6) return 2;
    const std::wstring dir = argv[2];
    int uw = 0, uh = 0;
    std::vector<uint8_t> under;
    if (!LoadPng(argv[3], uw, uh, under) || uw != W || uh != W) {
        printf("bad overlay\n");
        return 1;
    }
    struct Box {
        int x0, y0, x1, y1;
        C3 c;
        char label[64];
        std::vector<float> ratios;
    };
    std::vector<Box> boxes;
    FILE* bf = _wfopen(argv[4], L"r");
    if (!bf) return 1;
    char line[256];
    while (fgets(line, sizeof line, bf)) {
        Box b{};
        unsigned hex = 0;
        if (sscanf(line, "%d %d %d %d %x %63[^\r\n]", &b.x0, &b.y0, &b.x1, &b.y1, &hex, b.label) >= 5) {
            b.c = Hex(hex);
            boxes.push_back(b);
        }
    }
    fclose(bf);
    int frames = 0;
    for (;; ++frames) {
        wchar_t name[64];
        swprintf(name, 64, L"\\f%03d.png", frames);
        int w = 0, h = 0;
        std::vector<uint8_t> fr;
        if (!LoadPng(dir + name, w, h, fr)) break;
        for (Box& b : boxes) {
            const float lt = Luma(b.c.r, b.c.g, b.c.b);
            for (int y = std::max(0, b.y0); y < std::min(W, b.y1); ++y)
                for (int x = std::max(0, b.x0); x < std::min(W, b.x1); ++x) {
                    const size_t i = (static_cast<size_t>(y) * W + x) * 4;
                    const float a = under[i + 3] / 255.0f;
                    float c[3];
                    for (int k = 0; k < 3; ++k) c[k] = (under[i + k] / 255.0f) * a + (fr[i + k] / 255.0f) * (1 - a);
                    const float lb = Luma(c[0], c[1], c[2]);
                    b.ratios.push_back((std::max(lt, lb) + 0.05f) / (std::min(lt, lb) + 0.05f));
                }
        }
    }
    FILE* of = _wfopen(argv[5], L"w");
    if (!of) return 1;
    fprintf(of, "%-30s %7s %8s %7s\n", "text (WCAG ratio)", "worst", "5th pct", "median");
    for (Box& b : boxes) {
        std::sort(b.ratios.begin(), b.ratios.end());
        if (b.ratios.empty()) continue;
        const float worst = b.ratios.front(), p5 = b.ratios[b.ratios.size() / 20], med = b.ratios[b.ratios.size() / 2];
        fprintf(of, "%-30s %7.2f %8.2f %7.2f\n", b.label, worst, p5, med);
        printf("%-30s worst %5.2f  p5 %5.2f  median %5.2f\n", b.label, worst, p5, med);
    }
    fprintf(of, "(over %d frames; every pixel of each text's box, under the text; 4.5 = WCAG AA for normal text, 3.0 = AA for large text)\n",
            frames);
    fclose(of);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printf("usage: koi_frames.exe render <lily|glass|night> <outdir> [--frames N]\n"
               "       koi_frames.exe icons <devicesdir> <outdir> <iconRRGGBB> <haloRRGGBB> <haloAlpha> <size> <fileName>\n"
               "       koi_frames.exe contrast <framesdir> <under.png> <boxes.txt> <out.txt>\n"
               "       koi_frames.exe catalog <out.png>\n");
        return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) return 1;
    const D2D1_FACTORY_OPTIONS opts = {D2D1_DEBUG_LEVEL_NONE};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &opts, reinterpret_cast<void**>(&g_d2d))))
        return 1;
    gwscene::Params params = {1, 1.0f, 1.0f, 1.0f, false};
    g_pond = gwscene::Pond_Create(params, 1);
    g_pond->dpiScale = static_cast<float>(SS);

    const std::wstring cmd = argv[1];
    int rc = 2;
    if (cmd == L"render" && argc >= 4) {
        int frames = 0;
        for (int i = 4; i + 1 < argc; ++i)
            if (std::wstring(argv[i]) == L"--frames") frames = _wtoi(argv[i + 1]);
        std::wstring d = argv[2];
        rc = CmdRender(std::string(d.begin(), d.end()), argv[3], frames);
    } else if (cmd == L"icons") {
        rc = CmdIcons(argc, argv);
    } else if (cmd == L"contrast") {
        rc = CmdContrast(argc, argv);
    } else if (cmd == L"catalog" && argc >= 3) {
        rc = CmdCatalog(argv[2]);
    }
    gwscene::Pond_Destroy(g_pond);
    Release(g_d2d);
    Release(g_wic);
    CoUninitialize();
    return rc;
}
