cmake_minimum_required(VERSION 3.20)
project(HelixSim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR "HelixSim targets Windows 10/11 64-bit only (Win32 + DirectX 12 + XAudio2).")
endif()

add_executable(HelixSim WIN32
    src/main.cpp
    src/Window/Window.cpp
    src/Renderer/D3D12Renderer.cpp
    src/Physics/Physics.cpp
    src/Input/InputManager.cpp
    src/Audio/AudioEngine.cpp
    src/Game/Ship.cpp
    src/Game/World.cpp
    src/HUD/HUDRenderer.cpp
    src/UI/FontAtlas.cpp
    src/UI/Ui.cpp
    src/UI/BootFlow.cpp
    src/UI/Profile.cpp
    src/UI/Log.cpp
    src/UI/HudOverlay.cpp
    src/Game/Mission.cpp
)

# Only `src` is an include root. Do NOT add the per-module dirs (src\Physics,
# src\Renderer, ...): on Windows' case-insensitive filesystem, MSVC's own
# <cmath> does `#include <math.h>`, which would resolve to src\Physics\Math.h
# via the include path and silently skip the real C math header.
target_include_directories(HelixSim PRIVATE src)

target_link_libraries(HelixSim PRIVATE
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    xaudio2.lib
    xinput.lib
    user32.lib
    gdi32.lib
    hid.lib
)

target_compile_definitions(HelixSim PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Copy the HLSL shader source next to the exe so the renderer can load it at runtime.
add_custom_command(TARGET HelixSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/Renderer/shaders"
        "$<TARGET_FILE_DIR:HelixSim>/shaders"
)

# Copy art assets (logo/icon) next to the exe for the splash screen and
# window icon.
add_custom_command(TARGET HelixSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/Resources"
        "$<TARGET_FILE_DIR:HelixSim>/Resources"
)
