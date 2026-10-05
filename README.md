<div align="center">

**An external overlay for Roblox (`RobloxPlayerBeta.exe`) written in modern C++.**

Memory internals · ImGui/D3D11 overlay · Aimbot · Triggerbot · ESP · Chams · Particles

`Windows x64` · `C++20` · `Visual Studio 2022`

</div>


## Overview

`popstar` is an **external** cheat. It runs as its own process, attaches to the Roblox client,
reads/writes it through a layered memory backend, and draws its UI with Dear ImGui on a
Direct3D 11 overlay.

It resolves Roblox's `DataModel` (via `FakeDataModel` → real `DataModel`), then uses
**reflection** to bind hot instance methods (`FindFirstChild`, `GetChildren`, `WaitForChild`,
`FindFirstChildOfClass`, `GetDescendants`, `GetAttribute`, `Clone`) through a code cave inside the
target's own image — so no `SEC_IMAGE` is wiped and no remote thread is required for those calls.

---

## Features

### Combat
- **Aimbot** — FOV circle, target line, hitbox selection, multipoint, bezier curve
  smoothing, humanization, prediction, target switching with FOV/delay, reaction time
- **Triggerbot** — sticky mode, team check, visible-only, configurable radius/delay, on-screen
  hitbox feedback
- Per-hitbox enable, combat checks, and debug hitbox visualization

### Visuals / ESP
- Boxes (full / cornered / glow / fill / gradient, animated)
- Skeleton (with animated gradient), head dot, china hat
- Health bar (gradient / glow / static), name, flags (rig, team, visible, knocked, sit, tool,
  distance), bottom flags
- Snaplines with configurable origin, off-screen arrows (distance / health / name)
- Visible vs occluded color sets, max-distance filtering, teammate/dead exclusion

### Chams
- **Mesh chams** — mesh parser + cache, custom DX shader, stack-based renderer, selectable
  materials, visible/invisible/glow
- **Engine chams** — alternative engine-renderer path
- Two selectable modes (mesh or engine) from one toggle

### Particles & Tracers
- Death effect (particle burst, duration, spread, count)
- Ambient ash/ember/snow/rain/star systems with turbulence and wind
- Tracers with max-distance cutoff

### Sound ESP
- Footsteps, jumps, and footprints with distance cutoff and lifetime

### Local
- Walk speed, jump power, FOV, gravity
- Freecam
- Watermark, streamer-proof mode, overlay FPS, menu/ESP scale, account sync

---

## Architecture

```
 ┌──────────────────────────────────────────────┐
 │  project.exe  (popstar)                      │
 │                                              │
 │  utils/memory ── api · memory.asm · syscall   │
 │       │                                      │
 │       ├── cave/  image · module · xrw pool   │
 │       │           pe_stub · nt_remote        │
 │       │       └── gate/  engine_gate · tramp  │
 │       │                                      │
 │  core/sdk/rblx ── classes · reflect · offsets │
 │       │          engine/map_parser · physics  │
 │       │          script · types · value       │
 │       │                                      │
 │  core/sdk/cache ── game · lists · map · world │
 │       │                                      │
 │  core/framework/features ── combat · player   │
 │                              visuals · world  │
 │       │                                      │
 │  core/framework/gui ── backend · frontend     │
 │                       loop · manager · widgets│
 └───────────────┬──────────────────────────────┘
                 │ read / write / execute
                 ▼
   ┌───────────────────────────────┐
   │  RobloxPlayerBeta.exe         │
   └───────────────────────────────┘
```

### Startup (`project/entry.cxx`)

1. `g_console->initialize("nirvana")`
2. Attach to `RobloxPlayerBeta.exe`, resolve module base
3. Overlay + DirectX setup, then manager/fonts/textures/blur/acrylic/menu init
4. `g_syscall->initialize()` and `g_cave->initialize()`, then a **probe** placement to verify
   the cave is writable
5. `g_scheduler->start()`, `g_cache->start()`
6. Resolve the `DataModel` (retry loop) and `VisualEngine`
7. Bind + prove the hot reflection methods through `g_gate`
8. Start workspace cache, GPU renderers (chams, tracers, particles, combat visuals), and features
9. `g_loop->render()` — then ordered shutdown of every subsystem

---

## Repository layout

| Path | Purpose |
|------|---------|
| `project/entry.cxx` | Entry point / boot sequence |
| `project/includes.hxx` | Central include graph |
| `project/core/globals.hxx` | All feature flags and settings |
| `project/core/sdk/rblx/` | Roblox SDK — classes, reflection, offsets, physics, map parser |
| `project/core/sdk/cache/` | Game, list, map, and world caches |
| `project/core/framework/features/` | Combat, player, visuals (ESP, chams, tracers, particles) |
| `project/core/framework/gui/` | Backend (render, blur, water, inputs), frontend menu, widgets |
| `project/core/scheduler/` | Frame/task scheduler |
| `project/utils/memory/` | Memory API, MASM stubs, syscall wrapper |
| `project/utils/memory/cave/` | Code caves (image, module, XRW pool), PE stub, gate + trampoline |
| `project/utils/output/` | Console + logger |
| `project/assets/` | Logo, menu icons, fonts |
| `project/deps/` | Vendored ImGui, FreeType, zstd, DirectX SDK (June 2010) |
| `tools/` | Icon index generator, diagnostics, Roblox API dumps + reflection metadata |
| `roblox-sdk.slnx` | Solution |

---

## Requirements

- **Windows 10/11 x64**
- **Visual Studio 2022** — most configurations use the **v145** toolset (some use `v143`)
- **MASM** build customization (`project/utils/memory/memory.asm`)
- C++20 (`LanguageStandard=stdcpp20`)
- A working environment for `project/deps/dxsdk` (DirectX SDK June 2010, `d3dx11` etc.)

Third-party code is vendored — no vcpkg or NuGet required.

---

## Building

1. Open `roblox-sdk.slnx` (or `roblox-sdk.vcxproj`) in Visual Studio 2022.
2. Select your configuration — **Release | x64**.
3. Build.

Output goes to `output\project.exe` (`OutDir=$(SolutionDir)\output\`, `IntDir=$(SolutionDir)\output\intermediate\`).

From the command line:

```bat
msbuild roblox-sdk.slnx /p:Configuration=Release /p:Platform=x64
```

`compile_commands.json` and `.clangd` are included, so clangd-based tooling works if you
regenerate them after changing the build configuration.

---

## Usage

1. Launch `project.exe`.
2. Start Roblox. It attaches to `RobloxPlayerBeta.exe`, resolves the `DataModel`, and binds the
   hot methods.
3. The console logs `pid`, `handle`, `base`, `datamodel`, and `visual_engine` — if `datamodel=0`
   persists, the offsets need updating.
4. Use the in-game menu to toggle features.

---

## Tools

| Script / data | Purpose |
|---------------|---------|
| `tools/gen_icon_index.py` | Builds an icon index from Roblox reflection metadata |
| `tools/diag_icons.py` | Diagnostics for icon resolution |
| `tools/ReflectionMetadata.xml` | Roblox reflection metadata dump |
| `tools/Full-API-Dump.json` / `tools/Mini-API-Dump.json` | API dumps for class/offset work |

---

## Notes on offsets

Offsets live in `project/core/sdk/rblx/offsets/` and are Roblox-client specific. They change
frequently with client updates — regenerate them from the API dumps in `tools/` when the client
updates, or `datamodel` resolution will fail at boot.

---

## Credits

- [Dear ImGui](https://github.com/ocornut/imgui) — UI
- [FreeType](https://freetype.org) — font rasterization
- [zstd](https://facebook.github.io/zstd/) — compression
- [MaximumADHD/Roblox-Client-Tracker](https://github.com/MaximumADHD/Roblox-Client-Tracker) —
  reflection metadata source
- Fonts in `project/assets/fonts/` — Inter, Space Grotesk, SST, Font Awesome

---

## License

No license is provided. All rights reserved by the respective authors. This repository is shared
for educational purposes; you may not use it commercially or in violation of any game's Terms of
Service. Third-party components remain under their own licenses.
