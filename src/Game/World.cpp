#include "World.h"

namespace helix::game {

using helix::math::Vec3;
using helix::math::Mat4;
using helix::math::Quat;

void World::Initialize(helix::renderer::D3D12Renderer& renderer) {
    atmosphere_.center = Vec3{ 0.f, -6000.f, 0.f }; // planet center 6000m below the flight-start altitude
    atmosphere_.planetRadius = 6000.f;
    atmosphere_.atmosphereRadius = 6120.f;
    atmosphere_.surfaceGravity = 9.81f;
    physics_.SetActiveAtmosphere(atmosphere_);

    playerShip_ = shipControl_.SpawnPlayerShip(ecs_, Vec3{ 0.f, 500.f, 0.f });

    shipMesh_ = renderer.CreateBoxMesh(4.5f);
    terrainMesh_ = renderer.CreateTerrainPatch(64, 8000.f, 220.f, 1337u);
    planetMesh_ = renderer.CreateIcosphereMesh(atmosphere_.planetRadius, 3);
}

void World::RespawnPlayer() {
    auto& body = ecs_.GetComponent<helix::physics::RigidBody>(playerShip_);
    body.position = Vec3{ 0.f, 500.f, 0.f };
    body.velocity = Vec3{ 0.f, 0.f, 0.f };
    body.orientation = Quat::Identity();
    body.angularVelocity = Vec3{ 0.f, 0.f, 0.f };
    body.accumulatedForce = Vec3{ 0.f, 0.f, 0.f };
    body.accumulatedTorque = Vec3{ 0.f, 0.f, 0.f };
    auto& systems = ecs_.GetComponent<ShipSystems>(playerShip_);
    systems.hullIntegrity01 = 1.f;
    systems.engineHeat01 = 0.f;
    systems.reentryHeat01 = 0.f;
}

void World::Update(helix::input::InputManager& input, helix::audio::AudioEngine& audio, float dt) {
    shipControl_.Update(ecs_, playerShip_, input, audio, dt);
    physics_.Step(ecs_, dt);
}

void World::Render(helix::renderer::D3D12Renderer& renderer) {
    using namespace helix::renderer;

    const auto& body = ecs_.GetComponent<helix::physics::RigidBody>(playerShip_);

    // Chase camera: positioned behind and above the ship, looking at it.
    Vec3 behind = body.orientation.Forward() * -18.f;
    Vec3 above = body.orientation.Up() * 6.f;
    Vec3 camPos = body.position + behind + above;
    Mat4 view = Mat4::LookAt(camPos, body.position, Vec3{ 0.f, 1.f, 0.f });
    Mat4 proj = Mat4::Perspective(1.1f, 16.f / 9.f, 0.5f, 200000.f);
    renderer.SetCamera(view, proj);
    renderer.SetDirectionalLight(Vec3{ 0.3f, -0.8f, 0.4f }, Vec3{ 1.f, 0.96f, 0.9f }, 3.2f);

    DrawItem shipDraw;
    shipDraw.mesh = &shipMesh_;
    shipDraw.world = Mat4::FromTRS(body.position, body.orientation, 1.f);
    renderer.Draw(shipDraw);

    DrawItem terrainDraw;
    terrainDraw.mesh = &terrainMesh_;
    terrainDraw.world = Mat4::FromTRS(Vec3{ 0.f, 0.f, 0.f }, Quat::Identity(), 1.f);
    renderer.Draw(terrainDraw);

    DrawItem planetDraw;
    planetDraw.mesh = &planetMesh_;
    planetDraw.world = Mat4::FromTRS(atmosphere_.center, Quat::Identity(), 1.f);
    renderer.Draw(planetDraw);
}

} // namespace helix::game
