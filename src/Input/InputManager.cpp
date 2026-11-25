#include "InputManager.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "hid.lib")

namespace helix::input {

bool InputManager::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    RegisterRawInputDevices(hwnd);

    // Sensible defaults: WASD + a few keys, so the sim is flyable with
    // keyboard-only before the user opens the binding editor.
    bindings_.keyBindings[Action::PitchAxis]        = 'S'; // handled bipolar below via W/S
    bindings_.keyBindings[Action::YawAxis]          = 'A';
    bindings_.keyBindings[Action::RollAxis]         = 'Q';
    bindings_.keyBindings[Action::ThrottleAxis]     = VK_SHIFT;
    bindings_.keyBindings[Action::FirePrimary]      = VK_SPACE;
    bindings_.keyBindings[Action::LandingGearToggle]= 'G';
    bindings_.keyBindings[Action::FlightAssistToggle]='F';
    bindings_.keyBindings[Action::Boost]            = VK_TAB;
    bindings_.keyBindings[Action::VtolToggle]       = 'V';

    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        axisValues_[static_cast<Action>(i)] = 0.f;
        buttonHeld_[static_cast<Action>(i)] = false;
        buttonHeldPrevFrame_[static_cast<Action>(i)] = false;
    }

    ZeroMemory(&gamepadState_, sizeof(XINPUT_STATE));
    return true;
}

void InputManager::Shutdown() {
    hwnd_ = nullptr;
}

void InputManager::RegisterRawInputDevices(HWND hwnd) {
    RAWINPUTDEVICE devices[3]{};

    // Generic HID joystick/HOTAS (usage page 0x01, usage 0x04 = joystick)
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x04;
    devices[0].dwFlags = RIDEV_INPUTSINK;
    devices[0].hwndTarget = hwnd;

    // Generic HID gamepad-style device (usage page 0x01, usage 0x05)
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x05;
    devices[1].dwFlags = RIDEV_INPUTSINK;
    devices[1].hwndTarget = hwnd;

    // Mouse
    devices[2].usUsagePage = 0x01;
    devices[2].usUsage = 0x02;
    devices[2].dwFlags = RIDEV_INPUTSINK;
    devices[2].hwndTarget = hwnd;

    ::RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE));
}

float InputManager::ApplyDeadzoneAndCurve(float raw) const {
    float sign = raw < 0.f ? -1.f : 1.f;
    float mag = std::fabs(raw);
    if (mag < bindings_.deadzone) return 0.f;
    float scaled = (mag - bindings_.deadzone) / (1.f - bindings_.deadzone);
    scaled = std::pow(std::clamp(scaled, 0.f, 1.f), bindings_.axisCurveExponent);
    return sign * scaled;
}

void InputManager::HandleRawInput(HRAWINPUT hRawInput) {
    UINT size = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) return;

    std::vector<BYTE> buffer(size);
    if (GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
        return;
    }
    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());

    if (raw->header.dwType == RIM_TYPEMOUSE) {
        LONG dx = raw->data.mouse.lLastX;
        LONG dy = raw->data.mouse.lLastY;
        if (bindings_.mouseControlsFreeLook) {
            axisValues_[Action::ViewFreeLookX] = std::clamp(
                axisValues_[Action::ViewFreeLookX] + dx * bindings_.mouseSensitivity, -1.f, 1.f);
            axisValues_[Action::ViewFreeLookY] = std::clamp(
                axisValues_[Action::ViewFreeLookY] + dy * bindings_.mouseSensitivity, -1.f, 1.f);
        }
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
            buttonHeld_[Action::FirePrimary] = true;
        }
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) {
            buttonHeld_[Action::FirePrimary] = false;
        }
    }
    else if (raw->header.dwType == RIM_TYPEHID) {
        // A real shipping build parses the device's HID report descriptor
        // (via HidP_GetCaps / HidP_GetValueCaps) to map each report field to
        // a physical axis/button generically for arbitrary HOTAS hardware.
        // Here we read the raw report bytes and treat the first three
        // 16-bit fields as pitch/roll/yaw and the following byte as a
        // button bitmask, which is how the large majority of HOTAS devices
        // (Thrustmaster/Logitech/VKB) lay out their default report.
        BYTE* report = raw->data.hid.bRawData;
        DWORD reportSize = raw->data.hid.dwSizeHid;
        if (reportSize >= 6) {
            auto readAxis16 = [&](DWORD offset) -> float {
                if (offset + 2 > reportSize) return 0.f;
                uint16_t v = report[offset] | (report[offset + 1] << 8);
                return (static_cast<float>(v) / 32767.5f) - 1.f;
            };
            float pitch = readAxis16(0);
            float roll  = readAxis16(2);
            float yaw   = readAxis16(4);
            axisValues_[Action::PitchAxis] = ApplyDeadzoneAndCurve(pitch);
            axisValues_[Action::RollAxis]  = ApplyDeadzoneAndCurve(roll);
            axisValues_[Action::YawAxis]   = ApplyDeadzoneAndCurve(yaw);
        }
        if (reportSize >= 7) {
            BYTE buttonMask = report[6];
            buttonHeld_[Action::FirePrimary]     = (buttonMask & 0x01) != 0;
            buttonHeld_[Action::FireSecondary]   = (buttonMask & 0x02) != 0;
            buttonHeld_[Action::LandingGearToggle]= (buttonMask & 0x04) != 0;
        }
    }
}

void InputManager::HandleKey(int vk, bool isDown) {
    for (auto& [action, key] : bindings_.keyBindings) {
        if (key == vk) {
            buttonHeld_[action] = isDown;
        }
    }
    // Bipolar keyboard axes for pitch/yaw/roll/throttle when no HOTAS present.
    if (vk == 'W') axisValues_[Action::PitchAxis] = isDown ? 1.f : (axisValues_[Action::PitchAxis] > 0.f ? 0.f : axisValues_[Action::PitchAxis]);
    if (vk == 'S') axisValues_[Action::PitchAxis] = isDown ? -1.f : (axisValues_[Action::PitchAxis] < 0.f ? 0.f : axisValues_[Action::PitchAxis]);
    if (vk == 'D') axisValues_[Action::YawAxis] = isDown ? 1.f : (axisValues_[Action::YawAxis] > 0.f ? 0.f : axisValues_[Action::YawAxis]);
    if (vk == 'A') axisValues_[Action::YawAxis] = isDown ? -1.f : (axisValues_[Action::YawAxis] < 0.f ? 0.f : axisValues_[Action::YawAxis]);
    if (vk == 'E') axisValues_[Action::RollAxis] = isDown ? 1.f : (axisValues_[Action::RollAxis] > 0.f ? 0.f : axisValues_[Action::RollAxis]);
    if (vk == 'Q') axisValues_[Action::RollAxis] = isDown ? -1.f : (axisValues_[Action::RollAxis] < 0.f ? 0.f : axisValues_[Action::RollAxis]);
    if (vk == VK_SHIFT) axisValues_[Action::ThrottleAxis] = isDown ? 1.f : 0.f;
    if (vk == VK_CONTROL) axisValues_[Action::VerticalThrustAxis] = isDown ? -1.f : 0.f;
    if (vk == 'R') axisValues_[Action::VerticalThrustAxis] = isDown ? 1.f : (axisValues_[Action::VerticalThrustAxis] > 0.f ? 0.f : axisValues_[Action::VerticalThrustAxis]);
}

void InputManager::PollGamepads() {
    XINPUT_STATE state{};
    DWORD result = XInputGetState(0, &state);
    gamepadConnected_ = (result == ERROR_SUCCESS);
    if (!gamepadConnected_) return;
    gamepadState_ = state;

    auto normThumb = [](SHORT v) { return std::clamp(static_cast<float>(v) / 32767.f, -1.f, 1.f); };
    auto normTrigger = [](BYTE v) { return static_cast<float>(v) / 255.f; };

    float lx = ApplyDeadzoneAndCurve(normThumb(state.Gamepad.sThumbLX));
    float ly = ApplyDeadzoneAndCurve(normThumb(state.Gamepad.sThumbLY));
    float rx = ApplyDeadzoneAndCurve(normThumb(state.Gamepad.sThumbRX));

    // Left stick = pitch/roll, right stick X = yaw, triggers = throttle/vertical.
    axisValues_[Action::PitchAxis] = ly;
    axisValues_[Action::RollAxis] = lx;
    axisValues_[Action::YawAxis] = rx;
    axisValues_[Action::ThrottleAxis] = normTrigger(state.Gamepad.bRightTrigger);
    axisValues_[Action::VerticalThrustAxis] = normTrigger(state.Gamepad.bLeftTrigger);

    buttonHeld_[Action::FirePrimary] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
    buttonHeld_[Action::LandingGearToggle] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    buttonHeld_[Action::DockingAssistToggle] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
    buttonHeld_[Action::Boost] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    buttonHeld_[Action::FireSecondary] = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
}

void InputManager::NewFrame() {
    buttonHeldPrevFrame_ = buttonHeld_;
    axisValues_[Action::ViewFreeLookX] = 0.f;
    axisValues_[Action::ViewFreeLookY] = 0.f;
}

float InputManager::GetAxis(Action a) const {
    auto it = axisValues_.find(a);
    return it == axisValues_.end() ? 0.f : it->second;
}

bool InputManager::IsPressed(Action a) const {
    auto it = buttonHeld_.find(a);
    return it != buttonHeld_.end() && it->second;
}

bool InputManager::WasJustPressed(Action a) const {
    bool now = IsPressed(a);
    auto it = buttonHeldPrevFrame_.find(a);
    bool prev = it != buttonHeldPrevFrame_.end() && it->second;
    return now && !prev;
}

void InputManager::LoadBindingProfile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string name;
        int actionIdx, key;
        if (line.rfind("axiscfg", 0) == 0) {
            ss >> name >> bindings_.deadzone >> bindings_.axisCurveExponent;
            continue;
        }
        ss >> actionIdx >> key;
        if (actionIdx >= 0 && actionIdx < static_cast<int>(Action::Count)) {
            bindings_.keyBindings[static_cast<Action>(actionIdx)] = key;
        }
    }
}

void InputManager::SaveBindingProfile(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;
    for (auto& [action, key] : bindings_.keyBindings) {
        file << static_cast<int>(action) << " " << key << "\n";
    }
    file << "axiscfg _ " << bindings_.deadzone << " " << bindings_.axisCurveExponent << "\n";
}

} // namespace helix::input
