#include "HUDRenderer.h"
#include <cmath>
#include <algorithm>

namespace helix::hud {

using helix::renderer::LineVertex;
using helix::math::Vec3;

void HUDRenderer::AddLine(std::vector<LineVertex>& lines, float x0, float y0, float x1, float y1,
                           float r, float g, float b, float a) {
    lines.push_back({ x0, y0, r, g, b, a });
    lines.push_back({ x1, y1, r, g, b, a });
}

void HUDRenderer::AddBar(std::vector<LineVertex>& lines, float x, float yBottom, float yTop,
                          float fill01, float r, float g, float b) {
    fill01 = std::clamp(fill01, 0.f, 1.f);
    // Outline
    AddLine(lines, x, yBottom, x, yTop, 0.5f, 0.5f, 0.5f, 0.6f);
    // Fill
    float filledTop = yBottom + (yTop - yBottom) * fill01;
    AddLine(lines, x, yBottom, x, filledTop, r, g, b, 0.9f);
    // End caps
    AddLine(lines, x - 0.01f, yBottom, x + 0.01f, yBottom, 0.5f, 0.5f, 0.5f, 0.6f);
    AddLine(lines, x - 0.01f, yTop, x + 0.01f, yTop, 0.5f, 0.5f, 0.5f, 0.6f);
}

void HUDRenderer::Build(const helix::physics::RigidBody& body, const helix::game::ShipSystems& systems,
                         float aspectRatio, std::vector<LineVertex>& lines) {
    lines.clear();
    const float hudR = 0.15f, hudG = 1.0f, hudB = 0.4f; // classic HUD green

    // --- Reticle (center crosshair) ---
    AddLine(lines, -0.02f, 0.f, 0.02f, 0.f, hudR, hudG, hudB, 0.9f);
    AddLine(lines, 0.f, -0.03f, 0.f, 0.03f, hudR, hudG, hudB, 0.9f);
    for (int i = 0; i < 4; ++i) {
        float angle = i * 3.14159265f * 0.5f + 0.785f;
        float cx = std::cos(angle) * 0.05f, cy = std::sin(angle) * 0.05f * aspectRatio;
        float dx = std::cos(angle) * 0.07f, dy = std::sin(angle) * 0.07f * aspectRatio;
        AddLine(lines, cx, cy, dx, dy, hudR, hudG, hudB, 0.5f);
    }

    // --- Attitude ladder / horizon line, derived from ship pitch & roll ---
    Vec3 fwd = body.orientation.Forward();
    Vec3 up = body.orientation.Up();
    float pitch = std::asin(std::clamp(fwd.y, -1.f, 1.f));      // radians
    float roll = std::atan2(body.orientation.Right().y, up.y);  // approx bank angle

    auto rotatePoint = [&](float x, float y) {
        float cs = std::cos(-roll), sn = std::sin(-roll);
        return std::pair<float, float>{ x * cs - y * sn, x * sn + y * cs };
    };

    float pitchOffset = -pitch * 0.6f; // scale radians to screen-space offset
    for (int deg = -30; deg <= 30; deg += 10) {
        float lineY = pitchOffset + (deg / 90.f) * 0.6f;
        float halfWidth = (deg == 0) ? 0.18f : 0.09f;
        auto [lx0, ly0] = rotatePoint(-halfWidth, lineY);
        auto [lx1, ly1] = rotatePoint(halfWidth, lineY);
        float bright = (deg == 0) ? 1.0f : 0.6f;
        AddLine(lines, lx0, ly0, lx1, ly1, hudR * bright, hudG * bright, hudB * bright, 0.8f);
    }

    // --- Velocity vector (projected flight-path marker) ---
    Vec3 velDir = body.velocity.LengthSq() > 0.01f ? body.velocity.Normalized() : fwd;
    float velAngleH = std::atan2(Vec3::Dot(velDir, body.orientation.Right()), Vec3::Dot(velDir, fwd));
    float velAngleV = std::asin(std::clamp(Vec3::Dot(velDir, up), -1.f, 1.f));
    float vx = std::clamp(velAngleH * 0.5f, -0.3f, 0.3f);
    float vy = std::clamp(velAngleV * 0.5f, -0.3f, 0.3f);
    float vs = 0.025f;
    AddLine(lines, vx - vs, vy, vx - vs * 0.3f, vy, 1.f, 1.f, 0.2f, 0.9f);
    AddLine(lines, vx + vs * 0.3f, vy, vx + vs, vy, 1.f, 1.f, 0.2f, 0.9f);
    AddLine(lines, vx, vy - vs, vx, vy - vs * 0.3f, 1.f, 1.f, 0.2f, 0.9f);
    for (int i = 0; i < 16; ++i) {
        float a0 = (i / 16.f) * 6.2831853f, a1 = ((i + 1) / 16.f) * 6.2831853f;
        AddLine(lines, vx + std::cos(a0) * vs, vy + std::sin(a0) * vs,
                       vx + std::cos(a1) * vs, vy + std::sin(a1) * vs, 1.f, 1.f, 0.2f, 0.7f);
    }

    // --- Side bars: speed (left), altitude (right) ---
    float speed = body.velocity.Length();
    float speedFrac = std::clamp(speed / 400.f, 0.f, 1.f);
    AddBar(lines, -0.85f, -0.5f, 0.5f, speedFrac, hudR, hudG, hudB);

    float altitudeFrac = std::clamp(body.position.y / 6000.f, 0.f, 1.f);
    AddBar(lines, 0.85f, -0.5f, 0.5f, altitudeFrac, hudR, hudG, hudB);

    // --- Hull / shield status bars (bottom-left cluster) ---
    AddBar(lines, -0.7f, -0.85f, -0.65f, systems.hullIntegrity01, 1.f, 0.3f, 0.2f);
    AddBar(lines, -0.6f, -0.85f, -0.65f, systems.shieldFront01, 0.2f, 0.6f, 1.f);
    AddBar(lines, -0.5f, -0.85f, -0.65f, systems.shieldRear01, 0.2f, 0.4f, 0.8f);

    // --- Heat meter (bottom-right) ---
    AddBar(lines, 0.7f, -0.85f, -0.65f, systems.engineHeat01, 1.f, 0.5f, 0.1f);

    // --- Gear / assist status ticks (small indicator marks, bottom center) ---
    float gearR = systems.landingGearDown ? 0.2f : 0.8f;
    float gearG = systems.landingGearDown ? 1.f : 0.3f;
    AddLine(lines, -0.05f, -0.9f, 0.05f, -0.9f, gearR, gearG, 0.2f, 0.8f);

    float assistR = systems.flightAssistOn ? 0.2f : 0.8f;
    float assistG = systems.flightAssistOn ? 1.f : 0.3f;
    AddLine(lines, -0.05f, -0.94f, 0.05f, -0.94f, assistR, assistG, 0.2f, 0.8f);
}

} // namespace helix::hud
