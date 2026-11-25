#pragma once
#include "../ECS/ECS.h"
#include "../Physics/Physics.h"
#include "../Input/InputManager.h"
#include "../Audio/AudioEngine.h"
#include "../Physics/Math.h"

namespace helix::game {

using helix::math::Vec3;

enum class EngineMode { Cruise, Boost, Retro, Vtol };
enum class PowerPriority { Balanced, Engines, Weapons, Systems };

struct ShipSystems {
    EngineMode engineMode = EngineMode::Cruise;
    PowerPriority power = PowerPriority::Balanced;

    bool landingGearDown = true;
    bool flightAssistOn = true;
    bool weaponsArmed = false;
    bool shieldsUp = true;

    float hullIntegrity01 = 1.0f;
    float shieldFront01 = 1.0f;
    float shieldRear01 = 1.0f;

    // Subsystem health: engine/reactor/weapons/avionics/gear
    float engineHealth01 = 1.0f;
    float reactorHealth01 = 1.0f;
    float weaponsHealth01 = 1.0f;
    float avionicsHealth01 = 1.0f;
    float gearHealth01 = 1.0f;

    float fuelStandard01 = 1.0f;
    float fuelAtmospheric01 = 1.0f;
    float fuelBoost01 = 1.0f;

    float engineHeat01 = 0.0f;
    float reentryHeat01 = 0.0f;

    float maxThrustNewtons = 220000.f;
    float maxRcsTorqueNm = 90000.f;

    // Weapon groups: cooldown timers, ammo counts.
    float laserCooldown = 0.f;
    float cannonCooldown = 0.f;
    int missileCount = 6;
    int countermeasureCount = 8;

    int killScore = 0;
    int landingScore = 0;
    int penaltyScore = 0;
};

// Applies input to thrusters/RCS as forces/torques on the RigidBody, handles
// weapon firing, gear/assist/shield toggles, subsystem heat/fuel drain, and
// crash/landing detection. This is the "Gameplay" module from the spec.
class ShipControlSystem {
public:
    helix::ecs::EntityId SpawnPlayerShip(helix::ecs::World& world, const Vec3& startPosition);

    void Update(helix::ecs::World& world, helix::ecs::EntityId shipEntity,
                helix::input::InputManager& input, helix::audio::AudioEngine& audio,
                float dt);

private:
    void HandleToggles(helix::ecs::World& world, helix::ecs::EntityId e,
                        helix::input::InputManager& input, helix::audio::AudioEngine& audio,
                        ShipSystems& systems, helix::physics::RigidBody& body);
    void ApplyThrust(helix::input::InputManager& input, ShipSystems& systems,
                      helix::physics::RigidBody& body, float dt);
    void HandleWeapons(helix::input::InputManager& input, helix::audio::AudioEngine& audio,
                        ShipSystems& systems, float dt);
    void UpdateFuelAndHeat(ShipSystems& systems, float throttleMag, bool boosting, float dt);
    void CheckLandingAndCrash(ShipSystems& systems, helix::physics::RigidBody& body,
                               helix::audio::AudioEngine& audio, float dt);
};

} // namespace helix::game
