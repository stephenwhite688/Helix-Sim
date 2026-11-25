#pragma once
#include <windows.h>
#include <functional>

namespace helix::window {

struct WindowCallbacks {
    std::function<void(UINT msg, WPARAM wParam, LPARAM lParam)> onRawMessage;
    std::function<void(int vk, bool isDown)> onKey;
    std::function<void(wchar_t ch)> onChar;                       // WM_CHAR text input
    std::function<void(int x, int y)> onMouseMove;
    std::function<void(int button, bool isDown, int x, int y)> onMouseButton; // 0=L,1=R,2=M
    std::function<void()> onClose;
    std::function<void(int width, int height)> onResize;
};

class Window {
public:
    bool Create(HINSTANCE hInstance, int width, int height, const wchar_t* title);
    void SetCallbacks(WindowCallbacks callbacks) { callbacks_ = std::move(callbacks); }

    // Pumps the Win32 message queue. Returns false when a WM_QUIT was received.
    bool PumpMessages();

    HWND Handle() const { return hwnd_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    WindowCallbacks callbacks_;
};

} // namespace helix::window
