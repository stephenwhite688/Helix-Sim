#pragma once
#include "../ECS/ECS.h"
#include "../Physics/Physics.h"
#include "../Renderer/D3D12Renderer.h"
#include "Ship.h"

namespace helix::game {

// Owns the ECS world, the active planet/atmosphere, the player ship, and the
// renderable meshes for terrain/ship — i.e. the "World" module from the spec.
class World {
public:
    void Initialize(helix::renderer::D3D12Renderer& renderer);

    void Update(helix::input::InputManager& input, helix::audio::AudioEngine& audio, float dt);
    void Render(helix::renderer::D3D12Renderer& renderer);

    helix::ecs::World& Ecs() { return ecs_; }
    helix::ecs::EntityId PlayerShip() const { return playerShip_; }
    // Reset the player ship to the mission-start state (position,
    // orientation, velocities) — used between missions.
    void RespawnPlayer();
    // Not const-qualified: the underlying ECS pool lookup mutates internal
    // caches, matching ECS::GetComponent's own (non-const) signature.
    ShipSystems& PlayerSystems() { return ecs_.GetComponent<ShipSystems>(playerShip_); }
    helix::physics::RigidBody& PlayerBody() { return ecs_.GetComponent<helix::physics::RigidBody>(playerShip_); }

private:
    helix::ecs::World ecs_;
    helix::physics::PhysicsSystem physics_;
    ShipControlSystem shipControl_;

    helix::ecs::EntityId playerShip_ = helix::ecs::kInvalidEntity;

    helix::renderer::MeshHandle shipMesh_;
    helix::renderer::MeshHandle terrainMesh_;
    helix::renderer::MeshHandle planetMesh_; // distant low-poly sphere for orbital view

    helix::physics::AtmosphereField atmosphere_;
};

} // namespace helix::game
