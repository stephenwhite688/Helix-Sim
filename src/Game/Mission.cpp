#include "Mission.h"
#include <algorithm>

namespace helix::game {

std::vector<MissionDef> CareerMissions() {
    return {
        { 1, "First Flight", "Act I — Test Pilot",
          "Welcome to Vanguard Space Command, Pilot. You're cleared for the "
          "experimental airframe. Climb to 300 m, then bring her down on "
          "the pad like you mean it. Gear down, watch your speed.",
          "Climb to 300 m, then land safely." },
        { 2, "Pattern Work", "Act I — Test Pilot",
          "Handling trial: three touch-and-goes around the field. Stay in "
          "the pattern, keep it smooth — the engineers are watching your "
          "technique.",
          "Complete 3 touch-and-go landings." },
        { 3, "River Run", "Act I — Test Pilot",
          "Low-level transit: follow the valley floor between the ridgelines "
          "and cross the river gorge. Mind the terrain — Vtol is your "
          "friend down here.",
          "Reach the far marker (800 m downrange), then land." },
        { 4, "Scramble", "Act II — Defend Earth",
          "Unknown contacts on scope. Weapons free. Intercept the bandits "
          "before they reach the station approach corridor.",
          "Destroy 3 hostile contacts." },
        { 5, "Lunar Shuttle", "Act III — Earth Orbit & Moon",
          "Supply run: dock at the Vanguard station in low Earth orbit, "
          "transfer to lunar orbit, and touch down on the surface. Mind "
          "your delta-v — the Moon doesn't forgive.",
          "Dock at station, then land on the Moon." },
    };
}

void MissionSystem::Start(const MissionDef& def) {
    def_ = def;
    hasDef_ = true;
    status_ = MissionStatus::Active;
    progress_ = MissionProgress{};
    elapsed_ = 0.f;
}

void MissionSystem::Stop() {
    hasDef_ = false;
    status_ = MissionStatus::Inactive;
}

MissionStatus MissionSystem::Update(float altitudeM, float speedMps, bool onGround,
                                    float dt, MissionProgress& out) {
    out = progress_;
    if (!hasDef_ || status_ != MissionStatus::Active) return status_;

    elapsed_ += dt;
    progress_.altitudeReachedM = std::max(progress_.altitudeReachedM, altitudeM);

    // Touchdown detection: airborne -> grounded transition after a real
    // sortie (>3 s airborne) counts as a landing.
    if (!onGround) {
        sortieAirtime_ += dt;
        progress_.airtimeSeconds += dt;
    } else if (!wasOnGround_ && sortieAirtime_ > 3.f) {
        progress_.landingCount++;
        progress_.hasLanded = true;
        sortieAirtime_ = 0.f;
    }
    wasOnGround_ = onGround;
    out = progress_;

    // v1 completion rules keyed on mission id (mission-type system proper
    // lands with Act II/III content).
    switch (def_.id) {
        case 1: // First Flight: climb 300m then land
            if (progress_.altitudeReachedM >= 300.f && progress_.landingCount >= 1) {
                status_ = MissionStatus::Complete;
            }
            break;
        case 2: // Pattern Work: 3 landings
            if (progress_.landingCount >= 3) status_ = MissionStatus::Complete;
            break;
        case 3: // River Run: sustained low-level transit then land —
                // approximated by airtime + a landing
            if (progress_.airtimeSeconds > 20.f && progress_.landingCount >= 1) {
                status_ = MissionStatus::Complete;
            }
            break;
        case 4: // Scramble: combat — can't complete until combat exists
            if (elapsed_ > 600.f) status_ = MissionStatus::Failed; // 10 min timeout
            break;
        case 5: // Lunar Shuttle: space ops — far future
            if (elapsed_ > 600.f) status_ = MissionStatus::Failed;
            break;
    }

    if (status_ == MissionStatus::Complete || status_ == MissionStatus::Failed) {
        debrief_.airtime = progress_.airtimeSeconds;
        debrief_.topAltitude = progress_.altitudeReachedM;
        debrief_.landed = progress_.hasLanded;
        debrief_.score = (status_ == MissionStatus::Complete) ? 1000 : 0;
        debrief_.penalties = progress_.failed ? 0 : 0;
    }
    return status_;
}

} // namespace helix::game
