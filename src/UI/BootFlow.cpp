#include "BootFlow.h"
#include "Log.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace helix::ui {

namespace {
// HelixSim theme — deep space navy + ice-cyan accents.
const float kBg[4]        = { 0.025f, 0.035f, 0.070f, 1.0f };
const float kPanel[4]     = { 0.055f, 0.075f, 0.130f, 0.96f };
const float kPanelEdge[4] = { 0.20f, 0.38f, 0.60f, 0.85f };
const float kAccent[4]    = { 0.35f, 0.75f, 1.00f, 1.0f };
const float kAccentDim[4] = { 0.20f, 0.45f, 0.70f, 1.0f };
const float kText[4]      = { 0.90f, 0.94f, 1.00f, 1.0f };
const float kTextDim[4]   = { 0.55f, 0.62f, 0.72f, 1.0f };
const float kWarn[4]      = { 1.00f, 0.55f, 0.30f, 1.0f };

// Stable widget ids per screen (hit regions are per-frame, ids just need
// to be unique within a frame).
enum BootIds {
    ID_CONTINUE = 100, ID_NEWCAREER, ID_MISSIONS, ID_SETTINGS, ID_EXIT,
    ID_LAUNCH, ID_BACK, ID_NAME_SUBMIT, ID_DEBRIEF_MENU, ID_DEBRIEF_NEXT,
    ID_PAUSE_RESUME, ID_PAUSE_MENU, ID_PAUSE_QUIT,
    ID_MISSION_BASE = 200 // + mission index
};

std::string FormatClock(float seconds) {
    int s = static_cast<int>(seconds);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Decode a PNG via WIC into a tightly-packed RGBA8 buffer for
// D3D12Renderer::CreateUiTexture. Returns false on any failure (missing
// file, bad image) — callers treat the logo as optional.
bool LoadPngRGBA(const wchar_t* path, std::vector<uint8_t>& pixels,
                 UINT& outW, UINT& outH) {
    pixels.clear();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    if (!comInit) return false;

    bool ok = false;
    ComPtr<IWICImagingFactory2> factory;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(factory->CreateDecoderFromFilename(
                path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame))) {
            UINT w = 0, h = 0;
            frame->GetSize(&w, &h);
            if (w > 0 && h > 0 && w <= 4096 && h <= 4096 &&
                SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                SUCCEEDED(converter->Initialize(frame.Get(),
                        GUID_WICPixelFormat32bppRGBA,
                        WICBitmapDitherTypeNone, nullptr, 0.0,
                        WICBitmapPaletteTypeCustom))) {
                pixels.resize(static_cast<size_t>(w) * h * 4);
                if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4,
                        static_cast<UINT>(pixels.size()), pixels.data()))) {
                    outW = w; outH = h; ok = true;
                }
            }
        }
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return ok;
}

// The key art is fully opaque — its near-black background would hide the
// starfield. Convert luminance to alpha so dark backdrop pixels become
// transparent (kept soft-edged to avoid a hard halo around the subject).
void KeyOutDarkBackground(std::vector<uint8_t>& pixels, float lo = 14.f, float hi = 60.f) {
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        float lum = 0.299f * pixels[i] + 0.587f * pixels[i + 1] + 0.114f * pixels[i + 2];
        float a = (lum - lo) / (hi - lo);
        a = a < 0.f ? 0.f : (a > 1.f ? 1.f : a);
        pixels[i + 3] = static_cast<uint8_t>(a * 255.f);
    }
}

// Drifting twinkling starfield drawn behind boot screens. Deterministic
// per-star hash — no RNG state, same sky every run.
void DrawStarfield(UiContext& ui, float w, float h, float t) {
    constexpr int kStars = 140;
    for (int i = 0; i < kStars; ++i) {
        float h1 = std::fabs(std::sin(i * 127.1f + 311.7f) * 43758.5453f);
        float h2 = std::fabs(std::sin(i * 269.5f + 183.3f) * 28001.8384f);
        float h3 = std::fabs(std::sin(i * 419.2f +  74.7f) * 12433.8197f);
        float h4 = std::fabs(std::sin(i *  71.9f + 402.1f) * 19642.3490f);
        h1 -= std::floor(h1); h2 -= std::floor(h2);
        h3 -= std::floor(h3); h4 -= std::floor(h4);

        float speed = 5.f + h3 * 14.f;                    // px/sec drift
        float sx = h1 * w + std::sin(t * 0.3f + h4 * 6.28f) * 6.f;
        float sy = std::fmod(h2 * (h + 40.f) + t * speed, h + 40.f) - 20.f;
        float size = 1.f + h4 * 1.8f;
        float tw = 0.30f + 0.55f * (0.5f + 0.5f * std::sin(t * (0.8f + h3) + h4 * 6.28f));
        float c[4];
        if (h3 > 0.88f) { c[0]=kAccent[0]; c[1]=kAccent[1]; c[2]=kAccent[2]; } // rare cyan star
        else           { c[0]=0.85f;    c[1]=0.90f;    c[2]=1.00f;    }
        c[3] = tw;
        ui.Rect(sx, sy, size, size, c);
    }
}
} // namespace

bool BootFlow::Initialize(helix::renderer::D3D12Renderer& renderer, int width, int height) {
    renderer_ = &renderer;
    (void)width; (void)height;
    screen_ = Screen::BootLoading;
    initStep_ = 0;
    loadStatus_ = "STARTING";
    helix::log::Info("BootFlow: entering boot loading");
    return true;
}

// One init stage per call — called from Update() while BootLoading so the
// loading screen renders live progress between stages.
void BootFlow::RunInitStep() {
    switch (initStep_) {
        case 0:
            loadStatus_ = "RASTERIZING FONT ATLAS";
            helix::log::Info("BootFlow: building font atlas (Segoe UI 42px)");
            if (!font_.Build(L"Segoe UI", 42)) {
                helix::log::Error("BootFlow: font atlas build failed");
                loadStatus_ = "FONT ATLAS FAILED";
                initStep_ = 6; // skip to done — menus will be textless but alive
                break;
            }
            ++initStep_;
            break;
        case 1:
            loadStatus_ = "UPLOADING UI TEXTURES";
            // 1x1 white texture for tinted rects — MUST be slot 0: UiContext
            // draws buckets in texture-index order, so panels (white) render
            // before text (atlas).
            {
                static const uint8_t white[4] = { 255, 255, 255, 255 };
                renderer_->CreateUiTexture(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, white);
                font_.UploadTo(*renderer_);
            }
            helix::log::Info("BootFlow: UI textures uploaded (white=0 atlas=%u)", font_.TextureIndex());
            ++initStep_;
            break;
        case 2:
            loadStatus_ = "LOADING PILOT PROFILE";
            profile_.Load();
            firstRun_ = !profile_.IsValidCallsign(profile_.callsign) || profile_.callsign == "Pilot";
            helix::log::Info("BootFlow: profile loaded callsign='%s' firstRun=%d",
                             profile_.callsign.c_str(), firstRun_ ? 1 : 0);
            ++initStep_;
            break;
        case 3:
            loadStatus_ = "GENERATING CAREER";
            missions_ = helix::game::CareerMissions();
            helix::log::Info("BootFlow: %zu missions generated", missions_.size());
            ++initStep_;
            break;
        case 4:
            loadStatus_ = "LOADING CREST";
            {
                // Logo/crest next to the exe's Resources folder (copied by
                // the CMake post-build step). Optional — menus still work
                // without it.
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                std::wstring dir(exePath);
                dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\Resources\\";
                // Prefer a dedicated logo file (real alpha); the key-art
                // fallbacks are opaque, so dark-key their backgrounds.
                struct Candidate { const wchar_t* name; bool keyDark; };
                const Candidate candidates[] = {
                    { L"logo.png", false },
                    { L"HelixSim.png", true },
                    { L"Helix_Flight_sim.png", true },
                    { L"Helix Flight sim.jpg", true },
                };
                for (const auto& cand : candidates) {
                    std::vector<uint8_t> px; UINT w = 0, h = 0;
                    if (LoadPngRGBA((dir + cand.name).c_str(), px, w, h)) {
                        if (cand.keyDark) KeyOutDarkBackground(px);
                        logoTexture_ = renderer_->CreateUiTexture(
                            static_cast<int>(w), static_cast<int>(h),
                            DXGI_FORMAT_R8G8B8A8_UNORM, px.data());
                        logoW_ = w; logoH_ = h;
                        helix::log::Info("BootFlow: crest loaded '%ls' %ux%u -> slot %u",
                                         cand.name, w, h, logoTexture_);
                        break;
                    }
                }
                if (logoTexture_ == UINT_MAX)
                    helix::log::Info("BootFlow: no crest image found (optional)");
            }
            ++initStep_;
            break;
        case 5:
            loadStatus_ = "READY";
            helix::log::Info("BootFlow: init complete -> %s",
                             (firstRun_ && !profile_.introCompleted) ? "NameEntry" : "MainMenu");
            GoTo(firstRun_ && !profile_.introCompleted ? Screen::NameEntry : Screen::MainMenu);
            initStep_ = 6;
            break;
        default:
            break;
    }
}

void BootFlow::GoTo(Screen s) { screen_ = s; }

void BootFlow::Update(UiContext& ui, float dt) {
    splashTimer_ += dt;
    loadingTimer_ += dt;

    if (screen_ == Screen::BootLoading) {
        RunInitStep();
        return; // no input handling while booting
    }

    int hit = ui.ClickedId();
    bool lmb = ui.MouseDown(0);

    switch (screen_) {
        case Screen::BootLoading:
            break;

        case Screen::NameEntry: {
            if (nameDone_) {
                ceremonyT_ += dt;
                if (ceremonyT_ > 2.8f) {
                    nameDone_ = false;
                    GoTo(Screen::MainMenu);
                }
                break;
            }
            ui.SetTextFocusId(ID_NAME_SUBMIT);
            auto acceptCallsign = [&]() {
                inductionName_ = ui.TextBuffer();
                profile_.callsign = inductionName_;
                profile_.introCompleted = true;
                profile_.Save();
                ui.TextBuffer().clear();
                firstRun_ = false;
                nameDone_ = true;
                ceremonyT_ = 0.f;
            };
            if (ui.TakeSubmit() && Profile::IsValidCallsign(ui.TextBuffer())) {
                acceptCallsign();
            } else if (hit == ID_NAME_SUBMIT && Profile::IsValidCallsign(ui.TextBuffer())) {
                acceptCallsign();
            }
            break;
        }

        case Screen::MainMenu:
            if (hit == ID_NEWCAREER || hit == ID_CONTINUE || hit == ID_MISSIONS) {
                selectedMission_ = 0;
                GoTo(Screen::MissionSelect);
            } else if (hit == ID_EXIT) {
                wantsQuit_ = true;
            }
            break;

        case Screen::MissionSelect:
            if (hit >= ID_MISSION_BASE && hit < ID_MISSION_BASE + static_cast<int>(missions_.size())) {
                selectedMission_ = hit - ID_MISSION_BASE;
            }
            if (hit == ID_LAUNCH && selectedMission_ >= 0 &&
                selectedMission_ < static_cast<int>(missions_.size())) {
                pendingMission_ = &missions_[selectedMission_];
                loadingTimer_ = 0.f;
                GoTo(Screen::Loading);
            } else if (hit == ID_BACK) {
                GoTo(Screen::MainMenu);
            }
            break;

        case Screen::Loading:
            if (loadingTimer_ > 1.6f) {
                // Hand off to flight: main.cpp picks up pendingMission_.
                screen_ = Screen::None;
            }
            break;

        case Screen::Debrief:
            if (hit == ID_DEBRIEF_NEXT && selectedMission_ + 1 < static_cast<int>(missions_.size())) {
                selectedMission_++;
                pendingMission_ = &missions_[selectedMission_];
                loadingTimer_ = 0.f;
                GoTo(Screen::Loading);
            } else if (hit == ID_DEBRIEF_MENU || hit == ID_DEBRIEF_NEXT) {
                GoTo(Screen::MainMenu);
                selectedMission_ = 0;
            }
            break;

        case Screen::Pause:
            if (hit == ID_PAUSE_RESUME) GoTo(Screen::None);       // resume flight
            else if (hit == ID_PAUSE_MENU) { ExitToMenu(); }
            else if (hit == ID_PAUSE_QUIT) wantsQuit_ = true;
            break;

        case Screen::None:
            break;
    }

    // Text focus only lives on the name-entry screen.
    if (screen_ != Screen::NameEntry) ui.SetTextFocusId(0);
}

void BootFlow::Draw(UiContext& ui) {
    switch (screen_) {
        case Screen::BootLoading:   DrawBootLoading(ui); break;
        case Screen::NameEntry:   DrawNameEntry(ui); break;
        case Screen::MainMenu:    DrawMainMenu(ui); break;
        case Screen::MissionSelect: DrawMissionSelect(ui); break;
        case Screen::Loading:     DrawLoading(ui); break;
        case Screen::Debrief:     DrawDebrief(ui); break;
        case Screen::Pause:       DrawPause(ui); break;
        case Screen::None: break;
    }
}

// ============================================================================
// BOOT LOADING SCREEN — layout reference
// ----------------------------------------------------------------------------
// All positions are fractions of ui.ScreenW()/ScreenH() so it scales with
// resolution. Text y = TOP of line box; font size is arg 3 (px).
//   Title "HELIXSIM"          y = 34% H,  font 110px, centered on screen
//   Subtitle                  y = 34%H+120, font 24px, centered
//   Status line               y = 68% H,  font 22px, centered
//   Progress segments         y = 68%H+48, five 90x8px bars centered
//   Version footer            y = 92% H,  font 16px, centered
//   Atlas diag preview        bottom-left 16px inset (remove when done)
// ============================================================================
void BootFlow::DrawBootLoading(UiContext& ui) {
    // Deep-space backdrop with a slow accent sweep.
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), kBg);
    float sweepX = fmodf(splashTimer_ * 0.15f, 1.2f) * ui.ScreenW() - ui.ScreenW() * 0.1f;
    ui.Rect(sweepX, 0, ui.ScreenW() * 0.08f, ui.ScreenH(), 0.10f, 0.20f, 0.35f, 0.05f);

    const float cx = ui.ScreenW() * 0.5f;
    // Big title — the "large futuristic text" splash look.
    ui.Text(font_, 0.f, ui.ScreenH() * 0.34f, 110.f, "HELIX", kAccent, 1, ui.ScreenW());
    ui.Text(font_, 0.f, ui.ScreenH() * 0.34f + 120.f, 24.f,
           "VANGUARD SPACE COMMAND", kTextDim, 1, ui.ScreenW());

    // Status block: current stage + animated dots + progress segments.
    float dots = fmodf(splashTimer_ * 2.f, 3.f);
    std::string status = loadStatus_;
    for (int i = 0; i < static_cast<int>(dots); ++i) status += ".";
    ui.Text(font_, 0.f, ui.ScreenH() * 0.68f, 22.f, status, kText, 1, ui.ScreenW());

    // Segment bar: one segment per completed init step.
    const float segW = 90.f, segGap = 10.f, segH = 8.f;
    const float barW = 5.f * segW + 4.f * segGap;
    const float bx = cx - barW * 0.5f, by = ui.ScreenH() * 0.68f + 48.f;
    for (int i = 0; i < 5; ++i) {
        float sx = bx + i * (segW + segGap);
        bool done = initStep_ > i;
        bool active = initStep_ == i;
        const float* col = done ? kAccent : (active ? kAccentDim : kPanelEdge);
        float pulse = active ? 0.5f + 0.5f * sinf(splashTimer_ * 6.f) : 1.f;
        float c[4] = { col[0], col[1], col[2], col[3] * pulse };
        ui.Rect(sx, by, segW, segH, c);
    }

    ui.Text(font_, 0.f, ui.ScreenH() * 0.92f, 16.f,
           "HELIOS DYNAMICS // BUILD 0.1.0-m1", kTextDim, 1, ui.ScreenW());

}

// ============================================================================
// PILOT REGISTRATION SCREEN — layout reference
// ----------------------------------------------------------------------------
// Coordinate system: ui.Text/ui.Rect/ui.Button take PIXELS.
//   x, y   = TOP-LEFT corner of the element (for Text: top of the line box;
//            the glyph quads extend ~cellH*scale below it)
//   w, h   = size in pixels
//   Text(..., align, boxWidth): align 0=left, 1=center INSIDE a box that
//            starts at x and is boxWidth wide, 2=right
//
// Screen/panel anchors:
//   cx,cy  = screen center            (ui.ScreenW()/2, ui.ScreenH()/2)
//   px,py  = panel top-left corner    (panel is centered on screen)
//   panelW, panelH = panel size       (560x300 here)
//
// LAYOUT: all positions are named constants (kTitleY, kFieldY, ...) in the
// "TWEAK POSITIONS HERE" block inside DrawNameEntry — pixels down from the
// panel's top edge. Edit those numbers to move things.
// ============================================================================
void BootFlow::DrawNameEntry(UiContext& ui) {
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), kBg);   // fullscreen backdrop
    DrawStarfield(ui, ui.ScreenW(), ui.ScreenH(), splashTimer_);

    // ---- anchors ----
    const float cx = ui.ScreenW() * 0.5f;             // screen center X
    const float panelW = 560.f, panelH = 400.f;       // panel size (px)
    const float px = cx - panelW * 0.5f;              // panel left edge
    const float py = ui.ScreenH() * 0.5f - panelH * 0.5f; // panel top edge

    // "Cleared for flight" ceremony — replaces the form after ENLIST.
    if (nameDone_) {
        ui.Panel(cx - 240.f, ui.ScreenH() * 0.5f - 110.f, 480.f, 220.f,
                 kPanel, kPanelEdge);
        const float cy = ui.ScreenH() * 0.5f;
        if (ceremonyT_ < 0.9f) {
            float pulse = 0.6f + 0.4f * std::sin(splashTimer_ * 6.f);
            float pc[4] = { kText[0], kText[1], kText[2], pulse };
            ui.Text(font_, 0, cy - 60.f, 30.f, "PROCESSING", pc, 1, ui.ScreenW());
            ui.Text(font_, 0, cy - 18.f, 16.f, "VERIFIED AGAINST PERSONNEL FILES", kTextDim, 1, ui.ScreenW());
        } else {
            ui.Text(font_, 0, cy - 78.f, 38.f, "CLEARED FOR FLIGHT", kAccent, 1, ui.ScreenW());
            ui.Text(font_, 0, cy - 28.f, 22.f, "WELCOME ABOARD, " + inductionName_, kText, 1, ui.ScreenW());
            ui.Text(font_, 0, cy + 8.f, 15.f, "REPORTING TO MAIN MENU", kTextDim, 1, ui.ScreenW());
        }
        return;
    }

    ui.Panel(px, py, panelW, panelH, kPanel, kPanelEdge); // draw the box

    // ---- crest flanking the panel ----
    // The PNG has a transparent background, so the starfield shows through.
    // Aspect ratio is preserved; size is clamped to the side margins.
    if (logoTexture_ != UINT_MAX) {
        const float sideSpace = (ui.ScreenW() - panelW) * 0.5f - 24.f;
        float ih = panelH * 0.72f;
        float iw = ih * (static_cast<float>(logoW_) / static_cast<float>(logoH_));
        if (iw > sideSpace) {
            iw = sideSpace;
            ih = iw * (static_cast<float>(logoH_) / static_cast<float>(logoW_));
        }
        const float iy = ui.ScreenH() * 0.5f - ih * 0.5f;
        ui.Image(logoTexture_, px - iw - 36.f, iy, iw, ih);
        ui.Image(logoTexture_, px + panelW + 36.f, iy, iw, ih);
    }

    // HUD targeting-frame corner brackets around the panel
    {
        const float arm = 22.f, th = 2.f, off = 7.f;
        // horizontal arms (top-left, top-right, bottom-left, bottom-right)
        ui.Rect(px - off, py - off, arm, th, kAccent);
        ui.Rect(px + panelW + off - arm, py - off, arm, th, kAccent);
        ui.Rect(px - off, py + panelH + off - th, arm, th, kAccent);
        ui.Rect(px + panelW + off - arm, py + panelH + off - th, arm, th, kAccent);
        // vertical arms
        ui.Rect(px - off, py - off, th, arm, kAccent);
        ui.Rect(px + panelW + off - th, py - off, th, arm, kAccent);
        ui.Rect(px - off, py + panelH + off - arm, th, arm, kAccent);
        ui.Rect(px + panelW + off - th, py + panelH + off - arm, th, arm, kAccent);
    }

    // ==================== TWEAK POSITIONS HERE ====================
    // All values are pixels measured DOWN from the panel's top edge (py).
    // Bigger number = lower on screen. Change these, rebuild, done.
    const float kClassY    = 16.f;   // "VANGUARD SPACE COMMAND" header line
    const float kTitleY    = 52.f;   // "PILOT REGISTRATION" title
    const float kSubY      = 118.f;  // "Enter your callsign, Pilot:"
    const float kLabelY    = 168.f;  // "CALLSIGN" field label
    const float kFieldY    = 186.f;  // text input box top edge
    const float kFieldH    = 36.f;   // text input box height
    const float kFieldX    = 60.f;   // text box left inset from panel edge
    const float kBtnFromBottom = 76.f; // ENLIST button distance above panel bottom
    const float kHintFromBottom = 18.f; // validation hint above panel bottom
    // ==============================================================

    // ---- classification header + rule ----
    ui.Text(font_, px, py + kClassY, 13.f,
           "VANGUARD SPACE COMMAND PERSONNEL INDUCTION", kAccentDim, 1, panelW);
    ui.Rect(px + 50.f, py + 44.f, panelW - 100.f, 1.f,
            0.20f, 0.38f, 0.60f, 0.5f);

    // ---- title + subtitle (centered within the panel width) ----
    ui.Text(font_, px, py + kTitleY, 34.f, "PILOT REGISTRATION", kAccent, 1, panelW);
    ui.Text(font_, px, py + kSubY, 20.f,
           "Enter your callsign, Pilot:", kTextDim, 1, panelW);

    // ---- text field ----
    ui.Text(font_, px + kFieldX, py + kLabelY, 12.f, "CALLSIGN", kTextDim, 0);
    const float fx = px + kFieldX, fy = py + kFieldY;
    const float fw = panelW - kFieldX * 2.f, fh = kFieldH;
    ui.Rect(fx, fy, fw, fh, 0.02f, 0.03f, 0.06f, 1.f);  // field background
    // 1px accent border on all four edges
    ui.Rect(fx, fy, fw, 1.f, kAccentDim);
    ui.Rect(fx, fy + fh - 1.f, fw, 1.f, kAccentDim);
    ui.Rect(fx, fy, 1.f, fh, kAccentDim);
    ui.Rect(fx + fw - 1.f, fy, 1.f, fh, kAccentDim);
    const std::string& shown = ui.TextBuffer();
    // typed text: 14px left inset; vertically centered by INK bounds so the
    // glyphs sit at the same height as the caret (the em box top is not the
    // ink top — ascender leading pushes ink down inside the line box).
    float inkTop = 0.f, inkH = 20.f;
    font_.MeasureInkExtent(shown, 20.f, inkTop, inkH);
    const float textY = fy + (fh - inkH) * 0.5f - inkTop;
    ui.Text(font_, fx + 14.f, textY, 20.f, shown, kText, 0);
    // caret blink: 2px wide bar after the last character
    if (fmodf(splashTimer_ * 2.f, 1.f) < 0.6f) {
        float caretX = fx + 14.f + font_.MeasureWidth(ui.TextBuffer(), 20.f) + 4.f;
        ui.Rect(caretX, fy + 8.f, 2.f, 20.f, kAccent);
    }

    // ---- ENLIST button ----
    bool valid = Profile::IsValidCallsign(ui.TextBuffer()) && !ui.TextBuffer().empty();
    ui.Button(font_, ID_NAME_SUBMIT, cx - 110.f, py + panelH - kBtnFromBottom, 220.f, 48.f,
              "ENLIST", valid, false);
    // validation hint: only shows while typing an invalid callsign
    if (!valid && !ui.TextBuffer().empty())
        ui.Text(font_, px, py + panelH - kHintFromBottom, 16.f,
               "1-16 chars: letters, numbers, space, - _", kWarn, 1, panelW);

    // ---- flavor line under the panel ----
    ui.Text(font_, 0.f, py + panelH + 18.f, 13.f,
           "ALL FLIGHTS LOGGED  MISHAPS are FILED UNDER 'TRAINING'", kTextDim, 1, ui.ScreenW());
}

void BootFlow::DrawMainMenu(UiContext& ui) {
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), kBg);
    DrawStarfield(ui, ui.ScreenW(), ui.ScreenH(), splashTimer_);
    const float cx = ui.ScreenW() * 0.5f;
    ui.Text(font_, 0.f, ui.ScreenH() * 0.18f, 64.f, "HELIXSIM", kAccent, 1, ui.ScreenW());
    ui.Text(font_, 0.f, ui.ScreenH() * 0.18f + 74.f, 20.f,
           "pilot: " + profile_.callsign, kTextDim, 1, ui.ScreenW());

    const float bw = 320.f, bh = 56.f, gap = 14.f;
    float y = ui.ScreenH() * 0.42f;
    ui.Button(font_, ID_NEWCAREER, cx - bw * 0.5f, y, bw, bh, "NEW CAREER", false);
    ui.Button(font_, ID_MISSIONS,  cx - bw * 0.5f, y + bh + gap, bw, bh, "MISSIONS", false);
    ui.Button(font_, ID_EXIT,      cx - bw * 0.5f, y + (bh + gap) * 2.f, bw, bh, "EXIT", false);

    ui.Text(font_, 0.f, ui.ScreenH() * 0.92f, 16.f,
           "Vanguard Space Command // build 0.1", kTextDim, 1, ui.ScreenW());
}

void BootFlow::DrawMissionSelect(UiContext& ui) {
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), kBg);
    DrawStarfield(ui, ui.ScreenW(), ui.ScreenH(), splashTimer_);
    const float mx = ui.ScreenW() * 0.08f, my = ui.ScreenH() * 0.10f;
    ui.Text(font_, mx, my, 40.f, "MISSION SELECT", kAccent);

    // Mission list (left column)
    const float listX = mx, listY = my + 70.f, rowH = 64.f, rowW = ui.ScreenW() * 0.44f;
    for (size_t i = 0; i < missions_.size(); ++i) {
        const auto& m = missions_[i];
        bool sel = static_cast<int>(i) == selectedMission_;
        float ry = listY + i * (rowH + 8.f);
        const float* bg = sel ? kPanelEdge : kPanel;
        ui.Rect(listX, ry, rowW, rowH, bg);
        ui.Rect(listX, ry, 4.f, rowH, sel ? kAccent : kAccentDim);
        ui.Text(font_, listX + 18.f, ry + 10.f, 22.f, m.name, kText);
        ui.Text(font_, listX + 18.f, ry + 38.f, 15.f, m.act + "  //  " + m.objective, kTextDim);
        ui.HitRegion(ID_MISSION_BASE + static_cast<int>(i), listX, ry, rowW, rowH);
    }

    // Briefing panel (right)
    const auto& m = missions_[std::clamp(selectedMission_, 0, static_cast<int>(missions_.size()) - 1)];
    const float bx = ui.ScreenW() * 0.56f, by = my + 70.f;
    const float bw = ui.ScreenW() * 0.36f, bh = ui.ScreenH() * 0.62f;
    ui.Panel(bx, by, bw, bh, kPanel, kPanelEdge);
    ui.Text(font_, bx + 24.f, by + 20.f, 30.f, m.name, kAccent);
    ui.Text(font_, bx + 24.f, by + 62.f, 16.f, m.act, kAccentDim);
    // Word-wrap briefing crudely at ~52 chars
    std::string brief = m.briefing;
    float ty = by + 96.f;
    size_t pos = 0;
    while (pos < brief.size()) {
        size_t end = std::min(brief.size(), pos + 52);
        if (end < brief.size()) {
            size_t sp = brief.rfind(' ', end);
            if (sp != std::string::npos && sp > pos) end = sp;
        }
        ui.Text(font_, bx + 24.f, ty, 18.f, brief.substr(pos, end - pos), kText);
        ty += 26.f;
        pos = (end < brief.size() && brief[end] == ' ') ? end + 1 : end;
        if (ty > by + bh - 90.f) break;
    }
    ui.Text(font_, bx + 24.f, by + bh - 70.f, 18.f, "OBJECTIVE: " + m.objective, kWarn);

    ui.Button(font_, ID_LAUNCH, bx + 24.f, by + bh - 48.f, 160.f, 40.f, "LAUNCH", false);
    ui.Button(font_, ID_BACK, bx + bw - 130.f, by + bh - 48.f, 106.f, 40.f, "BACK", false);
}

void BootFlow::DrawLoading(UiContext& ui) {
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), kBg);
    const float cx = ui.ScreenW() * 0.5f;
    const auto& m = *pendingMission_;
    ui.Text(font_, 0.f, ui.ScreenH() * 0.40f, 36.f, "PREPARING MISSION", kAccent, 1, ui.ScreenW());
    ui.Text(font_, 0.f, ui.ScreenH() * 0.40f + 52.f, 22.f, m.name, kText, 1, ui.ScreenW());

    const float bw = 480.f, bh = 10.f;
    const float bx = cx - bw * 0.5f, by = ui.ScreenH() * 0.55f;
    ui.Rect(bx, by, bw, bh, 0.10f, 0.13f, 0.20f, 1.f);
    float frac = std::min(1.f, loadingTimer_ / 1.6f);
    ui.Rect(bx, by, bw * frac, bh, kAccent);
    ui.Text(font_, 0.f, by + 24.f, 16.f, "loading world...", kTextDim, 1, ui.ScreenW());
}

void BootFlow::DrawDebrief(UiContext& ui) {
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), 0.02f, 0.03f, 0.05f, 0.92f);
    const float cx = ui.ScreenW() * 0.5f;
    const float pw = 620.f, ph = 460.f;
    const float px = cx - pw * 0.5f, py = ui.ScreenH() * 0.5f - ph * 0.5f;
    ui.Panel(px, py, pw, ph, kPanel, kPanelEdge);

    ui.Text(font_, px, py + 28.f, 36.f,
           debriefComplete_ ? "MISSION COMPLETE" : "MISSION FAILED",
           debriefComplete_ ? kAccent : kWarn, 1, pw);
    ui.Text(font_, px, py + 76.f, 20.f, debriefMission_.name, kText, 1, pw);
    ui.Text(font_, px, py + 106.f, 16.f, "pilot: " + profile_.callsign, kTextDim, 1, pw);

    char buf[128];
    snprintf(buf, sizeof(buf), "Score: %d    Airtime: %s    Top altitude: %.0f m    Landings: %d",
             debriefStats_.score, FormatClock(debriefStats_.airtime).c_str(),
             debriefStats_.topAltitude, debriefStats_.landed ? 1 : 0);
    ui.Text(font_, px, py + 160.f, 20.f, buf, kText, 1, pw);

    if (debriefComplete_) {
        ui.Button(font_, ID_DEBRIEF_NEXT, cx - 240.f, py + ph - 76.f, 220.f, 48.f,
                  "NEXT MISSION", true);
    }
    ui.Button(font_, ID_DEBRIEF_MENU, cx + (debriefComplete_ ? 20.f : -110.f), py + ph - 76.f,
              220.f, 48.f, "RETURN TO MENU", false);
}

void BootFlow::DrawPause(UiContext& ui) {
    ui.Rect(0, 0, ui.ScreenW(), ui.ScreenH(), 0.01f, 0.02f, 0.04f, 0.75f);
    const float cx = ui.ScreenW() * 0.5f;
    const float pw = 420.f, ph = 340.f;
    const float px = cx - pw * 0.5f, py = ui.ScreenH() * 0.5f - ph * 0.5f;
    ui.Panel(px, py, pw, ph, kPanel, kPanelEdge);
    ui.Text(font_, px, py + 28.f, 34.f, "PAUSED", kAccent, 1, pw);

    const float bw = 260.f, bh = 50.f, gap = 12.f;
    float y = py + 90.f;
    ui.Button(font_, ID_PAUSE_RESUME, cx - bw * 0.5f, y, bw, bh, "RESUME", false);
    ui.Button(font_, ID_PAUSE_MENU,   cx - bw * 0.5f, y + bh + gap, bw, bh, "QUIT TO MENU", false);
    ui.Button(font_, ID_PAUSE_QUIT,   cx - bw * 0.5f, y + (bh + gap) * 2.f, bw, bh, "EXIT GAME", false);
}

void BootFlow::EnterPause() {
    if (screen_ == Screen::None) GoTo(Screen::Pause);
}

void BootFlow::EnterDebrief(const helix::game::MissionSystem::DebriefStats& stats,
                            const helix::game::MissionDef& mission, bool complete) {
    debriefStats_ = stats;
    debriefMission_ = mission;
    debriefComplete_ = complete;
    GoTo(Screen::Debrief);
    // Profile flags for onboarding completion
    if (complete) {
        profile_.introCompleted = true;
        profile_.flightChecksDone = true;
        profile_.Save();
    }
}

void BootFlow::ExitToMenu() {
    selectedMission_ = 0;
    GoTo(Screen::MainMenu);
}

} // namespace helix::ui
