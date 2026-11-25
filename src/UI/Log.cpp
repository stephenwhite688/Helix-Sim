#include "Log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace helix::log {
namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
bool g_ready = false;

void Write(const char* tag, const char* fmt, va_list ap) {
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    char line[1200];
    SYSTEMTIME st{};
    GetLocalTime(&st);
    snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] %s %s\n",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag, msg);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) {
            fputs(line, g_file);
            fflush(g_file);
        }
    }
    OutputDebugStringA(line);
}
} // namespace

void Init() {
    if (g_ready) return;
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring p(exePath);
    size_t slash = p.find_last_of(L"\\/");
    std::wstring logPath = (slash == std::wstring::npos) ? L"HelixSim.log"
                                                        : p.substr(0, slash + 1) + L"HelixSim.log";
    _wfopen_s(&g_file, logPath.c_str(), L"w");
    g_ready = true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) { fclose(g_file); g_file = nullptr; }
}

void Info(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); Write("INFO ", fmt, ap); va_end(ap); }
void Warn(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); Write("WARN ", fmt, ap); va_end(ap); }
void Error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); Write("ERROR", fmt, ap); va_end(ap); }

} // namespace helix::log
