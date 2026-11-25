#pragma once
#include <cstdarg>

// Minimal file+debug-output logger. Writes HelixSim.log next to the exe and
// mirrors to OutputDebugString so VS's Output window shows it live.
namespace helix::log {

void Init(); // opens the log file (call once at startup)
void Shutdown();
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);

} // namespace helix::log
