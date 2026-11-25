#include "FontAtlas.h"
#include <windows.h>
#include <cstring>
#include <algorithm>

namespace helix::ui {

namespace {
constexpr int kAtlasSize = 1024;
// Horizontal pad must clear glyphs whose ink extends past their advance
// ('j' hook reaches ~4px left of origin) — 2px let it bleed into the
// previous cell, producing stray pixels under neighboring letters.
constexpr int kGlyphPad = 6; // AA bleed padding around each cell

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}
} // namespace

bool FontAtlas::Build(const wchar_t* fontName, int emHeightPx) {
    emHeightPx_ = emHeightPx;
    atlasWidth_ = kAtlasSize;
    atlasHeight_ = kAtlasSize;
    coverage_.assign(static_cast<size_t>(atlasWidth_) * atlasHeight_, 0);

    // --- GDI setup: 32bpp top-down DIB to draw glyphs into ---
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = atlasWidth_;
    bmi.bmiHeader.biHeight = -atlasHeight_; // negative = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* dibBits = nullptr;
    HBITMAP dib = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDc, dib);
    RECT full{ 0, 0, atlasWidth_, atlasHeight_ };
    FillRect(memDc, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(memDc, TRANSPARENT);
    SetTextColor(memDc, RGB(255, 255, 255));

    HFONT font = CreateFontW(-emHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontName);
    if (!font) font = CreateFontW(-emHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HGDIOBJ oldFont = SelectObject(memDc, font);

    TEXTMETRICW tm{};
    GetTextMetricsW(memDc, &tm);
    // Vertical padding must clear descenders (g,j,p,q,y). tmHeight already
    // includes internal leading — do NOT add tmInternalLeading again or the
    // glyph is pushed down and descenders overflow into the next row's cell
    // (that overflow was the ghost pixels above button labels).
    const int cellPadY = 10;
    const int cellH = tm.tmHeight + cellPadY * 2;

    // --- Pack glyphs 32..126 into the atlas with shelf packing ---
    // NOTE: GetCharABCWidthsW's abcA is unreliable on modern fonts (returns
    // ~0 for Segoe UI) — GetTextExtentPoint32W gives the true advance.
    int cursorX = kGlyphPad, cursorY = kGlyphPad, rowH = 0;
    struct Placement { wchar_t c; RECT cell; int advance; };
    std::vector<Placement> placements;

    for (wchar_t c = 32; c <= 126; ++c) {
        SIZE ext{};
        if (!GetTextExtentPoint32W(memDc, &c, 1, &ext)) continue;
        int advance = ext.cx > 1 ? static_cast<int>(ext.cx) : 1;
        int cellW = advance + kGlyphPad * 2;
        if (cursorX + cellW > atlasWidth_ - kGlyphPad) {
            cursorX = kGlyphPad;
            cursorY += rowH;
            rowH = 0;
        }
        if (cursorY + cellH > atlasHeight_) break; // atlas full — shouldn't happen at 1024
        RECT cell{ cursorX, cursorY, cursorX + cellW, cursorY + cellH };
        placements.push_back({ c, cell, advance });
        // Draw the glyph into its cell. TextOut origin is the top-left of
        // the line box; internal leading keeps ascenders/descenders consistent.
        wchar_t ch[2] = { c, L'\0' };
        TextOutW(memDc, cursorX + kGlyphPad, cursorY + cellPadY, ch, 1);
        cursorX += cellW;
        rowH = std::max(rowH, cellH);
    }

    // --- Extract coverage: white text on black → R8 alpha ---
    BITMAPINFO bmi32{};
    bmi32.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi32.bmiHeader.biWidth = atlasWidth_;
    bmi32.bmiHeader.biHeight = -atlasHeight_; // top-down, matching the DIB
    bmi32.bmiHeader.biPlanes = 1;
    bmi32.bmiHeader.biBitCount = 32;
    bmi32.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(atlasWidth_ * atlasHeight_ * 4);
    GetDIBits(memDc, dib, 0, atlasHeight_, pixels.data(), &bmi32, DIB_RGB_COLORS);

    for (int y = 0; y < atlasHeight_; ++y) {
        for (int x = 0; x < atlasWidth_; ++x) {
            const BYTE* p = pixels.data() + (static_cast<size_t>(y) * atlasWidth_ + x) * 4;
            // BGRA byte order in DIB; take max channel as coverage
            coverage_[static_cast<size_t>(y) * atlasWidth_ + x] =
                static_cast<uint8_t>(std::max({ p[0], p[1], p[2] }));
        }
    }

    // --- Despeckle: remove isolated coverage pixels (GDI dust / AA flecks).
    // A pixel survives only if it has >=2 neighbors above threshold in its
    // 8-connected ring — real strokes always qualify, lone specks don't.
    {
        std::vector<uint8_t> src = coverage_;
        for (int y = 1; y < atlasHeight_ - 1; ++y) {
            for (int x = 1; x < atlasWidth_ - 1; ++x) {
                size_t i = static_cast<size_t>(y) * atlasWidth_ + x;
                if (src[i] <= 40) { coverage_[i] = 0; continue; }
                int n = 0;
                for (int dy = -1; dy <= 1 && n < 2; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        if (src[static_cast<size_t>(y + dy) * atlasWidth_ + (x + dx)] > 40) ++n;
                    }
                if (n < 2) coverage_[i] = 0;
            }
        }
    }

    // --- Build glyph metric table: tight ink bounding boxes ---
    for (auto& p : placements) {
        Glyph g;
        g.advance = static_cast<float>(p.advance);

        // Scan the cell for ink bounds (coverage > 40 — high enough to ignore
        // stray AA specks/GDI dust that render as ghost pixels, low enough to
        // keep real glyph edges), inflated by 1px for AA.
        int x0 = p.cell.right, y0 = p.cell.bottom, x1 = p.cell.left, y1 = p.cell.top;
        for (int y = p.cell.top; y < p.cell.bottom; ++y)
            for (int x = p.cell.left; x < p.cell.right; ++x)
                if (coverage_[static_cast<size_t>(y) * atlasWidth_ + x] > 40) {
                    if (x < x0) x0 = x; if (x > x1) x1 = x;
                    if (y < y0) y0 = y; if (y > y1) y1 = y;
                }
        if (x1 >= x0 && y1 >= y0) {
            g.hasInk = true;
            x0 = std::max(static_cast<int>(p.cell.left), x0 - 1);
            y0 = std::max(static_cast<int>(p.cell.top), y0 - 1);
            x1 = std::min(static_cast<int>(p.cell.right) - 1, x1 + 1);
            y1 = std::min(static_cast<int>(p.cell.bottom) - 1, y1 + 1);
            g.inkX0 = static_cast<float>(x0 - p.cell.left);
            g.inkY0 = static_cast<float>(y0 - p.cell.top);
            g.inkWPx = static_cast<float>(x1 - x0 + 1);
            g.inkHPx = static_cast<float>(y1 - y0 + 1);
            // Inset UVs by half a texel: linear filtering at quad edges
            // otherwise blends in neighboring glyph coverage (stray pixels).
            g.u0 = static_cast<float>(x0 + 0.5f) / atlasWidth_;
            g.v0 = static_cast<float>(y0 + 0.5f) / atlasHeight_;
            g.u1 = static_cast<float>(x1 + 0.5f) / atlasWidth_;
            g.v1 = static_cast<float>(y1 + 0.5f) / atlasHeight_;
        }
        glyphs_[p.c] = g;
    }
    // Fallback for unmapped chars
    auto it = glyphs_.find(L'?');
    unknownGlyph_ = (it != glyphs_.end()) ? it->second : Glyph{};

    // Cell height for layout = tmHeight; store as em height reference
    // (emHeightPx_ is the requested em; tm.tmHeight is actual cell px).
    // We keep emHeightPx_ as the design em and scale advance/uvs by
    // heightPx/emHeightPx_.

    SelectObject(memDc, oldFont);
    DeleteObject(font);
    SelectObject(memDc, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return true;
}

bool FontAtlas::DumpPpm(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", atlasWidth_, atlasHeight_);
    for (size_t i = 0; i < coverage_.size(); ++i) {
        uint8_t v = coverage_[i];
        fputc(v, f); fputc(v, f); fputc(v, f);
    }
    fclose(f);
    return true;
}

bool FontAtlas::DebugGlyph(char c, float& advance, float& inkX0, float& inkY0,
                           float& inkW, float& inkH) const {
    auto it = glyphs_.find(static_cast<wchar_t>(c));
    if (it == glyphs_.end()) return false;
    const Glyph& g = it->second;
    advance = g.advance; inkX0 = g.inkX0; inkY0 = g.inkY0;
    inkW = g.inkWPx; inkH = g.inkHPx;
    return true;
}

bool FontAtlas::DebugInkRows(char c, std::vector<int>& rowCounts, int& cellTop,
                             int& cellH) const {
    // Recover the cell rect from UVs: ink bbox is a sub-rect of the cell.
    // We don't store the cell, so approximate: scan a window around the ink
    // UV region for ink rows. Good enough for debugging bleed.
    auto it = glyphs_.find(static_cast<wchar_t>(c));
    if (it == glyphs_.end()) return false;
    const Glyph& g = it->second;
    int x0 = static_cast<int>(g.u0 * atlasWidth_);
    int x1 = static_cast<int>(g.u1 * atlasWidth_);
    int y0 = static_cast<int>(g.v0 * atlasHeight_);
    int y1 = static_cast<int>(g.v1 * atlasHeight_);
    cellTop = y0; cellH = y1 - y0;
    rowCounts.assign(cellH, 0);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (coverage_[static_cast<size_t>(y) * atlasWidth_ + x] > 40)
                ++rowCounts[y - y0];
    return true;
}

void FontAtlas::UploadTo(helix::renderer::D3D12Renderer& renderer) {
    // Expand R8 coverage to RGBA (white RGB, coverage as alpha) so the UI
    // shader can modulate vertex color x texture for rects, glyphs and
    // images with one code path.
    std::vector<uint8_t> rgba(coverage_.size() * 4);
    for (size_t i = 0; i < coverage_.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage_[i];
    }
    textureIndex_ = renderer.CreateUiTexture(atlasWidth_, atlasHeight_,
                                             DXGI_FORMAT_R8G8B8A8_UNORM, rgba.data());
}

float FontAtlas::MeasureWidth(const std::string& utf8, float heightPx) const {
    std::wstring w = Utf8ToWide(utf8);
    float scale = heightPx / static_cast<float>(emHeightPx_);
    float pen = 0.f;
    for (wchar_t c : w) {
        auto it = glyphs_.find(c);
        const Glyph& g = (it != glyphs_.end()) ? it->second : unknownGlyph_;
        pen += g.advance * scale;
    }
    return pen;
}

void FontAtlas::MeasureInkExtent(const std::string& utf8, float heightPx,
                                 float& topPx, float& heightPxOut) const {
    std::wstring w = Utf8ToWide(utf8);
    float scale = heightPx / static_cast<float>(emHeightPx_);
    float top = 1e30f, bot = -1e30f;
    for (wchar_t c : w) {
        auto it = glyphs_.find(c);
        const Glyph& g = (it != glyphs_.end()) ? it->second : unknownGlyph_;
        if (!g.hasInk) continue;
        top = std::min(top, g.inkY0 * scale);
        bot = std::max(bot, (g.inkY0 + g.inkHPx) * scale);
    }
    if (top > bot) { topPx = 0.f; heightPxOut = heightPx; return; }
    topPx = top;
    heightPxOut = bot - top;
}

float FontAtlas::AppendText(const std::string& utf8, float x, float y, float heightPx,
                            const float rgba[4], std::vector<helix::renderer::UiVertex>& out) const {
    std::wstring w = Utf8ToWide(utf8);
    float scale = heightPx / static_cast<float>(emHeightPx_);
    float penX = x;
    const float startX = x;

    for (wchar_t c : w) {
        auto it = glyphs_.find(c);
        const Glyph& g = (it != glyphs_.end()) ? it->second : unknownGlyph_;
        if (g.hasInk) {
            // Quad wraps the glyph's ink bbox exactly — positioned relative
            // to the pen origin (cell left = penX, cell top = y).
            float qx = penX + g.inkX0 * scale;
            float qy = y + g.inkY0 * scale;
            float qw = g.inkWPx * scale;
            float qh = g.inkHPx * scale;
            out.push_back({ qx,      qy,      g.u0, g.v0, rgba[0], rgba[1], rgba[2], rgba[3] });
            out.push_back({ qx + qw, qy,      g.u1, g.v0, rgba[0], rgba[1], rgba[2], rgba[3] });
            out.push_back({ qx,      qy + qh, g.u0, g.v1, rgba[0], rgba[1], rgba[2], rgba[3] });
            out.push_back({ qx + qw, qy,      g.u1, g.v0, rgba[0], rgba[1], rgba[2], rgba[3] });
            out.push_back({ qx + qw, qy + qh, g.u1, g.v1, rgba[0], rgba[1], rgba[2], rgba[3] });
            out.push_back({ qx,      qy + qh, g.u0, g.v1, rgba[0], rgba[1], rgba[2], rgba[3] });
        }
        penX += g.advance * scale;
    }
    return penX - startX;
}

} // namespace helix::ui
