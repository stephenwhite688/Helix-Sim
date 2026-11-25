#pragma once
#include <vector>
#include "FontAtlas.h"
#include "Ui.h"
#include "../Physics/Physics.h"
#include "../Game/Ship.h"

namespace helix::ui {

// Numeric HUD readouts drawn with the font atlas on top of the line HUD:
// speed, altitude, heading, throttle %, and objective line when a mission
// is active. Appends into the UiContext's font bucket during Draw().
class HudOverlay {
public:
    void Draw(UiContext& ui, const FontAtlas& font,
              const helix::physics::RigidBody& body,
              const helix::game::ShipSystems& systems,
              const std::string& objectiveText);

private:
    float HeadingDeg(const helix::physics::RigidBody& body) const;
};

} // namespace helix::ui
