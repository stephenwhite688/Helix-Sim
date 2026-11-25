#include "Window.h"

namespace helix::window {

namespace {
constexpr wchar_t kClassName[] = L"HelixSimWindowClass";
}

LRESULT CALLBACK Window::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (callbacks_.onRawMessage) callbacks_.onRawMessage(msg, wParam, lParam);

    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (callbacks_.onKey) callbacks_.onKey(static_cast<int>(wParam), true);
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (callbacks_.onKey) callbacks_.onKey(static_cast<int>(wParam), false);
            return 0;
        case WM_CHAR:
            if (callbacks_.onChar) callbacks_.onChar(static_cast<wchar_t>(wParam));
            return 0;
        case WM_MOUSEMOVE:
            if (callbacks_.onMouseMove)
                callbacks_.onMouseMove(static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
            return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
            int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? 0
                      : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? 1 : 2;
            bool down = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN);
            if (callbacks_.onMouseButton)
                callbacks_.onMouseButton(button, down,
                    static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
            return 0;
        }
        case WM_SIZE:
            width_ = LOWORD(lParam);
            height_ = HIWORD(lParam);
            if (callbacks_.onResize) callbacks_.onResize(width_, height_);
            return 0;
        case WM_DESTROY:
            if (callbacks_.onClose) callbacks_.onClose();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

bool Window::Create(HINSTANCE hInstance, int width, int height, const wchar_t* title) {
    WNDCLASSEX wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::WndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassEx(&wc)) return false;

    width_ = width;
    height_ = height;

    RECT rect{ 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowEx(
        0, kClassName, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_) return false;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

bool Window::PumpMessages() {
    MSG msg{};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

} // namespace helix::window
