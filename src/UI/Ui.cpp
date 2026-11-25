#include "Ui.h"
#include <algorithm>

namespace helix::ui {

void UiContext::BeginFrame(float screenW, float screenH) {
    screenW_ = screenW;
    screenH_ = screenH;
    hitRegions_.clear();
    buckets_.clear();
    clickedId_ = 0;
}

void UiContext::EmitQuad(UINT texIndex, float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1, const float rgba[4]) {
    auto& v = buckets_[texIndex];
    v.push_back({ x,     y,     u0, v0, rgba[0], rgba[1], rgba[2], rgba[3] });
    v.push_back({ x + w, y,     u1, v0, rgba[0], rgba[1], rgba[2], rgba[3] });
    v.push_back({ x,     y + h, u0, v1, rgba[0], rgba[1], rgba[2], rgba[3] });
    v.push_back({ x + w, y,     u1, v0, rgba[0], rgba[1], rgba[2], rgba[3] });
    v.push_back({ x + w, y + h, u1, v1, rgba[0], rgba[1], rgba[2], rgba[3] });
    v.push_back({ x,     y + h, u0, v1, rgba[0], rgba[1], rgba[2], rgba[3] });
}

void UiContext::Rect(float x, float y, float w, float h, const float rgba[4]) {
    // Solid white texture is uploaded by the app as a known slot; the
    // caller passes it via SetSolidTexture (stored in a static below is
    // avoided — instead we use texture index 0 reserved for "white").
    EmitQuad(solidTextureIndex_, x, y, w, h, 0.f, 0.f, 1.f, 1.f, rgba);
}

void UiContext::Panel(float x, float y, float w, float h,
                      const float rgba[4], const float borderRgba[4]) {
    Rect(x, y, w, h, borderRgba);
    Rect(x + 2.f, y + 2.f, w - 4.f, h - 4.f, rgba);
}

float UiContext::Text(const FontAtlas& font, float x, float y, float heightPx,
                      const std::string& utf8, const float rgba[4], int align,
                      float boxWidth) {
    float w = font.MeasureWidth(utf8, heightPx);
    float tx = x;
    if (align == 1 && boxWidth >= 0.f) tx = x + (boxWidth - w) * 0.5f;
    else if (align == 2 && boxWidth >= 0.f) tx = x + boxWidth - w;
    auto& v = buckets_[font.TextureIndex()];
    size_t before = v.size();
    font.AppendText(utf8, tx, y, heightPx, rgba, v);
    return w;
}

void UiContext::Image(UINT textureIndex, float x, float y, float w, float h,
                      const float rgba[4]) {
    static const float white[4] = { 1.f, 1.f, 1.f, 1.f };
    EmitQuad(textureIndex, x, y, w, h, 0.f, 0.f, 1.f, 1.f, rgba ? rgba : white);
}

void UiContext::ImageRegion(UINT textureIndex, float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1, const float rgba[4]) {
    EmitQuad(textureIndex, x, y, w, h, u0, v0, u1, v1, rgba);
}

int UiContext::Button(const FontAtlas& font, int id, float x, float y, float w, float h,
                      const std::string& label, bool hovered, bool active) {
    static const float idleBg[4]   = { 0.10f, 0.14f, 0.22f, 0.92f };
    static const float hoverBg[4]  = { 0.16f, 0.24f, 0.38f, 0.95f };
    static const float activeBg[4] = { 0.22f, 0.34f, 0.55f, 0.98f };
    static const float idleBr[4]   = { 0.35f, 0.55f, 0.85f, 0.9f };
    static const float hoverBr[4]  = { 0.55f, 0.75f, 1.0f, 1.0f };
    static const float txtCol[4]   = { 0.92f, 0.95f, 1.0f, 1.0f };

    const float* bg = active ? activeBg : (hovered ? hoverBg : idleBg);
    const float* br = hovered || active ? hoverBr : idleBr;
    Panel(x, y, w, h, bg, br);
    float th = h * 0.42f;
    float ty = y + (h - th) * 0.5f - th * 0.08f;
    Text(font, x, ty, th, label, txtCol, 1, w);
    hitRegions_.push_back({ id, { x, y, w, h } });
    return id;
}

void UiContext::Checkbox(const FontAtlas& font, int id, float x, float y, float size,
                         const std::string& label, bool checked, bool& outChecked) {
    static const float boxBg[4]  = { 0.08f, 0.10f, 0.16f, 0.95f };
    static const float boxBr[4]  = { 0.40f, 0.60f, 0.90f, 0.9f };
    static const float chkCol[4] = { 0.45f, 0.85f, 1.0f, 1.0f };
    static const float txtCol[4] = { 0.90f, 0.93f, 0.97f, 1.0f };

    Panel(x, y, size, size, boxBg, boxBr);
    if (checked) Rect(x + size * 0.2f, y + size * 0.2f, size * 0.6f, size * 0.6f, chkCol);
    float th = size * 0.8f;
    Text(font, x + size + 10.f, y - size * 0.1f, th, label, txtCol, 0);
    UiRect hit{ x, y - size * 0.1f, size + 10.f + font.MeasureWidth(label, th), size * 1.2f };
    hitRegions_.push_back({ id, hit });
    outChecked = checked;
}

int UiContext::HitTest(float mx, float my) const {
    // Last registered region wins (topmost = drawn last).
    for (auto it = hitRegions_.rbegin(); it != hitRegions_.rend(); ++it)
        if (it->rect.Contains(mx, my)) return it->id;
    return 0;
}

void UiContext::OnMouseMove(float x, float y) {
    mouseX_ = x; mouseY_ = y;
}

void UiContext::OnMouseDown(float x, float y, int button) {
    mouseX_ = x; mouseY_ = y;
    if (button >= 0 && button < 3) mouseDown_[button] = true;
    if (button == 0) clickedId_ = HitTest(x, y);
}

void UiContext::OnMouseUp(float x, float y, int button) {
    mouseX_ = x; mouseY_ = y;
    if (button >= 0 && button < 3) mouseDown_[button] = false;
}

void UiContext::OnChar(wchar_t ch) {
    // Printable ASCII + space goes into the focused text buffer.
    if (textFocusId_ != 0 && ch >= 32 && ch < 127) {
        if (textBuffer_.size() < 16) textBuffer_ += static_cast<char>(ch);
    } else if (textFocusId_ != 0 && ch == 8) { // backspace
        if (!textBuffer_.empty()) textBuffer_.pop_back();
    }
}

void UiContext::OnKey(int vk, bool down) {
    if (!down) return;
    if (textFocusId_ != 0 && vk == VK_RETURN) {
        submitRequested_ = true;
    } else if (textFocusId_ != 0 && vk == VK_ESCAPE) {
        textFocusId_ = 0;
    }
}

void UiContext::Render(helix::renderer::D3D12Renderer& renderer) const {
    // Flatten buckets into one vertex buffer — buckets_ is a std::map keyed
    // by texture index, so slot 0 (white panels) draws before the font atlas.
    std::vector<helix::renderer::UiVertex> all;
    std::vector<helix::renderer::D3D12Renderer::UiDrawRange> ranges;
    all.reserve(8192);
    ranges.reserve(buckets_.size());
    for (auto& [tex, verts] : buckets_) {
        if (verts.empty()) continue;
        ranges.push_back({ tex, static_cast<UINT>(all.size()), static_cast<UINT>(verts.size()) });
        all.insert(all.end(), verts.begin(), verts.end());
    }
    renderer.DrawUiGeometry(all, ranges);
}

} // namespace helix::ui
