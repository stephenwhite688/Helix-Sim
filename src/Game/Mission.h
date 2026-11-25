#pragma once
#include <string>
#include <vector>

namespace helix::game {

struct MissionDef {
    int id = 0;
    std::string name;
    std::string act;          // "Act I" / "Act II" / "Act III"
    std::string briefing;     // shown on mission select
    std::string objective;    // HUD objective line
};

// v1 mission logic: altitude + landing checks against the live ship state.
// Types:
//   ClimbAndLand — reach targetAltitudeM, then land safely
//   Endurance    — stay airborne for durationSeconds
struct MissionProgress {
    float altitudeReachedM = 0.f;
    float airtimeSeconds = 0.f;
    int landingCount = 0;
    bool hasLanded = false;
    bool failed = false;
};

enum class MissionStatus { Inactive, Active, Complete, Failed };

// Data-driven mission list for campaign v1 (SPEC §3.4 — three acts,
// starting with Act I atmospheric handling trials).
std::vector<MissionDef> CareerMissions();

class MissionSystem {
public:
    void Start(const MissionDef& def);
    void Stop();

    MissionStatus Update(float altitudeM, float speedMps, bool onGround, float dt,
                         MissionProgress& out);
    const MissionDef* Active() const { return hasDef_ ? &def_ : nullptr; }
    MissionStatus Status() const { return status_; }

    // Scoring snapshot for the debrief screen.
    struct DebriefStats {
        int score = 0;
        int penalties = 0;
        float airtime = 0.f;
        float topAltitude = 0.f;
        bool landed = false;
    };
    DebriefStats LastDebrief() const { return debrief_; }

private:
    bool hasDef_ = false;
    MissionDef def_{};
    MissionStatus status_ = MissionStatus::Inactive;
    MissionProgress progress_{};
    DebriefStats debrief_{};
    float elapsed_ = 0.f;
    float sortieAirtime_ = 0.f;   // airtime accumulated since last touchdown
    bool wasOnGround_ = true;
};

} // namespace helix::game
