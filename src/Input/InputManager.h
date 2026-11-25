#pragma once
#include <windows.h>
#include <xinput.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>

namespace helix::input {

// Actions the game cares about. Any physical input (HOTAS axis/button,
// gamepad stick/trigger/button, or keyboard key) can be bound to one of
// these at runtime and saved/loaded via BindingProfile.
enum class Action {
    PitchAxis, YawAxis, RollAxis, ThrottleAxis, VerticalThrustAxis,
    FirePrimary, FireSecondary, LaunchMissile, CycleWeaponGroup,
    LandingGearToggle, FlightAssistToggle, DockingAssistToggle,
    AutopilotToggle, CountermeasureRelease, WeaponArmToggle,
    RadarRangeIncrease, RadarRangeDecrease, RadarModeCycle,
    MfdPageCycle, ViewFreeLookX, ViewFreeLookY, EngineModeCycle,
    PowerToEngines, PowerToWeapons, PowerToShields, ShieldDirectionCycle,
    Boost, Retro, VtolToggle,
    Count
};

struct AxisState {
    float value = 0.f; // -1..1 for bipolar axes, 0..1 for throttle-style axes
};

struct BindingProfile {
    std::string name = "default";
    // Keyboard virtual-key bindings for digital actions.
    std::unordered_map<Action, int> keyBindings;
    // Mouse-axis bindings (used for free-look / roll assist).
    bool mouseControlsFreeLook = true;
    float mouseSensitivity = 0.0025f;
    // Deadzone/curve applied to every analog axis from HOTAS or gamepad.
    float deadzone = 0.08f;
    float axisCurveExponent = 1.6f; // >1 = gentler near center, sharper at extremes
};

// Wraps Win32 Raw Input registration (HOTAS via RIM_TYPEHID, mouse, keyboard)
// and XInput polling (gamepads) behind a single bindable action interface.
class InputManager {
public:
    bool Initialize(HWND hwnd);
    void Shutdown();

    // Called from the window's WM_INPUT handler.
    void HandleRawInput(HRAWINPUT hRawInput);
    // Called on WM_KEYDOWN/WM_KEYUP.
    void HandleKey(int vk, bool isDown);

    // Poll XInput gamepads and merge into axis/button state. Call once per frame.
    void PollGamepads();

    // Advance per-frame derived state (mouse delta reset, edge-trigger tracking).
    void NewFrame();

    float GetAxis(Action a) const;
    bool IsPressed(Action a) const;      // held down this frame
    bool WasJustPressed(Action a) const; // edge-triggered, true for one frame only

    BindingProfile& Bindings() { return bindings_; }
    void LoadBindingProfile(const std::string& path);
    void SaveBindingProfile(const std::string& path) const;

private:
    void RegisterRawInputDevices(HWND hwnd);
    float ApplyDeadzoneAndCurve(float raw) const;

    HWND hwnd_ = nullptr;
    BindingProfile bindings_;

    std::unordered_map<Action, float> axisValues_;
    std::unordered_map<Action, bool> buttonHeld_;
    std::unordered_map<Action, bool> buttonHeldPrevFrame_;

    // Raw HID axis values read straight from the HOTAS report, before
    // deadzone/curve shaping and before being mapped to an Action.
    std::unordered_map<uint16_t, LONG> hidAxisRaw_;

    XINPUT_STATE gamepadState_{};
    bool gamepadConnected_ = false;

    POINT lastMousePos_{};
    bool mouseInitialized_ = false;
};

} // namespace helix::input
