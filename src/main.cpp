#include <windows.h>
#include <chrono>
#include <cstdio>
#include <algorithm>

#include "Window/Window.h"
#include "Renderer/D3D12Renderer.h"
#include "Input/InputManager.h"
#include "Audio/AudioEngine.h"
#include "Game/World.h"
#include "Game/Mission.h"
#include "HUD/HUDRenderer.h"
#include "UI/BootFlow.h"
#include "UI/HudOverlay.h"
#include "UI/Log.h"

using helix::window::Window;
using helix::renderer::D3D12Renderer;
using helix::input::InputManager;
using helix::audio::AudioEngine;
using helix::game::MissionStatus;
using helix::ui::BootFlow;

namespace {
bool g_running = true;
bool g_escWasDown = false;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    helix::log::Init();
    helix::log::Info("=== HelixSim starting ===");

    Window window;
    if (!window.Create(hInstance, 1600, 900, L"Helix Spaceflight Simulator")) {
        MessageBox(nullptr, L"Failed to create window.", L"Helix", MB_OK | MB_ICONERROR);
        return -1;
    }

    D3D12Renderer renderer;
    if (!renderer.Initialize(window.Handle(), window.Width(), window.Height())) {
        std::wstring msg = L"DirectX 12 initialization failed.\n\n";
        if (!renderer.InitError().empty()) {
            int len = MultiByteToWideChar(CP_UTF8, 0, renderer.InitError().c_str(), -1, nullptr, 0);
            std::wstring detail(len - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, renderer.InitError().c_str(), -1, detail.data(), len);
            msg += L"Step that failed: " + detail + L"\nHRESULT info is in the debugger output.";
        } else {
            msg += L"No details available.";
        }
        MessageBoxW(nullptr, msg.c_str(), L"Helix - Renderer", MB_OK | MB_ICONERROR);
        helix::log::Error("renderer init failed: %s", renderer.InitError().c_str());
        return -1;
    }
    helix::log::Info("renderer initialized OK");

    InputManager input;
    input.Initialize(window.Handle());

    AudioEngine audio;
    if (!audio.Initialize()) {
        MessageBox(nullptr, L"Failed to initialize XAudio2.", L"Helix", MB_OK | MB_ICONERROR);
        return -1;
    }

    helix::ui::UiContext ui;
    BootFlow boot;
    if (!boot.Initialize(renderer, window.Width(), window.Height())) {
        MessageBox(nullptr, L"Failed to initialize UI/font atlas.", L"Helix", MB_OK | MB_ICONERROR);
        return -1;
    }
    ui.SetSolidTextureIndex(0); // 1x1 white uploaded by BootFlow::Initialize

    helix::ui::HudOverlay hudOverlay;
    helix::game::MissionSystem mission;
    helix::game::MissionProgress missionProgress;

    helix::window::WindowCallbacks callbacks;
    callbacks.onRawMessage = [&](UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_INPUT) {
            input.HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        }
    };
    callbacks.onKey = [&](int vk, bool isDown) {
        input.HandleKey(vk, isDown);
        ui.OnKey(vk, isDown);
    };
    callbacks.onChar = [&](wchar_t ch) { ui.OnChar(ch); };
    callbacks.onMouseMove = [&](int x, int y) { ui.OnMouseMove(static_cast<float>(x), static_cast<float>(y)); };
    callbacks.onMouseButton = [&](int button, bool isDown, int x, int y) {
        if (isDown) ui.OnMouseDown(static_cast<float>(x), static_cast<float>(y), button);
        else ui.OnMouseUp(static_cast<float>(x), static_cast<float>(y), button);
    };
    callbacks.onClose = [&]() { g_running = false; };
    callbacks.onResize = [&](int w, int h) { renderer.OnResize(w, h); };
    window.SetCallbacks(callbacks);

    helix::game::World world;
    world.Initialize(renderer);
    helix::log::Info("world initialized");

    helix::hud::HUDRenderer hudRenderer;
    std::vector<helix::renderer::LineVertex> hudLines;

    // Fixed-timestep physics (120 Hz) decoupled from variable-rate rendering,
    // matching the "seamless world / no loading screens" requirement without
    // physics behavior depending on frame rate.
    constexpr double kFixedDt = 1.0 / 120.0;
    auto lastTime = std::chrono::high_resolution_clock::now();
    double accumulator = 0.0;

    helix::log::Info("entering main loop");
    bool loggedFirstFrame = false;
    while (g_running) {
        if (!window.PumpMessages()) break;
        if (boot.WantsQuit()) break;

        auto now = std::chrono::high_resolution_clock::now();
        double frameSeconds = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        if (frameSeconds > 0.25) frameSeconds = 0.25; // clamp huge spikes (e.g. debugger pause)
        accumulator += frameSeconds;

        input.PollGamepads();

        float screenW = static_cast<float>(window.Width());
        float screenH = static_cast<float>(window.Height());
        ui.BeginFrame(screenW, screenH);

        boot.Update(ui, static_cast<float>(frameSeconds > 0 ? frameSeconds : 0.016));

        // Esc edge-detect → pause (only while sim running)
        bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (escDown && !g_escWasDown && boot.SimRunning()) boot.EnterPause();
        g_escWasDown = escDown;

        // Launch a pending mission (Loading finished → Screen::None)
        if (const auto* m = boot.PendingMission()) {
            mission.Start(*m);
            world.RespawnPlayer();
            boot.ClearPendingMission();
        }

        // Sim step — only when no screen is up at all
        if (boot.SimRunning()) {
            while (accumulator >= kFixedDt) {
                world.Update(input, audio, static_cast<float>(kFixedDt));
                input.NewFrame();
                accumulator -= kFixedDt;
            }

            // Mission progress + completion
            auto& body = world.PlayerBody();
            bool onGround = body.position.y <= 2.0f && body.velocity.Length() < 5.f;
            MissionStatus status = mission.Update(body.position.y, body.velocity.Length(),
                                                  onGround, static_cast<float>(frameSeconds),
                                                  missionProgress);
            if (status == MissionStatus::Complete || status == MissionStatus::Failed) {
                const auto* def = mission.Active();
                if (def) {
                    auto stats = mission.LastDebrief();
                    stats.landed = missionProgress.hasLanded;
                    boot.EnterDebrief(stats, *def, status == MissionStatus::Complete);
                    mission.Stop();
                }
            }
        } else {
            accumulator = 0.0; // don't bank physics time while in menus
        }

        // ---- render ----
        if (boot.InBootFlow()) {
            // Pre-flight screens: flat backdrop, no world
            renderer.BeginFrame(helix::math::Vec3{ 0.025f, 0.035f, 0.070f });
        } else {
            renderer.BeginFrame(helix::math::Vec3{ 0.02f, 0.02f, 0.05f });
            world.Render(renderer);

            float aspect = screenW / std::max(1.f, screenH);
            hudRenderer.Build(world.PlayerBody(), world.PlayerSystems(), aspect, hudLines);
            renderer.DrawHudLines(hudLines);

            if (boot.SimRunning()) {
                std::string objective;
                if (const auto* def = mission.Active()) objective = def->objective;
                hudOverlay.Draw(ui, boot.GetFont(), world.PlayerBody(),
                                world.PlayerSystems(), objective);
            }
        }

        boot.Draw(ui);
        ui.Render(renderer);
        renderer.EndFrame();

        if (!loggedFirstFrame) {
            loggedFirstFrame = true;
            helix::log::Info("first frame presented (screen=%d)", static_cast<int>(boot.CurrentScreen()));
        }
    }

    audio.Shutdown();
    renderer.Shutdown();
    return 0;
}
