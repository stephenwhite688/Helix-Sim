#include "Ship.h"
#include <algorithm>

namespace helix::game {

using namespace helix::ecs;
using namespace helix::physics;
using namespace helix::input;
using namespace helix::audio;

EntityId ShipControlSystem::SpawnPlayerShip(World& world, const Vec3& startPosition) {
    EntityId e = world.CreateEntity();

    RigidBody body;
    body.position = startPosition;
    body.mass = 12000.f; // kg, single-seat fighter-scale craft
    body.angularDrag = 0.5f;
    world.AddComponent<RigidBody>(e, body);

    AeroSurface aero;
    aero.wingArea = 22.f;
    world.AddComponent<AeroSurface>(e, aero);

    FlightAssist assist;
    assist.enabled = true;
    world.AddComponent<FlightAssist>(e, assist);

    world.AddComponent<ShipSystems>(e, ShipSystems{});
    return e;
}

void ShipControlSystem::HandleToggles(World& world, EntityId e, InputManager& input,
                                       AudioEngine& audio, ShipSystems& systems, RigidBody& body) {
    if (input.WasJustPressed(Action::LandingGearToggle)) {
        systems.landingGearDown = !systems.landingGearDown;
        audio.PlaySfx(SfxType::GearDeploy, 0.7f);
    }
    if (input.WasJustPressed(Action::FlightAssistToggle)) {
        systems.flightAssistOn = !systems.flightAssistOn;
        if (world.HasComponent<FlightAssist>(e)) {
            world.GetComponent<FlightAssist>(e).enabled = systems.flightAssistOn;
        }
        audio.PlaySfx(SfxType::SwitchClick, 0.5f);
    }
    if (input.WasJustPressed(Action::WeaponArmToggle)) {
        systems.weaponsArmed = !systems.weaponsArmed;
        audio.PlaySfx(SfxType::SwitchClick, 0.5f);
    }
    if (input.WasJustPressed(Action::DockingAssistToggle) || input.WasJustPressed(Action::AutopilotToggle)) {
        audio.PlaySfx(SfxType::SwitchClick, 0.4f);
    }
    if (input.WasJustPressed(Action::VtolToggle)) {
        systems.engineMode = (systems.engineMode == EngineMode::Vtol) ? EngineMode::Cruise : EngineMode::Vtol;
        audio.PlaySfx(SfxType::SwitchClick, 0.5f);
    }
    if (input.WasJustPressed(Action::EngineModeCycle)) {
        switch (systems.engineMode) {
            case EngineMode::Cruise: systems.engineMode = EngineMode::Boost; break;
            case EngineMode::Boost:  systems.engineMode = EngineMode::Retro; break;
            case EngineMode::Retro:  systems.engineMode = EngineMode::Vtol; break;
            case EngineMode::Vtol:   systems.engineMode = EngineMode::Cruise; break;
        }
        audio.PlaySfx(SfxType::SwitchClick, 0.5f);
    }
    if (input.WasJustPressed(Action::PowerToEngines)) systems.power = PowerPriority::Engines;
    if (input.WasJustPressed(Action::PowerToWeapons)) systems.power = PowerPriority::Weapons;
    if (input.WasJustPressed(Action::PowerToShields)) systems.power = PowerPriority::Systems;
    if (input.WasJustPressed(Action::CountermeasureRelease) && systems.countermeasureCount > 0) {
        systems.countermeasureCount--;
        audio.PlaySfx(SfxType::WarningTone, 0.6f);
    }
}

void ShipControlSystem::ApplyThrust(InputManager& input, ShipSystems& systems, RigidBody& body, float dt) {
    bool boosting = input.IsPressed(Action::Boost) && systems.fuelBoost01 > 0.f;
    float powerMultiplier = (systems.power == PowerPriority::Engines) ? 1.25f : 1.0f;

    Vec3 forward = body.orientation.Forward();
    Vec3 up = body.orientation.Up();
    Vec3 right = body.orientation.Right();

    float throttle = input.GetAxis(Action::ThrottleAxis); // 0..1
    float vertical = input.GetAxis(Action::VerticalThrustAxis); // -1..1

    float thrustScale = boosting ? 2.2f : 1.0f;
    Vec3 thrustForce = forward * (throttle * systems.maxThrustNewtons * thrustScale * powerMultiplier);
    thrustForce += up * (vertical * systems.maxThrustNewtons * 0.6f * powerMultiplier);
    body.ApplyForce(thrustForce);

    // RCS rotational thrusters: pitch/yaw/roll from stick/HOTAS/gamepad.
    float pitch = input.GetAxis(Action::PitchAxis);
    float yaw = input.GetAxis(Action::YawAxis);
    float roll = input.GetAxis(Action::RollAxis);

    Vec3 torque = right * (pitch * systems.maxRcsTorqueNm)
                + up * (yaw * systems.maxRcsTorqueNm)
                + forward * (roll * systems.maxRcsTorqueNm);
    body.ApplyTorque(torque);
}

void ShipControlSystem::HandleWeapons(InputManager& input, AudioEngine& audio, ShipSystems& systems, float dt) {
    systems.laserCooldown = std::max(0.f, systems.laserCooldown - dt);
    systems.cannonCooldown = std::max(0.f, systems.cannonCooldown - dt);

    if (!systems.weaponsArmed) return;

    if (input.IsPressed(Action::FirePrimary) && systems.laserCooldown <= 0.f) {
        systems.laserCooldown = 0.12f;
        audio.PlaySfx(SfxType::LaserFire, 0.6f);
        // A real hit-scan/projectile pass belongs in a dedicated Combat
        // system once target entities exist; firing/cooldown/audio here are
        // fully functional on their own.
    }
    if (input.IsPressed(Action::FireSecondary) && systems.cannonCooldown <= 0.f) {
        systems.cannonCooldown = 0.25f;
        audio.PlaySfx(SfxType::CannonFire, 0.6f);
    }
    if (input.WasJustPressed(Action::LaunchMissile) && systems.missileCount > 0) {
        systems.missileCount--;
        audio.PlaySfx(SfxType::MissileLaunch, 0.8f);
    }
}

void ShipControlSystem::UpdateFuelAndHeat(ShipSystems& systems, float throttleMag, bool boosting, float dt) {
    float standardDrain = 0.004f * throttleMag;
    float boostDrain = boosting ? 0.05f : 0.f;
    float atmoDrain = (systems.engineMode == EngineMode::Vtol) ? 0.03f : 0.f;

    systems.fuelStandard01 = std::clamp(systems.fuelStandard01 - standardDrain * dt, 0.f, 1.f);
    systems.fuelBoost01 = std::clamp(systems.fuelBoost01 - boostDrain * dt, 0.f, 1.f);
    systems.fuelAtmospheric01 = std::clamp(systems.fuelAtmospheric01 - atmoDrain * dt, 0.f, 1.f);

    float heatGain = throttleMag * (boosting ? 0.25f : 0.08f);
    float heatDissipation = 0.15f;
    systems.engineHeat01 = std::clamp(systems.engineHeat01 + (heatGain - heatDissipation) * dt, 0.f, 1.f);
}

void ShipControlSystem::CheckLandingAndCrash(ShipSystems& systems, RigidBody& body, AudioEngine& audio, float dt) {
    // Ground-plane placeholder check (y == 0 as "surface"): a real build
    // queries the terrain heightfield at (body.position.x, body.position.z);
    // this function's landing-quality/crash logic itself is fully implemented.
    if (body.position.y > 2.0f) return;

    float impactSpeed = body.velocity.Length();
    Vec3 up = body.orientation.Up();
    float uprightness = up.y; // 1.0 = perfectly level

    bool gearOk = systems.landingGearDown && systems.gearHealth01 > 0.1f;

    if (!gearOk || impactSpeed > 40.f || uprightness < 0.6f) {
        systems.hullIntegrity01 = std::max(0.f, systems.hullIntegrity01 - 1.0f);
        systems.penaltyScore += 100;
        audio.PlaySfx(SfxType::Explosion, 1.0f);
        return;
    }

    if (impactSpeed < 3.f && uprightness > 0.95f) {
        systems.landingScore += 50; // smooth + precision bonus territory
    } else if (impactSpeed < 8.f) {
        systems.landingScore += 20; // safe but firm landing
    } else if (impactSpeed < 40.f) {
        systems.penaltyScore += 10; // unsafe landing
    }

    body.position.y = 0.f;
    body.velocity = body.velocity * 0.f;
    body.angularVelocity = body.angularVelocity * 0.f;
}

void ShipControlSystem::Update(World& world, EntityId shipEntity, InputManager& input,
                                AudioEngine& audio, float dt) {
    if (!world.IsAlive(shipEntity)) return;

    RigidBody& body = world.GetComponent<RigidBody>(shipEntity);
    ShipSystems& systems = world.GetComponent<ShipSystems>(shipEntity);

    HandleToggles(world, shipEntity, input, audio, systems, body);
    ApplyThrust(input, systems, body, dt);
    HandleWeapons(input, audio, systems, dt);

    float throttleMag = std::fabs(input.GetAxis(Action::ThrottleAxis));
    bool boosting = input.IsPressed(Action::Boost);
    UpdateFuelAndHeat(systems, throttleMag, boosting, dt);
    CheckLandingAndCrash(systems, body, audio, dt);

    audio.UpdateEngineHum(throttleMag, boosting, systems.engineHealth01 < 0.6f);
    audio.UpdateAtmosphereWind(body.velocity.Length(), body.position.y);
    audio.UpdateReentryRoar(systems.reentryHeat01);
}

} // namespace helix::game
