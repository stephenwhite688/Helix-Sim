# HelixSim — Game Specification

**Revision:** 2
**Working title:** Helix Spaceflight Simulator
**Engine:** None — from-scratch C++20, Win32, DirectX 12, XAudio2, XInput, Raw Input
**Status:** Foundation pass builds and runs (flyable tech demo, VS 2026 /
`.slnx`). This document is the contract for taking it to a complete,
playable game.

**Revision history**

- **Rev 1** — initial spec: vision, feature set, milestones M1–M4.
- **Rev 2** — added frontend/boot flow with pilot name entry (§3.6);
  reframed renderer target as Terragen-class real-time terrain realism
  with six-stage roadmap R1–R6 (§3.7); added story onboarding —
  Vanguard Space Command intro cinematic, on-foot arrival, briefing,
  boarding, and dual-mode flight-check training (§3.10); three-act
  campaign — test pilot → defend Earth → Earth-orbit/Moon ops (§3.4);
  hero experimental ship with under-wing engines + undercarriage
  (§3.2); milestones updated to carry the renderer stages and intro;
  recorded that the codebase now builds clean on VS 2026.

---

## 1. Vision

A seamless-flight spaceflight sim in the tradition of *Kerbal Space
Program* (meets *Flight Simulator* in the atmosphere) and *X*-series
cockpits: one continuous world from high orbit to the surface, no loading
screens, flown with mouse/HOTAS/gamepad/keyboard. Solo-first; the
architecture leaves room for multiplayer later.

**Design pillars**

1. **One world, no seams** — orbit to atmosphere to surface is one
   continuous simulation; the player never sees a load screen or a pop.
2. **Flyable on anything** — full keyboard/mouse fallback is a first-class
   control scheme, not an afterthought; HOTAS/gamepad layer on top.
3. **Systems that matter** — fuel, heat, hull, shields, subsystems,
   landing quality all have consequences. Failure is part of the game.
4. **Readable cockpit** — the HUD is a working instrument panel (MFDs,
   radar, nav), not a debug overlay.
5. **A place, not a prototype** — the world renders at real-time
   photographic-terrain fidelity (Terragen-class look; see §3.7).

---

## 2. Current state (what exists and is real)

| System | State |
|---|---|
| Windowing / input | Win32 + Raw Input (mouse, generic HID/HOTAS) + XInput gamepad → unified `Action` enum, deadzone/curve, key bindings + save/load profile |
| Physics | 6-DOF rigid body, inverse-square gravity, exponential atmosphere, drag + lift with stall, flight-assist damping, fixed 120 Hz step |
| ECS | Sparse-set pools, O(1) add/remove/iterate, two-component joins |
| Renderer | D3D12: lit opaque pipeline (ship, terrain patch, planet sphere), alpha line pipeline for HUD, runtime-compiled HLSL |
| Audio | XAudio2 procedural: engine hum (idle+boost layers), wind, re-entry roar, one-shot SFX |
| Ship systems | Engine modes, power priority, gear, assist, weapons arm/fire/cooldown/ammo, shields/hull/subsystem health, 3 fuel tanks, engine heat, landing + crash scoring |
| HUD | Line-rendered reticle, attitude ladder, velocity vector, speed/alt/hull/shield/heat bars, status ticks |

**Known placeholders (honest gaps, all marked in code)**

- Landing/crash check uses `y == 0` plane, not terrain heightfield
- Weapons fire + cooldown + audio but produce no projectiles or damage
- Single 8 km terrain patch + separate 6 km-radius planet sphere; no LOD
- No text/numeric HUD readouts (bars and ladders only)
- No menus, no mission/objective layer, no save of world state

---

## 3. Target feature set

### 3.1 Flight & world

- **Seamless planetary LOD** — chunked/CDLOD terrain from low orbit to
  surface; the "no seams" pillar. Planet radius scaled up to a believable
  size (target ≥ 500 km) with floating-origin or camera-relative rendering
  to keep float precision.
- **Atmosphere** — density model already exists; add wind, Mach effects
  on controls, contrails/particles at high G.
- **Space** — starfield skybox, sun lighting direction, Earthshine
  ambient, the **Vanguard space station** (mission hub: refuel/rearm),
  and the **Moon** as a second low-LOD body. Gravity wells are already
  supported by `AtmosphereField`; multi-body flight (Earth ↔ Moon
  transfers) is a physics extension, not a new system.
- **Biomes & terrain** — procedural heightfield generation with biome
  colouring (ocean/beach/forest/desert/mountain), scatter props (trees,
  rocks, buildings) at near LOD.
- **Weather/sky** — day/night cycle, volumetric cloud layer, height fog
  in atmosphere.

### 3.2 Ship & systems

- **Hero ship — the experimental test craft**: sleek black prototype with
  retractable undercarriage and **two under-wing engines** — one per
  wing — providing forward thrust and cockpit-operated ground
  maneuvering. Distinct silhouette; the player's first and signature
  aircraft. Later ship classes (§below) expand the stable.
- **Ship classes** — at least 3 total: the experimental prototype
  (atmospheric test craft), a space interceptor, and a heavy
  lift/cargo. Different thrust/mass/aero profiles.
- **Damage model** — subsystem health fields exist; wire them: damaged
  engine = thrust loss, damaged avionics = instrument failure, etc.
- **Fuel/energy** — existing 3-tank model; add fuel as a mission resource
  (range planning becomes gameplay).
- **Re-entry** — heat model exists; add plasma/glow shader, blackout
  period (avionics offline), and safe-corridor guidance.

### 3.3 Combat

- **Projectiles & hitscan** — laser bolts + cannon rounds as ECS entities;
  sphere/OBB collision vs target ships.
- **Damage application** — shields absorb first (front/rear already
  tracked), hull/subsystems take residual; visual + audio feedback.
- **Enemy AI** — patrol, intercept, evade, return-to-base states; at
  least one AI ship class.
- **Countermeasures** — chaff/flare decoys that break missile locks
  (ammo field exists).
- **Radar & lock-on** — MFD radar page: contacts, range rings, lock tone,
  lead-pursuit indicator.

### 3.4 Missions & gameplay — campaign structure

The career is a **three-act campaign**, not a mission list:

**Act I — Test pilot (atmospheric).** The player is a test pilot for the
experimental craft. First missions are training/handling exercises flown
in Earth-like atmosphere over dramatic terrain: valleys, mountains,
rivers, waterfalls, tunnels, and open sea. Teaches flight model,
handling limits, and the ship's quirks — the "feel" act.

**Act II — The emergency (defend Earth).** A crisis calls the pilot up:
Earth must be defended. Missions shift from handling trials to
atmosphere + space combat — patrols, intercepts, dogfights.

**Act III — Earth orbit & the Moon.** Operations extend off-world:
landing at the **space station at the original Vanguard base** for
refuel/rearm, and flights to the **Moon** for the same. Space combat
between Earth orbit and lunar operations — the full seamless-flight
pillar finally exercised end-to-end.

Underpinning systems:

- **Mission system** — data-driven missions across all three acts:
  handling trials, waypoint/cargo runs, precision landings, patrol,
  intercept/dogfight, recon, station/Moon turnaround ops.
- **Career/challenge structure** — mission list, scoring (kill/landing/
  penalty fields exist), medals, persistent record, act progression.
- **Career economy** — credits for completed missions funding ship
  upgrades and eventually additional airframes (optional, later
  milestone).

### 3.5 Cockpit & UI

- **Font-atlas text** — numeric readouts (speed, altitude, heading,
  Mach), labels, warnings. Extend the existing line renderer with a glyph
  pipeline (bitmap font atlas is sufficient).
- **MFD pages** — Nav (waypoint/map), Systems (fuel/hull/subsystems),
  Combat (radar/locks), Flight (engine/assist status).
- **Clickable cockpit** — MFD page buttons, gear/assist/scram switches as
  3D cockpit geometry or screen-space hotspots.
- **Menus** — main menu, pause, settings (graphics, controls, audio),
  mission briefing/debrief, key-binding editor (profile load/save exists).

### 3.6 Frontend / boot flow

A real game needs a front end before the sim ever spins up. Ordered flow:

1. **Splash** — HelixSim logo/title card (~2 s, skippable) using the
   supplied artwork (`src/Resources/Helix_Flight_sim.png` logo,
   `Helix Flight sim.jpg` key art), engine/version line. Rendered by the
   same D3D12 device so there's one graphics path. The PNG doubles as
   the window/taskbar icon.
2. **Player profile** — first run asks for a pilot callsign/name, stored
   in `%LOCALAPPDATA%\HelixSim\profile.cfg`. Reused for save slots,
   records, and mission debrief "pilot: X". Validation: 1–16 chars,
   alphanumeric + space/dash/underscore; "Pilot" default if skipped.
3. **Main menu** — Continue (if save exists), New Career, Missions,
   Settings, Exit. Keyboard + mouse navigable; controller support later.
4. **Mission select / briefing** — mission list with briefing text,
   recommended ship, success criteria; Launch button.
5. **Loading** — per-mission asset/world generation progress bar (terrain
   chunks, ship loadout). Kept short; seamless-flight worlds stream at
   runtime, loading screens only cover mission setup.
6. **Debrief** — post-mission stats (time, landings, kills, score,
   penalties) → back to mission select or main menu.

**UI implementation note:** menus/MFDs share one retained-mode UI layer
(screen-space quads + font atlas + hit-testing) built on top of the line
pipeline — do NOT build a second UI framework; the HUD and menus are the
same system at different depths.

### 3.7 Renderer realism roadmap — Terragen-class target

**Target:** Terragen-adjacent terrain realism *in real time*. The honest
benchmark is Microsoft Flight Simulator's terrain look, not Terragen's
offline output — same ingredient stack, real-time budget. There is no
middleware for this (NuGet is .NET-only; C++ libraries come via
vcpkg/Conan and none provide terrain realism). Unreal/Unity would abandon
the no-engine premise. Everything below is built into `D3D12Renderer`
itself — shaders, pipelines, and mesh streaming on top of what exists.

The Terragen look decomposes into six ingredients, each mapped to a DX12
implementation stage:

| Stage | Terragen ingredient | DX12 implementation | Notes |
|---|---|---|---|
| R1 | Surface response | **PBR materials** — metallic/roughness, normal maps, IBL from a generated environment map | Foundation; largest single material jump |
| R2 | Photographic grade | **HDR pipeline** — FP16 render target, ACES/AgX tonemap, bloom, **TAA** (near-mandatory at this fidelity) | Foundation; must land before content grows |
| R3 | Sky & light | **Physical sun/sky** — Preetham/Hosek atmospheric scattering, correct solar intensity, cascaded shadows, aerial-perspective fog | Core look |
| R4 | Terrain detail | **Procedural terrain gen** — tiled heightfield (fBm + domain warp + thermal erosion) feeding chunked LOD; **splat-map texturing** — 4–6 tiled PBR layers (grass/rock/sand/snow) blended by slope/height/moisture with detail normals | Core look; biggest single work item in the project |
| R5 | Atmosphere volume | **Volumetric clouds** — raymarched or layered-billboard cloud deck lit by sun position; height fog | The "photographic" ingredient — hardest shader, deliberately late |
| R6 | Water (optional) | Gerstner-wave water plane, planar/screen-space reflections, shoreline foam | Deferrable — dry-world planets first |

Ordering rationale: R1+R2 land first (retrofit cost grows with content);
R3+R4 deliver the visible "place"; R5 is what sells Terragen-realism to
most viewers and is deliberately late because it's the riskiest shader
work. Each stage is independently shippable — after R4 the world already
looks like a real place; R5 makes it cinematic.

Notes:
- Ship/terrain/planet meshes move to a shared PBR vertex format at R1;
  the opaque pipeline gets shader permutations, not new pipelines.
- IBL environment is generated at startup (procedural sky → cubemap →
  irradiance/specular convolutions) — no external HDR assets.
- "Realistic" target is real-time photographic terrain — film-grade path
  tracing remains out of scope.

### 3.10 Intro sequence & training (story onboarding)

The game opens with a narrative flow that establishes the fantasy before
free play. Both the cinematic and the training are **skippable/bypassable**
— experienced players go straight to the mission select.

**Setting:** the player is a **test pilot for the experimental craft**,
called up for a special mission and directed to report to **Vanguard
Space Command** at its proving ground. The campaign then unfolds in
three acts (§3.4): atmospheric test flying → emergency defence of Earth
→ Earth-orbit and lunar operations.

Ordered flow (after pilot name entry in §3.6):

1. **Cinematic — the call-up** (in-engine, not a video file): camera
   moves through a real 3D scene — comms call, cut to the proving
   ground establishing shot, title card. Skip/B bypass available.
2. **Arrival & on-foot traversal** — the pilot arrives at the grounds
   in a futuristic ground vehicle, disembarks, and walks to the briefing
   room. Lightweight first-person section: capsule controller, look,
   interact prompts at doors/console. Scoped as a guided corridor, not
   open-world walking.
3. **Briefing** — mission briefing rendered in-engine (briefing-room
   scene or UI-over-scene): the special mission is revealed, objectives
   and success criteria shown. Dismiss to continue.
4. **Boarding** — walk to the spacecraft, boarding prompt, transition
   into the cockpit (fade through hatch).
5. **Flight checks** — the tutorial, in the cockpit:
   - **Guided checklist mode** (first-timers): pre-flight → engine
     start → systems check → taxi → takeoff, step-by-step prompts.
   - **Free-flight mode** (returning/skippers): sandbox takeoff with
     contextual HUD prompts appearing as relevant systems come online.
   - Both teach the same controls; the checklist can be re-run from the
     menu at any time.
6. **First mission** — flight checks end with a short first mission
   (basic waypoint navigation + landing), then free career begins.

**Scope note:** the on-foot section is a real feature (character
controller, interactables, small interior/exterior scenes) — the
largest single addition in this section. If schedule slips, fallback is
compressing steps 2–3 into the cinematic + briefing UI (vehicle →
briefing room becomes a scene transition, no walking). Steps 1, 4, 5, 6
are unaffected by that fallback.

### 3.11 Audio

- Procedural engine/ambience exists; add: radar ping loops, damage
  alarms, landing gear/wind rush variation, optional music stems per
  context (menu/orbit/combat), voice-free comms bleeps. Keep
  asset-free generation as the default; support optional WAV/OGG
  drop-in folder later.

### 3.12 Polish & feel

- Explosion particles + debris, engine exhaust plume, damage
  smoke/fire, camera shake on hits, re-entry plasma glow, speed-based
  FOV kick, landing gear deployment animation, wing-rock on stall.

---

## 4. Milestones

Ordered by dependency and value. Each milestone must end **playable and
verified**, not stubbed.

### M1 — Playable loop (the "it's a game" milestone)

1. **Frontend boot flow** — splash → pilot name entry (first run) →
   main menu → mission select/briefing → loading → debrief. One
   retained UI layer shared with the HUD (font atlas + hit-testing).
2. Font-atlas text rendering + numeric HUD readouts
3. Mission system v1: waypoint + landing-challenge missions
4. Pause menu + settings shell (graphics/audio/controls)
5. Projectiles + hit detection + damage to a stationary/dummy target;
   AI patrol craft with intercept behaviour
6. Terrain-height landing/crash check (replace `y==0` placeholder)
7. Save/load: bindings (exists) + profile + mission progress + ship state
8. **Renderer R1+R2** — PBR materials/IBL + HDR/tonemap/bloom/TAA. Do
   these before content grows; retrofitting realism later costs more.

**Exit criteria:** launch → splash → enter name → menu → fly → complete
a mission → land → score → debrief, with menus and readable instruments.
No stubs.

### M2 — World depth

1. Chunked terrain LOD + scaled planet radius + floating origin
2. Starfield, day/night cycle
3. **Renderer R3** — physical sun/sky, atmospheric scattering, cascaded
   shadows, aerial-perspective fog
4. **Renderer R4** — procedural tiled terrain generation (fBm + domain
   warp + thermal erosion) + splat-map biomes with detail normals. This
   is the single biggest work item in the project — it *is* the
   Terragen-realism deliverable.
5. Additional ship classes + damage-model consequences
6. Radar/lock-on MFD + countermeasures + missile guidance
7. Re-entry effects (plasma, blackout)
8. **Flight-check training** — guided checklist mode + free-flight prompt
   mode (§3.10 step 5); the controls tutorial, skippable after first run

**Exit criteria:** fly from orbit to surface and back without a load
screen; terrain looks like a real place (biomes, detail, atmosphere);
dogfight a patrolling AI craft to kill or flee.

### M3 — Complete game

1. Mission set across the three acts (10–15 missions: handling trials,
   delivery/landing, patrol/intercept/dogfight, station & Moon
   turnaround ops)
2. Multi-body physics (Earth + Moon gravity wells) + floating origin —
   required for Act III
3. Space station hub (dock/refuel/rearm interactions) + Moon low-LOD
   terrain
2. Career scoring + medals + persistent stats
3. Optional career economy/ship upgrades
4. Full cockpit: clickable MFDs, switches, throttle quadrant (HOTAS)
5. Audio pass: contextual music, full SFX set
6. Settings completeness: graphics quality tiers, rebinding UI, profiles
7. **Renderer R5** — volumetric clouds + height fog (the cinematic
   finish; hardest shader, lands once terrain/lighting are stable)
8. **Renderer R6 (optional)** — water if any mission planet has it
9. **Intro sequence** — in-engine call-up cinematic, on-foot arrival at
   Vanguard Space Command, briefing, boarding (§3.10); all bypassable

**Exit criteria:** a new player can go from menu to campaign completion
with no dead ends.

### M4 — Post-launch (explicitly out of scope for "complete")

Multiplayer, modding, VR, additional planets, dynamic campaigns.

---

## 5. Technical architecture notes

- **Fixed timestep** stays at 120 Hz physics, decoupled from render —
  already correct; keep it.
- **ECS** is sufficient for current scope. Add archetype/query caching
  only if profiling demands it. Projectile/AI entities slot in as new
  components, no rearchitecture needed.
- **Renderer growth path:** add a second opaque pipeline variant for
  skinned/animated meshes only if needed; prefer vertex-shader
  animation for ships. Text needs a new pipeline (alpha + texture
  atlas). LOD needs a chunked vertex/index streaming design — the
  biggest single piece of renderer work in the project.
- **Floating origin:** when planet radius grows, switch to
  camera-relative coordinates for rendering and keep physics in
  double-precision or a periodic rebase; decide at M2 before terrain
  work, not after. Multi-body scale (Earth–Moon distances) makes this
  mandatory, not optional — float32 world coords cannot span it.
- **Multi-body physics:** `PhysicsSystem` currently supports a single
  active `AtmosphereField`; Earth + Moon (+ station as a non-atmospheric
  gravity well) requires iterating all active fields per body. Small,
  well-scoped change — do it when lunar missions enter scope (M3).
- **UI layer:** one retained-mode system (font atlas + quads +
  hit-testing) serves splash, menus, MFDs, and HUD — no immediate-mode
  or third-party UI.
- **Audio:** keep procedural-first; add a WAV loader behind a
  folder-drop interface for optional assets rather than baking assets
  into the binary.
- **No external middleware** remains a goal — stay Win32/DX12/XAudio2.
  Exceptions allowed only for clearly-scoped libs (e.g. a font atlas is
  hand-rolled — if stb_truetype is used, vendor it).

## 6. Risks & honest notes

- **Terragen-class terrain (R4) is the hardest item in the project** —
  bigger than everything else in M2 combined. If it slips, the fallback
  is believable-but-simpler terrain (R3 atmosphere only, vertex-coloured
  biomes, no splat detail) — still a complete game, less cinematic.
- **Seamless planetary LOD** is the second-biggest risk; the fallback is
  a "jump-to-orbit" model (atmospheric flight + separate orbital map)
  which is still a complete game, just not the flagship pillar.
- **Volumetric clouds (R5)** is the riskiest shader — raymarching cost
  vs. quality is a tuning problem, not a correctness one; billboard
  fallback exists.
- **AI quality** is cheap to start (state machines) and expensive to
  make feel good — scope M2 AI to believable patrol/intercept, not ace
  dogfighting.
- **Scope discipline:** the pillars above are the contract; anything not
  on this list (VR, multiplayer, modding) waits for M4+.
