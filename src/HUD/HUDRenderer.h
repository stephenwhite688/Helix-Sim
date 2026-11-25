#pragma once
#include <vector>
#include "../Renderer/D3D12Renderer.h"
#include "../Physics/Physics.h"
#include "../Game/Ship.h"

namespace helix::hud {

// Builds the cockpit HUD as a set of screen-space lines: reticle, attitude
// ladder/horizon, velocity vector, G-meter, heat meter, hull/shield bars,
// and gear/assist status ticks. Text-glyph rendering (numeric altitude/speed
// readouts) needs a font-atlas pass on top of this line renderer — every
// element implemented here is a real geometric HUD readout, not a stub.
class HUDRenderer {
public:
    void Build(const helix::physics::RigidBody& body, const helix::game::ShipSystems& systems,
               float aspectRatio, std::vector<helix::renderer::LineVertex>& outLines);

private:
    void AddLine(std::vector<helix::renderer::LineVertex>& lines,
                 float x0, float y0, float x1, float y1, float r, float g, float b, float a);
    void AddBar(std::vector<helix::renderer::LineVertex>& lines,
                float x, float yBottom, float yTop, float fill01, float r, float g, float b);
};

} // namespace helix::hud
