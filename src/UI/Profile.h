#pragma once
#include <string>

namespace helix::ui {

// Pilot profile persisted to %LOCALAPPDATA%\HelixSim\profile.cfg —
// callsign entered on first run (SPEC §3.6 step 2), reused for save
// slots, records and debrief headers.
struct Profile {
    std::string callsign = "Pilot";
    bool introCompleted = false;   // cinematic + first flight checks done
    bool flightChecksDone = false; // tutorial bypass unlocked

    static std::string ConfigDir();
    bool Load();
    bool Save() const;

    // Validates a callsign per spec: 1-16 chars, alnum + space/-/_.
    static bool IsValidCallsign(const std::string& s);
};

} // namespace helix::ui
