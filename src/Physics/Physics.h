#pragma once
#include "Math.h"
#include "../ECS/ECS.h"

namespace helix::physics {

using helix::math::Vec3;
using helix::math::Quat;

// Rigid body state: full 6-DOF Newtonian body with linear + angular motion.
struct RigidBody {
    Vec3 position{ 0.f, 0.f, 0.f };
    Vec3 velocity{ 0.f, 0.f, 0.f };
    Quat orientation = Quat::Identity();
    Vec3 angularVelocity{ 0.f, 0.f, 0.f };

    float mass = 1000.f;          // kg
    float linearDrag = 0.0f;      // applied only when inside an atmosphere
    float angularDrag = 0.4f;     // rotational damping, always applied (RCS damping)

    Vec3 accumulatedForce{ 0.f, 0.f, 0.f };
    Vec3 accumulatedTorque{ 0.f, 0.f, 0.f };

    void ApplyForce(const Vec3& worldForce) { accumulatedForce += worldForce; }
    void ApplyTorque(const Vec3& worldTorque) { accumulatedTorque += worldTorque; }

    // Force applied at a point offset from the center of mass (world space),
    // producing both linear force and the resulting torque.
    void ApplyForceAtPoint(const Vec3& worldForce, const Vec3& worldOffsetFromCOM) {
        accumulatedForce += worldForce;
        accumulatedTorque += Vec3::Cross(worldOffsetFromCOM, worldForce);
    }
};

// Per-planet/atmosphere parameters used for drag, lift and gravity while a
// body is within range of a gravity well.
struct AtmosphereField {
    Vec3 center{ 0.f, 0.f, 0.f };
    float planetRadius = 6000.f;        // meters
    float atmosphereRadius = 6100.f;    // meters, altitude where density hits 0
    float seaLevelDensity = 1.225f;     // kg/m^3, Earth-like default
    float surfaceGravity = 9.81f;       // m/s^2
    float gravitationalReach = 250000.f;// meters, beyond this the field has no effect
};

struct AeroSurface {
    float wingArea = 18.f;      // m^2, effective lifting/drag area
    float liftCoefficient = 1.1f;
    float dragCoefficient = 0.35f;
    float stallAngleRadians = 0.30f; // ~17 degrees
};

struct FlightAssist {
    bool enabled = true;
    float linearDamping = 0.6f;   // fraction of velocity cancelled per second when no input
    float angularDamping = 2.0f;  // rad/s^2 damping toward zero rotation when no input
};

// Computes atmospheric density at an altitude above a planet's surface using
// an exponential falloff model, returning 0 outside the atmosphere shell.
float DensityAtAltitude(const AtmosphereField& field, float altitude);

// Integrates every RigidBody one physics step, applying:
//  - accumulated forces/torques from gameplay/input systems this frame
//  - gravity from any AtmosphereField the body is within range of
//  - aerodynamic drag/lift for bodies that also carry an AeroSurface component
//    and are inside atmosphere
//  - flight-assist damping when FlightAssist is enabled and present
// Then clears the accumulators for next frame.
class PhysicsSystem {
public:
    void Step(helix::ecs::World& world, float dt);

    // Convenience: attach the single active atmosphere field bodies should
    // respond to (a real build would spatially query multiple fields; one
    // active field is sufficient for a single-planet flight scenario).
    void SetActiveAtmosphere(const AtmosphereField& field) { atmosphere_ = field; hasAtmosphere_ = true; }
    void ClearActiveAtmosphere() { hasAtmosphere_ = false; }

private:
    AtmosphereField atmosphere_{};
    bool hasAtmosphere_ = false;
};

} // namespace helix::physics
