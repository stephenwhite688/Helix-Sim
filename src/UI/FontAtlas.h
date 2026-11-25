#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "../Renderer/D3D12Renderer.h"

namespace helix::ui {

// Rasterizes an ASCII font into a single-channel (R8) coverage atlas at
// startup using GDI — no external font library needed. Glyphs 32..126 are
// packed into a 1024x1024 atlas; AppendText turns strings into textured
// UiVertex quads for D3D12Renderer::DrawUiQuads.
//
// Coordinates: AppendText takes a pixel position for the TOP-LEFT of the
// line box and a desired em height in pixels; the renderer converts to
// NDC internally.
class FontAtlas {
public:
    bool Build(const wchar_t* fontName, int emHeightPx);

    int EmHeightPx() const { return emHeightPx_; }
    int TextureIndex() const { return textureIndex_; }
    bool IsReady() const { return textureIndex_ != UINT_MAX; }

    // Appends quad vertices for `text` (UTF-8) with its line-box top-left
    // at (x, y). `heightPx` is the em height on screen. Returns the width
    // the text occupied (for centering/right-alignment).
    float AppendText(const std::string& utf8, float x, float y, float heightPx,
                     const float rgba[4], std::vector<helix::renderer::UiVertex>& out) const;
    float MeasureWidth(const std::string& utf8, float heightPx) const;
    // Ink bounding box of a string relative to the AppendText line-box top:
    // topPx = distance from line-box top to first ink pixel,
    // heightPxOut = total ink height. Use to vertically center text.
    void MeasureInkExtent(const std::string& utf8, float heightPx,
                          float& topPx, float& heightPxOut) const;

    void UploadTo(helix::renderer::D3D12Renderer& renderer);

    // Debug: writes the coverage atlas as a binary PPM (P6) for inspection.
    bool DumpPpm(const char* path) const;
    // Debug: ink metrics for one ASCII char (advance, ink offset/size in
    // cell pixels). Returns false if unmapped.
    bool DebugGlyph(char c, float& advance, float& inkX0, float& inkY0,
                    float& inkW, float& inkH) const;
    // Debug: dump per-row ink counts inside a glyph's cell (for bleed checks).
    bool DebugInkRows(char c, std::vector<int>& rowCounts, int& cellTop,
                      int& cellH) const;

private:
    struct Glyph {
        // UVs of the glyph's INK bounding box (tight — no cell padding),
        // normalized to atlas size.
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        // Ink bbox position/size in pixels relative to the CELL's top-left
        // (cell = advance+pad wide, tmHeight+pad tall). Used to place the
        // screen quad exactly over the ink.
        float inkX0 = 0, inkY0 = 0, inkWPx = 0, inkHPx = 0;
        float advance = 0;                     // advance width in atlas pixels
        bool hasInk = false;
    };

    int emHeightPx_ = 0;
    int atlasWidth_ = 0, atlasHeight_ = 0;
    std::vector<uint8_t> coverage_;           // R8 atlas pixels
    std::unordered_map<wchar_t, Glyph> glyphs_;
    Glyph unknownGlyph_;
    UINT textureIndex_ = UINT_MAX;
};

} // namespace helix::ui
