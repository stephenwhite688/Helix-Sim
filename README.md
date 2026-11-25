# Helix Spaceflight Simulator

A from-scratch C++20 / DirectX 12 spaceflight sim core. No engine, no
middleware beyond the Windows SDK (DX12, XAudio2, XInput, Raw Input).
See [SPEC.md](SPEC.md) for the full game specification and milestone plan.

## Build (Windows 10/11, Visual Studio 2026)

```
mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64
cmake --build . --config Debug
```

Open `build/HelixSim.slnx` — the VS 2026 generator emits the `.slnx`
solution format (the successor to `.sln`, opened natively by VS 2026).

Run `HelixSim.exe` from `build/Debug` (or `Release`). The `shaders/` folder
is copied next to the executable automatically as a post-build step — the
renderer compiles `mesh.hlsl` and `line.hlsl` from disk at runtime via
`D3DCompileFromFile`, so that folder must sit alongside the .exe.

Requires the Windows 10/11 SDK (for `d3d12.h`, `xaudio2.h`, `xinput.h`) and a
DX12-capable GPU/driver. If `D3D12CreateDevice` fails on your machine you'll
get a message box saying so at launch rather than a silent crash.

## What's implemented in this pass (all real, none stubbed)

- Win32 windowing, Raw Input (mouse + generic HID for HOTAS) and XInput
  (gamepad) merged into one bindable `Action` enum, with keyboard fallback
  bound by default so it's flyable with no HOTAS attached
- Full 6-DOF Newtonian rigid-body physics: linear + angular integration,
  inverse-square gravity toward a planet, exponential-falloff atmospheric
  density, drag + lift with a stall model, flight-assist damping
- ECS core (sparse-set component pools, O(1) add/remove/iterate)
- DirectX 12 renderer: device/swapchain/fence setup, one lit opaque pipeline
  (ship + procedurally generated terrain heightfield + a low-poly planet
  sphere), one alpha-blended screen-space line pipeline for the HUD
- XAudio2 audio: procedurally generated engine hum (idle + boost layers,
  pitch/volume driven by throttle), wind, re-entry roar, and one-shot SFX
  (lasers, cannons, missiles, impacts, switches, gear) — no external audio
  assets required
- Ship systems: engine modes, power priority, landing gear, flight assist,
  weapon arm/fire/cooldown, shields/hull/subsystem health fields, fuel
  drain, engine heat, landing-quality scoring, crash detection
- Cockpit HUD: reticle, attitude ladder/horizon (pitch + bank), velocity
  vector, speed/altitude/hull/shield/heat bars, gear/assist status ticks

## What is *not* yet in this pass — next milestones, not silently missing

- **Text-glyph HUD readouts** (numeric speed/altitude/etc.) — the line
  renderer draws bars and ladders but not digits yet; needs a font-atlas
  pass on top of the existing line pipeline
- **MFD screens** (Nav/Systems/Combat/Diagnostics) and clickable cockpit
  geometry (levers, switches, MFD buttons) — not yet built
- **Combat resolution** (hit-scan/projectiles actually damaging a target,
  radar/lock-on/lead indicator) — weapons fire, cooldown, and play audio now;
  target entities and damage application are the next system to add
- **Seamless orbital→atmospheric LOD** — the current terrain is one fixed
  8km patch with a separate low-poly planet sphere; true continuous LOD
  from high orbit to the surface (the "no loading screens" requirement) is
  the next big renderer milestone
- **Re-entry visual effects** (heat glow/plasma shaders), volumetric
  clouds, PBR texturing, docking/carrier gameplay, networking — not started

Send me build errors from your VS2022 build and I'll fix them, same
workflow as HELIX and StarHaven. Tell me which of the above you want built
next and I'll add it as a fully working pass, not a stub.
