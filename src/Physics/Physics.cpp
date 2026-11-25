#include "Physics.h"
#include <algorithm>

namespace helix::physics {

float DensityAtAltitude(const AtmosphereField& field, float altitude) {
    float atmosphereDepth = field.atmosphereRadius - field.planetRadius;
    if (altitude < 0.f) altitude = 0.f;
    if (altitude > atmosphereDepth) return 0.f;
    // Exponential falloff, normalized so density == seaLevelDensity at altitude 0
    // and approaches 0 near the top of the atmosphere shell.
    float t = altitude / atmosphereDepth;
    float falloff = std::exp(-5.0f * t);
    return field.seaLevelDensity * falloff;
}

void PhysicsSystem::Step(helix::ecs::World& world, float dt) {
    if (dt <= 0.f) return;

    world.ForEach<RigidBody>([&](helix::ecs::EntityId e, RigidBody& body) {
        // --- Gravity ---
        if (hasAtmosphere_) {
            Vec3 toBody = body.position - atmosphere_.center;
            float dist = toBody.Length();
            if (dist > 1.0f && dist < atmosphere_.gravitationalReach) {
                // g falls off with inverse-square beyond the surface, matches
                // surfaceGravity exactly at planetRadius.
                float g = atmosphere_.surfaceGravity *
                          (atmosphere_.planetRadius * atmosphere_.planetRadius) /
                          (dist * dist);
                Vec3 gravityDir = (-toBody).Normalized();
                body.ApplyForce(gravityDir * (g * body.mass));
            }

            // --- Atmospheric drag + lift (only if this entity also has AeroSurface) ---
            if (world.HasComponent<AeroSurface>(e)) {
                float altitude = dist - atmosphere_.planetRadius;
                float density = DensityAtAltitude(atmosphere_, altitude);
                if (density > 0.f) {
                    AeroSurface& aero = world.GetComponent<AeroSurface>(e);
                    float speed = body.velocity.Length();
                    if (speed > 0.01f) {
                        Vec3 velDir = body.velocity.Normalized();

                        // Drag: opposes velocity, scales with v^2.
                        float dragMag = 0.5f * density * speed * speed *
                                        aero.dragCoefficient * aero.wingArea;
                        body.ApplyForce(-velDir * dragMag);

                        // Lift: perpendicular to velocity, in the plane containing
                        // velocity and the ship's local "up", scaled by angle of
                        // attack approximated via dot(shipUp, velocity-perp).
                        Vec3 shipUp = body.orientation.Up();
                        Vec3 liftDir = (shipUp - velDir * Vec3::Dot(shipUp, velDir)).Normalized();
                        float angleOfAttack = std::acos(std::clamp(
                            Vec3::Dot(body.orientation.Forward().Normalized(), velDir), -1.f, 1.f));
                        float aoaFactor = angleOfAttack > aero.stallAngleRadians
                            ? std::max(0.f, 1.f - (angleOfAttack - aero.stallAngleRadians) * 2.f) // stall dropoff
                            : angleOfAttack / std::max(0.001f, aero.stallAngleRadians);
                        float liftMag = 0.5f * density * speed * speed *
                                        aero.liftCoefficient * aero.wingArea * aoaFactor;
                        body.ApplyForce(liftDir * liftMag);
                    }
                }
            }
        }

        // --- Flight assist damping (space "dampeners" / atmosphere auto-level assist) ---
        if (world.HasComponent<FlightAssist>(e)) {
            FlightAssist& assist = world.GetComponent<FlightAssist>(e);
            if (assist.enabled) {
                body.ApplyForce(-body.velocity * (assist.linearDamping * body.mass));
                body.angularVelocity = body.angularVelocity *
                    std::max(0.f, 1.f - assist.angularDamping * dt);
            }
        }

        // --- Newtonian integration (semi-implicit Euler) ---
        Vec3 linearAccel = body.accumulatedForce * (1.f / body.mass);
        body.velocity += linearAccel * dt;
        if (body.linearDrag > 0.f) {
            body.velocity *= std::max(0.f, 1.f - body.linearDrag * dt);
        }
        body.position += body.velocity * dt;

        // Angular: simple diagonal inertia approximation (unit inertia tensor
        // scaled by mass) — sufficient for arcade-accurate flight response.
        float invInertia = 1.f / (body.mass * 0.5f);
        Vec3 angularAccel = body.accumulatedTorque * invInertia;
        body.angularVelocity += angularAccel * dt;
        body.angularVelocity *= std::max(0.f, 1.f - body.angularDrag * dt);
        body.orientation = body.orientation.Integrate(body.angularVelocity, dt);

        body.accumulatedForce = Vec3{ 0.f, 0.f, 0.f };
        body.accumulatedTorque = Vec3{ 0.f, 0.f, 0.f };
    });
}

} // namespace helix::physics
