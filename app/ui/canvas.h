#pragma once

// The canvas: the viewer's drawing layer (design.md 2, R23;
// build-plan-gate1.md step 12). Nova 1's interface, drawn with Direct2D:
//   - on screen into a window's render target, with DirectWrite's text;
//   - off screen into a WIC bitmap, software-rendered, so a headless render
//     gives the same pixels every time, to read back or write as a PNG.
// Coordinates are pixels. Rectangles are drawn crisp; lines and polylines
// antialiased (the reason for Direct2D). windows.h and the COM headers stay
// out of this header.
#include "nova2/array.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

enum FontId : u32 { kFontUi, kFontBold, kFontMono, kFontSmall, kFontCount };
enum Align : u32 { kAlignLeft, kAlignCenter, kAlignRight };

struct Rect {
    i32 x = 0, y = 0, w = 0, h = 0;
    bool contains(i32 px, i32 py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    i32 right() const { return x + w; }
    i32 bottom() const { return y + h; }
};

class Canvas {
  public:
    Canvas() = default;
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // Creates the Direct2D, DirectWrite and WIC factories (COM must be
    // initialised on this thread first).
    bool init(TextBuf* why);

    // A frame into a window (an HWND), or into an off-screen bitmap. `scale`
    // is the DPI scale (1 at 96 dpi): fonts follow it.
    bool begin_window(void* hwnd, i32 w, i32 h, float scale);
    bool begin_bitmap(i32 w, i32 h, float scale);
    void end();

    // After an off-screen frame: its pixels as 0x00RRGGBB, row by row from
    // the top; or written as a PNG.
    bool read_pixels(Array<u32>* out) const;
    bool write_png(const char* path) const;
    // A PNG's pixels (for comparing a render with a stored one).
    bool load_png(const char* path, Array<u32>* out, i32* w, i32* h) const;

    i32 width() const { return w_; }
    i32 height() const { return h_; }
    float scale() const { return scale_; }
    i32 px(float v) const { return static_cast<i32>(v * scale_ + 0.5f); }

    void clip(const Rect& r); // replaces the clip
    void unclip();
    void fill(const Rect& r, u32 color);
    void frame(const Rect& r, u32 color); // a 1-pixel outline
    // A grid of w by h colours (0x00RRGGBB, row by row from the top), each
    // drawn as a square of `cell` pixels, the first at (x, y): one call for
    // what would be w * h fills. Each of kImageSlots slots keeps its picture
    // between frames: `same` says the colours are those last drawn in the
    // slot, so they needn't be sent again (they are, if the slot has lost
    // its picture or the size has changed).
    static constexpr u32 kImageSlots = 2;
    void image(i32 x, i32 y, const u32* rgb, i32 w, i32 h, i32 cell, u32 slot = 0, bool same = false);
    // A grid of w by h colours with their opacity (0xAARRGGBB, each colour
    // already multiplied by its opacity), stretched smoothly over dst and
    // laid over what is drawn there: a shading.
    void overlay(const Rect& dst, const u32* argb, i32 w, i32 h);
    void line(i32 x0, i32 y0, i32 x1, i32 y1, u32 color, float width = 1);
    void polyline(const float* xy, u32 points, u32 color, float width = 1.5f);
    // Text whose top-left (or top-centre, top-right) is at (x, y), cut at
    // max_w pixels if given.
    void text(i32 x, i32 y, const char* s, u32 font, u32 color, u32 align = kAlignLeft, i32 max_w = 0);
    i32 text_w(const char* s, u32 font);
    i32 line_h(u32 font);

  private:
    bool make_fonts();
    void free_fonts();
    void set_color(u32 color);
    bool make_brush();

    void* d2d_ = nullptr;         // ID2D1Factory
    void* dwrite_ = nullptr;      // IDWriteFactory
    void* wic_ = nullptr;         // IWICImagingFactory
    void* target_ = nullptr;      // ID2D1RenderTarget: the current one
    void* hwnd_target_ = nullptr; // ID2D1HwndRenderTarget
    void* hwnd_ = nullptr;
    void* bmp_target_ = nullptr;  // ID2D1RenderTarget on a WIC bitmap
    void* bitmap_ = nullptr;      // IWICBitmap
    void* brush_ = nullptr;       // ID2D1SolidColorBrush, on target_
    void* brush_target_ = nullptr;
    void release_images();
    void* overlay_ = nullptr; // ID2D1Bitmap, premultiplied, on overlay_target_: overlay()'s
    void* overlay_target_ = nullptr;
    i32 overlay_w_ = 0, overlay_h_ = 0;
    void* image_[kImageSlots] = {}; // ID2D1Bitmap, on image_target_: image()'s pixels, a slot each
    void* image_target_[kImageSlots] = {};
    i32 image_w_[kImageSlots] = {}, image_h_[kImageSlots] = {};
    void* fonts_[kFontCount] = {}; // IDWriteTextFormat
    float font_scale_ = 0;
    i32 line_h_[kFontCount] = {};
    i32 w_ = 0, h_ = 0;
    float scale_ = 1;
    bool clipped_ = false;
    bool drawing_ = false;
};

} // namespace nova2
