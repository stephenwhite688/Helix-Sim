#pragma once
#include <string>
#include <vector>
#include <map>
#include "FontAtlas.h"
#include "../Renderer/D3D12Renderer.h"

namespace helix::ui {

// One retained-mode UI layer shared by splash, menus, MFDs and the HUD
// (SPEC §3.5/§3.6). Widgets emit textured quads into per-texture buckets;
// hit regions are registered during the same pass so clicks resolve
// against exactly what was drawn this frame.
//
// Usage per frame:
//   ctx.BeginFrame(w, h);
//   screen->Draw(ctx);              // widgets append quads + hit regions
//   ctx.Render(renderer);           // issues DrawUiQuads per texture
// Hit regions from this frame are queryable via HitTest/ClickedId.

struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct UiHitRegion {
    int id = 0;
    UiRect rect;
};

class UiContext {
public:
    void BeginFrame(float screenW, float screenH);

    // The app uploads a 1x1 white texture and registers its slot here;
    // Rect/Panel/Image use it for tinted fills.
    void SetSolidTextureIndex(UINT index) { solidTextureIndex_ = index; }
    UINT SolidTextureIndex() const { return solidTextureIndex_; }

    // --- drawing ---
    // Filled rectangle on a solid-white UI texture (tinted by color).
    void Rect(float x, float y, float w, float h, const float rgba[4]);
    void Rect(float x, float y, float w, float h, float r, float g, float b, float a) {
        const float c[4] = { r, g, b, a };
        Rect(x, y, w, h, c);
    }
    // Rounded-ish panel: outer dark rect + inner accent border (two rects).
    void Panel(float x, float y, float w, float h, const float rgba[4], const float borderRgba[4]);
    // Text via the font atlas. align: 0=left, 1=center, 2=right (of the box).
    float Text(const FontAtlas& font, float x, float y, float heightPx,
               const std::string& utf8, const float rgba[4], int align = 0,
               float boxWidth = -1.f);
    // Textured image quad (logo, art). uv in 0..1.
    void Image(UINT textureIndex, float x, float y, float w, float h,
               const float rgba[4] = nullptr);
    void ImageRegion(UINT textureIndex, float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1, const float rgba[4]);

    // --- widgets (draw + register hit region) ---
    // Returns the widget id; also records it for hit-testing this frame.
    int Button(const FontAtlas& font, int id, float x, float y, float w, float h,
               const std::string& label, bool hovered, bool active = false);
    // Simple checkbox row; returns toggled state via out param.
    void Checkbox(const FontAtlas& font, int id, float x, float y, float size,
                  const std::string& label, bool checked, bool& outChecked);

    // Registers a hit region without drawing (custom-drawn widgets).
    void HitRegion(int id, float x, float y, float w, float h) {
        hitRegions_.push_back({ id, { x, y, w, h } });
    }

    // --- input/query ---
    bool MouseIn(const UiRect& r, float mx, float my) const { return r.Contains(mx, my); }
    int HitTest(float mx, float my) const;          // topmost region id or 0
    int ClickedId() const { return clickedId_; }    // set by OnMouseDown
    void OnMouseMove(float x, float y);
    void OnMouseDown(float x, float y, int button);
    void OnMouseUp(float x, float y, int button);
    void OnChar(wchar_t ch);
    void OnKey(int vk, bool down);
    float MouseX() const { return mouseX_; }
    float MouseY() const { return mouseY_; }
    bool MouseDown(int button = 0) const { return mouseDown_[button]; }

    // Text input buffer shared by name-entry-style fields.
    std::string& TextBuffer() { return textBuffer_; }
    void SetTextFocusId(int id) { textFocusId_ = id; }
    int TextFocusId() const { return textFocusId_; }
    // True for one frame after Enter is pressed while a text field is
    // focused. Consume via TakeSubmit().
    bool TakeSubmit() { bool v = submitRequested_; submitRequested_ = false; return v; }

    void Render(helix::renderer::D3D12Renderer& renderer) const;

    float ScreenW() const { return screenW_; }
    float ScreenH() const { return screenH_; }

private:
    void EmitQuad(UINT texIndex, float x, float y, float w, float h,
                  float u0, float v0, float u1, float v1, const float rgba[4]);

    float screenW_ = 0, screenH_ = 0;
    UINT solidTextureIndex_ = 0;
    float mouseX_ = 0, mouseY_ = 0;
    bool mouseDown_[3] = {};
    int clickedId_ = 0;
    int nextId_ = 1;
    int textFocusId_ = 0;
    bool submitRequested_ = false;
    std::string textBuffer_;

    std::vector<UiHitRegion> hitRegions_;
    // std::map (ordered by texture index) so draw order is deterministic:
    // slot 0 = white panels first, font atlas text last.
    std::map<UINT, std::vector<helix::renderer::UiVertex>> buckets_;
};

} // namespace helix::ui
