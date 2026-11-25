#include "Profile.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace helix::ui {

std::string Profile::ConfigDir() {
    char path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        std::string dir = std::string(path) + "\\HelixSim";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    return ".";
}

bool Profile::IsValidCallsign(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_';
    });
}

bool Profile::Load() {
    std::ifstream f(ConfigDir() + "\\profile.cfg");
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("callsign=", 0) == 0) {
            callsign = line.substr(9);
            if (!IsValidCallsign(callsign)) callsign = "Pilot";
        } else if (line == "introCompleted=1") {
            introCompleted = true;
        } else if (line == "flightChecksDone=1") {
            flightChecksDone = true;
        }
    }
    return true;
}

bool Profile::Save() const {
    std::ofstream f(ConfigDir() + "\\profile.cfg", std::ios::trunc);
    if (!f.is_open()) return false;
    f << "callsign=" << callsign << "\n";
    f << "introCompleted=" << (introCompleted ? 1 : 0) << "\n";
    f << "flightChecksDone=" << (flightChecksDone ? 1 : 0) << "\n";
    return true;
}

} // namespace helix::ui
