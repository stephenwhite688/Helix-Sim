#pragma once
#include <string>
#include <vector>
#include "Ui.h"
#include "Profile.h"
#include "../Game/Mission.h"
#include "../Renderer/D3D12Renderer.h"

namespace helix::ui {

// Owns the game's front-end state machine (SPEC §3.6 + §3.10):
//   Splash → NameEntry? → MainMenu → MissionSelect → Loading → [Flying]
// In flight it also owns Pause and Debrief overlays.
//
// main.cpp drives it: while BootFlow::InBootFlow() the world is frozen
// and only screens render; once a mission launches, the sim runs and
// BootFlow is consulted again only for pause/debrief overlays.
class BootFlow {
public:
    enum class Screen {
        BootLoading, NameEntry, MainMenu, MissionSelect, Loading, Debrief, Pause, None
    };

    // Cheap: just records the renderer and enters BootLoading. The real
    // init work happens one stage per frame inside Update() so the loading
    // screen can display live progress.
    bool Initialize(helix::renderer::D3D12Renderer& renderer, int width, int height);

    // Call once per frame while in the boot flow (world frozen).
    void Update(UiContext& ui, float dt);
    void Draw(UiContext& ui);

    // True only for pre-flight screens (Splash..Loading). Pause/Debrief
    // are flight overlays — the world keeps rendering behind them.
    bool InBootFlow() const {
        return screen_ == Screen::BootLoading || screen_ == Screen::NameEntry ||
               screen_ == Screen::MainMenu || screen_ == Screen::MissionSelect ||
               screen_ == Screen::Loading;
    }
    // True when the sim should run (no screen at all).
    bool SimRunning() const { return screen_ == Screen::None; }
    Screen CurrentScreen() const { return screen_; }

    // --- in-flight overlays ---
    void EnterPause();
    void EnterDebrief(const helix::game::MissionSystem::DebriefStats& stats,
                      const helix::game::MissionDef& mission, bool complete);
    void ExitToMenu();           // debrief/pause -> main menu
    bool WantsQuit() const { return wantsQuit_; }

    // Mission the flow wants launched (valid after LaunchMission()).
    const helix::game::MissionDef* PendingMission() const { return pendingMission_; }
    void ClearPendingMission() { pendingMission_ = nullptr; }

    const Profile& GetProfile() const { return profile_; }
    Profile& GetProfile() { return profile_; }
    const FontAtlas& GetFont() const { return font_; }
    const std::vector<helix::game::MissionDef>& Missions() const { return missions_; }

private:
    void GoTo(Screen s);
    void RunInitStep();
    void DrawBootLoading(UiContext& ui);
    void DrawNameEntry(UiContext& ui);
    void DrawMainMenu(UiContext& ui);
    void DrawMissionSelect(UiContext& ui);
    void DrawLoading(UiContext& ui);
    void DrawDebrief(UiContext& ui);
    void DrawPause(UiContext& ui);

    helix::renderer::D3D12Renderer* renderer_ = nullptr;
    FontAtlas font_;
    Profile profile_;
    std::vector<helix::game::MissionDef> missions_;

    Screen screen_ = Screen::BootLoading;
    Screen returnScreen_ = Screen::MainMenu;

    // BootLoading staged init
    int initStep_ = 0;
    float splashTimer_ = 0.f;
    std::string loadStatus_ = "STARTING";
    float loadingTimer_ = 0.f;
    int selectedMission_ = 0;
    const helix::game::MissionDef* pendingMission_ = nullptr;
    helix::game::MissionSystem::DebriefStats debriefStats_{};
    helix::game::MissionDef debriefMission_{};
    bool debriefComplete_ = false;
    bool wantsQuit_ = false;
    bool firstRun_ = true;

    // Name-entry "cleared for flight" ceremony state
    bool nameDone_ = false;
    float ceremonyT_ = 0.f;
    std::string inductionName_;

    // Crest/logo UI texture (optional — UINT_MAX when no image found)
    UINT logoTexture_ = UINT_MAX;
    UINT logoW_ = 0, logoH_ = 0;
};

} // namespace helix::ui
