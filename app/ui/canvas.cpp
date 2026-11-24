#include "canvas.h"

#include <windows.h> // HWND, MultiByteToWideChar, COM
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <cmath>   // ceilf
#include <cstring> // strlen

namespace nova2 {

namespace {

// Every COM interface starts with IUnknown, so one release fits all.
void release(void*& p) {
    if (p) {
        static_cast<IUnknown*>(p)->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F color_of(u32 c) {
    return D2D1::ColorF(static_cast<float>((c >> 16) & 255) / 255.0f, static_cast<float>((c >> 8) & 255) / 255.0f,
                        static_cast<float>(c & 255) / 255.0f, 1.0f);
}

// ASCII or UTF-8 text to UTF-16 for DirectWrite; returns the length.
u32 widen(const char* s, WCHAR* out, u32 cap) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, static_cast<int>(cap));
    if (n <= 0) {
        out[0] = 0;
        return 0;
    }
    return static_cast<u32>(n - 1);
}

ID2D1RenderTarget* rt(void* p) { return static_cast<ID2D1RenderTarget*>(p); }

} // namespace

void Canvas::release_images() {
    for (u32 k = 0; k < kImageSlots; ++k)
        release(image_[k]);
    release(overlay_);
}

Canvas::~Canvas() {
    free_fonts();
    release_images();
    release(brush_);
    release(hwnd_target_);
    release(bmp_target_);
    release(bitmap_);
    release(wic_);
    release(dwrite_);
    release(d2d_);
}

bool Canvas::init(TextBuf* why) {
    ID2D1Factory* f = nullptr;
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &opts,
                                 reinterpret_cast<void**>(&f)))) {
        if (why)
            why->append("canvas: Direct2D is not available");
        return false;
    }
    d2d_ = f;
    IDWriteFactory* dw = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&dw)))) {
        if (why)
            why->append("canvas: DirectWrite is not available");
        return false;
    }
    dwrite_ = dw;
    IWICImagingFactory* w = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&w)))) {
        if (why)
            why->append("canvas: WIC is not available (is COM initialised?)");
        return false;
    }
    wic_ = w;
    return true;
}

void Canvas::free_fonts() {
    for (u32 i = 0; i < kFontCount; ++i)
        release(fonts_[i]);
}

bool Canvas::make_fonts() {
    free_fonts();
    struct Spec {
        const WCHAR* family;
        DWRITE_FONT_WEIGHT weight;
        float size;
    };
    const Spec specs[kFontCount] = {{L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13},
                                    {L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 13},
                                    {L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 12},
                                    {L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11}};
    IDWriteFactory* dw = static_cast<IDWriteFactory*>(dwrite_);
    for (u32 i = 0; i < kFontCount; ++i) {
        IDWriteTextFormat* fmt = nullptr;
        if (FAILED(dw->CreateTextFormat(specs[i].family, nullptr, specs[i].weight, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, specs[i].size * scale_, L"en-us", &fmt)))
            return false;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fonts_[i] = fmt;
        IDWriteTextLayout* lay = nullptr;
        if (SUCCEEDED(dw->CreateTextLayout(L"Ag", 2, fmt, 1000.0f, 1000.0f, &lay))) {
            DWRITE_TEXT_METRICS m = {};
            lay->GetMetrics(&m);
            line_h_[i] = static_cast<i32>(ceilf(m.height));
            lay->Release();
        }
    }
    font_scale_ = scale_;
    return true;
}

bool Canvas::make_brush() {
    if (brush_ && brush_target_ == target_)
        return true;
    release(brush_);
    ID2D1SolidColorBrush* b = nullptr;
    if (FAILED(rt(target_)->CreateSolidColorBrush(color_of(0), &b)))
        return false;
    brush_ = b;
    brush_target_ = target_;
    return true;
}

bool Canvas::begin_window(void* hwnd, i32 w, i32 h, float scale) {
    if (!d2d_ || w <= 0 || h <= 0)
        return false;
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h));
    if (!hwnd_target_ || hwnd != hwnd_) {
        release_images();
        release(hwnd_target_);
        ID2D1HwndRenderTarget* t = nullptr;
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f, 96.0f);
        if (FAILED(static_cast<ID2D1Factory*>(d2d_)->CreateHwndRenderTarget(
                props, D2D1::HwndRenderTargetProperties(static_cast<HWND>(hwnd), size), &t)))
            return false;
        hwnd_target_ = t;
        hwnd_ = hwnd;
    } else if (w != w_ || h != h_) {
        static_cast<ID2D1HwndRenderTarget*>(hwnd_target_)->Resize(size);
    }
    target_ = hwnd_target_;
    w_ = w;
    h_ = h;
    scale_ = scale;
    if (font_scale_ != scale_ && !make_fonts())
        return false;
    if (!make_brush())
        return false;
    rt(target_)->BeginDraw();
    rt(target_)->SetTransform(D2D1::Matrix3x2F::Identity());
    rt(target_)->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    drawing_ = true;
    return true;
}

bool Canvas::begin_bitmap(i32 w, i32 h, float scale) {
    if (!d2d_ || !wic_ || w <= 0 || h <= 0)
        return false;
    release_images();
    release(bmp_target_);
    release(bitmap_);
    IWICBitmap* bmp = nullptr;
    if (FAILED(static_cast<IWICImagingFactory*>(wic_)->CreateBitmap(
            static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp)))
        return false;
    bitmap_ = bmp;
    // Software rendering: the same pixels on every run (design.md 2).
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    ID2D1RenderTarget* t = nullptr;
    if (FAILED(static_cast<ID2D1Factory*>(d2d_)->CreateWicBitmapRenderTarget(bmp, props, &t)))
        return false;
    bmp_target_ = t;
    target_ = t;
    w_ = w;
    h_ = h;
    scale_ = scale;
    if (font_scale_ != scale_ && !make_fonts())
        return false;
    if (!make_brush())
        return false;
    rt(target_)->BeginDraw();
    rt(target_)->SetTransform(D2D1::Matrix3x2F::Identity());
    rt(target_)->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    drawing_ = true;
    return true;
}

void Canvas::end() {
    if (!drawing_)
        return;
    unclip();
    const HRESULT hr = rt(target_)->EndDraw();
    drawing_ = false;
    if (hr == D2DERR_RECREATE_TARGET && target_ == hwnd_target_) {
        release(brush_);
        release_images();
        release(hwnd_target_); // made afresh next frame
        target_ = nullptr;
    }
}

void Canvas::set_color(u32 color) { static_cast<ID2D1SolidColorBrush*>(brush_)->SetColor(color_of(color)); }

void Canvas::clip(const Rect& r) {
    unclip();
    rt(target_)->PushAxisAlignedClip(
        D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.right()),
                    static_cast<float>(r.bottom())),
        D2D1_ANTIALIAS_MODE_ALIASED);
    clipped_ = true;
}

void Canvas::unclip() {
    if (clipped_) {
        rt(target_)->PopAxisAlignedClip();
        clipped_ = false;
    }
}

void Canvas::fill(const Rect& r, u32 color) {
    if (r.w <= 0 || r.h <= 0)
        return;
    rt(target_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    set_color(color);
    rt(target_)->FillRectangle(D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y),
                                           static_cast<float>(r.right()), static_cast<float>(r.bottom())),
                               static_cast<ID2D1Brush*>(brush_));
}

void Canvas::frame(const Rect& r, u32 color) {
    if (r.w <= 0 || r.h <= 0)
        return;
    rt(target_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    set_color(color);
    rt(target_)->DrawRectangle(D2D1::RectF(static_cast<float>(r.x) + 0.5f, static_cast<float>(r.y) + 0.5f,
                                           static_cast<float>(r.right()) - 0.5f, static_cast<float>(r.bottom()) - 0.5f),
                               static_cast<ID2D1Brush*>(brush_), 1.0f);
}

void Canvas::image(i32 x, i32 y, const u32* rgb, i32 w, i32 h, i32 cell, u32 slot, bool same) {
    if (w <= 0 || h <= 0 || cell <= 0 || slot >= kImageSlots)
        return;
    ID2D1Bitmap* bmp = static_cast<ID2D1Bitmap*>(image_[slot]);
    const UINT32 pitch = static_cast<UINT32>(w) * 4;
    if (bmp && (image_target_[slot] != target_ || image_w_[slot] != w || image_h_[slot] != h)) {
        release(image_[slot]);
        bmp = nullptr;
    }
    if (!bmp) {
        // 0x00RRGGBB in memory is blue, green, red and a byte that is ignored.
        const D2D1_BITMAP_PROPERTIES props =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(rt(target_)->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)), rgb, pitch,
                                             props, &bmp)))
            return;
        image_[slot] = bmp;
        image_target_[slot] = target_;
        image_w_[slot] = w;
        image_h_[slot] = h;
    } else if (!same) {
        rt(target_)->Flush(); // anything drawn from it this frame, before its pixels change
        if (FAILED(bmp->CopyFromMemory(nullptr, rgb, pitch)))
            return;
    }
    rt(target_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    rt(target_)->DrawBitmap(bmp,
                            D2D1::RectF(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w * cell),
                                        static_cast<float>(y + h * cell)),
                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr);
}

void Canvas::overlay(const Rect& dst, const u32* argb, i32 w, i32 h) {
    if (w <= 0 || h <= 0 || dst.w <= 0 || dst.h <= 0)
        return;
    ID2D1Bitmap* bmp = static_cast<ID2D1Bitmap*>(overlay_);
    const UINT32 pitch = static_cast<UINT32>(w) * 4;
    if (bmp && (overlay_target_ != target_ || overlay_w_ != w || overlay_h_ != h)) {
        release(overlay_);
        bmp = nullptr;
    }
    if (!bmp) {
        const D2D1_BITMAP_PROPERTIES props =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(rt(target_)->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)), argb,
                                             pitch, props, &bmp)))
            return;
        overlay_ = bmp;
        overlay_target_ = target_;
        overlay_w_ = w;
        overlay_h_ = h;
    } else {
        rt(target_)->Flush(); // anything drawn from it this frame, before its pixels change
        if (FAILED(bmp->CopyFromMemory(nullptr, argb, pitch)))
            return;
    }
    rt(target_)->DrawBitmap(bmp,
                            D2D1::RectF(static_cast<float>(dst.x), static_cast<float>(dst.y),
                                        static_cast<float>(dst.right()), static_cast<float>(dst.bottom())),
                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
}

void Canvas::line(i32 x0, i32 y0, i32 x1, i32 y1, u32 color, float width) {
    rt(target_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    set_color(color);
    rt(target_)->DrawLine(D2D1::Point2F(static_cast<float>(x0) + 0.5f, static_cast<float>(y0) + 0.5f),
                          D2D1::Point2F(static_cast<float>(x1) + 0.5f, static_cast<float>(y1) + 0.5f),
                          static_cast<ID2D1Brush*>(brush_), width);
}

void Canvas::polyline(const float* xy, u32 points, u32 color, float width) {
    if (points < 2)
        return;
    rt(target_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    set_color(color);
    for (u32 i = 1; i < points; ++i)
        rt(target_)->DrawLine(D2D1::Point2F(xy[2 * i - 2], xy[2 * i - 1]), D2D1::Point2F(xy[2 * i], xy[2 * i + 1]),
                              static_cast<ID2D1Brush*>(brush_), width);
}

void Canvas::text(i32 x, i32 y, const char* s, u32 font, u32 color, u32 align, i32 max_w) {
    WCHAR buf[512];
    const u32 n = widen(s, buf, 512);
    if (!n)
        return;
    IDWriteTextFormat* fmt = static_cast<IDWriteTextFormat*>(fonts_[font]);
    const float span = max_w > 0 ? static_cast<float>(max_w) : 10000.0f;
    float left = static_cast<float>(x);
    if (align == kAlignCenter) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        left -= span / 2;
    } else if (align == kAlignRight) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        left -= span;
    } else {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    set_color(color);
    rt(target_)->DrawText(buf, n, fmt,
                           D2D1::RectF(left, static_cast<float>(y), left + span,
                                       static_cast<float>(y + line_h_[font])),
                           static_cast<ID2D1Brush*>(brush_), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

i32 Canvas::text_w(const char* s, u32 font) {
    WCHAR buf[512];
    const u32 n = widen(s, buf, 512);
    if (!n)
        return 0;
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(static_cast<IDWriteFactory*>(dwrite_)->CreateTextLayout(
            buf, n, static_cast<IDWriteTextFormat*>(fonts_[font]), 10000.0f, 1000.0f, &lay)))
        return 0;
    DWRITE_TEXT_METRICS m = {};
    lay->GetMetrics(&m);
    lay->Release();
    return static_cast<i32>(ceilf(m.widthIncludingTrailingWhitespace));
}

i32 Canvas::line_h(u32 font) { return line_h_[font]; }

bool Canvas::read_pixels(Array<u32>* out) const {
    if (!bitmap_)
        return false;
    IWICBitmapLock* lock = nullptr;
    WICRect rc = {0, 0, w_, h_};
    if (FAILED(static_cast<IWICBitmap*>(bitmap_)->Lock(&rc, WICBitmapLockRead, &lock)))
        return false;
    UINT size = 0, stride = 0;
    BYTE* data = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&size, &data);
    out->resize(0);
    out->resize(static_cast<usize>(w_) * h_);
    for (i32 y = 0; y < h_; ++y) {
        const u32* row = reinterpret_cast<const u32*>(data + static_cast<usize>(y) * stride);
        for (i32 x = 0; x < w_; ++x)
            (*out)[static_cast<usize>(y) * w_ + x] = row[x] & 0x00FFFFFF;
    }
    lock->Release();
    return true;
}

bool Canvas::write_png(const char* path) const {
    if (!bitmap_ || !wic_)
        return false;
    WCHAR wpath[MAX_PATH];
    widen(path, wpath, MAX_PATH);
    IWICImagingFactory* f = static_cast<IWICImagingFactory*>(wic_);
    void* stream = nullptr;
    void* enc = nullptr;
    void* frame = nullptr;
    void* props = nullptr;
    bool ok = false;
    IWICStream* s = nullptr;
    IWICBitmapEncoder* e = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    IPropertyBag2* pb = nullptr;
    if (SUCCEEDED(f->CreateStream(&s))) {
        stream = s;
        if (SUCCEEDED(s->InitializeFromFilename(wpath, GENERIC_WRITE)) &&
            SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e))) {
            enc = e;
            if (SUCCEEDED(e->Initialize(s, WICBitmapEncoderNoCache)) && SUCCEEDED(e->CreateNewFrame(&fr, &pb))) {
                frame = fr;
                props = pb;
                WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                ok = SUCCEEDED(fr->Initialize(pb)) && SUCCEEDED(fr->SetSize(static_cast<UINT>(w_), static_cast<UINT>(h_))) &&
                     SUCCEEDED(fr->SetPixelFormat(&fmt)) &&
                     SUCCEEDED(fr->WriteSource(static_cast<IWICBitmap*>(bitmap_), nullptr)) &&
                     SUCCEEDED(fr->Commit()) && SUCCEEDED(e->Commit());
            }
        }
    }
    release(props);
    release(frame);
    release(enc);
    release(stream);
    return ok;
}

bool Canvas::load_png(const char* path, Array<u32>* out, i32* w, i32* h) const {
    if (!wic_)
        return false;
    WCHAR wpath[MAX_PATH];
    widen(path, wpath, MAX_PATH);
    IWICImagingFactory* f = static_cast<IWICImagingFactory*>(wic_);
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(f->CreateDecoderFromFilename(wpath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    void* d = dec;
    void* fr = nullptr;
    void* cv = nullptr;
    bool ok = false;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(dec->GetFrame(0, &frame))) {
        fr = frame;
        if (SUCCEEDED(f->CreateFormatConverter(&conv))) {
            cv = conv;
            UINT cw = 0, ch = 0;
            if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(conv->GetSize(&cw, &ch))) {
                out->resize(0);
                out->resize(static_cast<usize>(cw) * ch);
                ok = SUCCEEDED(conv->CopyPixels(nullptr, cw * 4, cw * ch * 4, reinterpret_cast<BYTE*>(out->data())));
                for (usize i = 0; i < out->size(); ++i)
                    (*out)[i] &= 0x00FFFFFF;
                *w = static_cast<i32>(cw);
                *h = static_cast<i32>(ch);
            }
        }
    }
    release(cv);
    release(fr);
    release(d);
    return ok;
}

} // namespace nova2
