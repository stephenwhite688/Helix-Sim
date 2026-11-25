#include "HudOverlay.h"
#include <cstdio>
#include <cmath>

namespace helix::ui {

namespace {
const float kHudGreen[4] = { 0.45f, 0.95f, 0.60f, 1.0f };
const float kHudCyan[4]  = { 0.55f, 0.85f, 1.00f, 1.0f };
const float kHudAmber[4] = { 1.00f, 0.75f, 0.30f, 1.0f };
const float kHudWhite[4] = { 0.92f, 0.95f, 1.00f, 1.0f };
}

float HudOverlay::HeadingDeg(const helix::physics::RigidBody& body) const {
    // Heading = atan2 of forward vector projected on XZ plane.
    helix::math::Vec3 fwd = body.orientation.Forward();
    float deg = std::atan2(fwd.x, fwd.z) * 57.2957795f;
    if (deg < 0.f) deg += 360.f;
    return deg;
}

void HudOverlay::Draw(UiContext& ui, const FontAtlas& font,
                      const helix::physics::RigidBody& body,
                      const helix::game::ShipSystems& systems,
                      const std::string& objectiveText) {
    char buf[96];
    const float W = ui.ScreenW(), H = ui.ScreenH();
    const float fh = 20.f; // readout font height

    // Bottom-left: speed / altitude / heading stack
    float x = 28.f, y = H - 120.f;
    float speed = body.velocity.Length();
    float altitude = body.position.y; // terrain-relative until LOD lands
    snprintf(buf, sizeof(buf), "SPD  %5.0f m/s", speed);
    ui.Text(font, x, y, fh, buf, kHudGreen); y += 24.f;
    snprintf(buf, sizeof(buf), "ALT  %6.0f m", altitude);
    ui.Text(font, x, y, fh, buf, kHudCyan); y += 24.f;
    snprintf(buf, sizeof(buf), "HDG  %03.0f deg", HeadingDeg(body));
    ui.Text(font, x, y, fh, buf, kHudWhite);

    // Bottom-right: ship status chips
    float rx = W - 240.f, ry = H - 96.f;
    if (!systems.landingGearDown) {
        ui.Text(font, rx, ry, fh, "GEAR UP", kHudAmber); ry += 24.f;
    } else {
        ui.Text(font, rx, ry, fh, "GEAR", kHudGreen); ry += 24.f;
    }
    ui.Text(font, rx, ry, fh, systems.flightAssistOn ? "ASSIST ON" : "ASSIST OFF",
            systems.flightAssistOn ? kHudGreen : kHudAmber); ry += 24.f;
    ui.Text(font, rx, ry, fh, systems.weaponsArmed ? "WPN ARMED" : "WPN SAFE",
            systems.weaponsArmed ? kHudAmber : kHudGreen);

    // Top-center: mission objective
    if (!objectiveText.empty()) {
        ui.Text(font, 0.f, 24.f, 22.f, objectiveText, kHudCyan, 1, W);
    }
}

} // namespace helix::ui
