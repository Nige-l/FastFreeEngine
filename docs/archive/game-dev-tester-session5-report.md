# game-dev-tester Session 5 Usage Report

**Tester:** game-dev-tester agent
**Session:** 5
**Date:** 2026-03-05
**Demo built:** `examples/interactive_demo/` — `ffe_interactive_demo`

---

## Demo Description

The interactive demo opens a 1280x720 window and renders:
- 1 player sprite (bright green, 48x48, render layer 5) starting at the screen center
- 8 static background sprites (slate blue at varying tints, 60x60, render layer 0) placed in a decorative pattern across the screen
- A Lua startup message logged via `ffe.log` on the first tick
- WASD movement for the player, clamped to the visible area
- ESC to quit

All textures are generated from raw RGBA8 pixel data using `rhi::createTexture` (4x4 solid-colour blocks). No PNG assets were available in the repository.

**To run (requires a display or Xvfb):**
```bash
./build/examples/interactive_demo/ffe_interactive_demo
```

---

## What Worked Well

### 1. Sprite and transform API is clean and minimal

Creating entities with `Transform` + `Sprite` and getting them on screen requires exactly three steps: `createEntity`, `addComponent<Transform>`, `addComponent<Sprite>`. The `renderPrepareSystem` picks them up automatically. For a first-time user, "add these two components and the renderer does the rest" is genuinely discoverable.

### 2. Layer-based draw ordering works immediately

Setting `sprite.layer = 5` on the player and `sprite.layer = 0` on backgrounds was enough to get correct draw ordering. No explicit sort calls, no z-value fiddling required. The range 0-15 is small but sufficient for this kind of 2D game.

### 3. Input query functions are idiomatic and simple

`isKeyHeld`, `isKeyPressed`, `isKeyReleased` map directly to the conceptual model (held = continuous movement, pressed = one-shot event, released = cleanup). No state machine boilerplate. The naming is the right level of explicit.

### 4. ScriptEngine init/doString lifecycle is clean

`init()` → `doString()` is a two-step lifecycle with no surprises. Errors are logged automatically; the caller only needs to decide what to do on false. The instruction budget preventing infinite-loop crashes is a meaningful safety property.

### 5. Raw texture creation is straightforward

`rhi::createTexture` with a `TextureDesc` and an inline pixel buffer was easy to use as a fallback. The `TextureDesc` field defaults are sensible; only `width`, `height`, and `pixelData` needed explicit setting in most cases.

### 6. The hello_sprites example was an excellent starting point

The pattern of a one-time initialization block guarded by a `bool` flag inside a system function is easy to follow. Having a working example to clone and modify significantly reduced time-to-first-build.

---

## Friction Points

### F-1: Key enum names not documented in .context.md

The `.context.md` for core describes the `Key` enum as containing "letters (A-Z)" but does not list the actual enumerator names. The task brief (written from the engine team's perspective) suggested `Key::KEY_W` style names. The actual names are `Key::W`, `Key::S`, `Key::A`, `Key::D`, `Key::ESCAPE`. This caused a compile error that required reading `engine/core/input.h` directly to resolve.

**Impact:** First compile attempt failed. Required header inspection to fix.

**Recommendation:** The `.context.md` should include a short table showing at minimum the letter key names and the most-used keys (`SPACE`, `ESCAPE`, `W`, `A`, `S`, `D`, `ARROW` keys). The current "letters (A-Z)" prose is insufficient for a developer writing code without IDE autocomplete.

### F-2: No PNG assets in the repository

There are no texture assets available. The `loadTexture` API documented in `.context.md` cannot be exercised at all without adding at least one PNG file to the repository. A developer following the documentation will write `loadTexture("player.png")`, see it fail at runtime, and have no obvious path forward unless they read the fallback pattern in `.context.md` closely.

**Impact:** The entire `texture_loader` API is untestable without supplementary assets. The `rhi::createTexture` fallback works but is not a documented "first path" -- it is buried in Pattern 2 of the renderer context.

**Recommendation:** Add at least one small reference PNG (a 32x32 checkerboard or palette swatch) to `assets/textures/` and adjust the hello_sprites or interactive demo to use it. This would validate the `loadTexture` → stb_image path end-to-end.

### F-3: `nameLength` field on SystemDescriptor is manual boilerplate

Every `SystemDescriptor` requires a hand-typed `nameLength = strlen("SystemName")`. This is error-prone -- if the name changes, the count silently becomes wrong, leading to truncated profiling labels or log output. The task brief itself notes this as "SystemDescriptor boilerplate (manual nameLength) -- friction point".

**Impact:** Low severity per instance, but it is the first thing a new developer has to type correctly. Getting it wrong silently corrupts profiling data, not a compile error.

**Recommendation:** Either compute `nameLength` from `name` in the `SystemDescriptor` constructor (if a constructor is added), or provide a helper macro/function like `FFE_SYSTEM_DESC("Name", fn, priority)` that fills the struct correctly.

### F-4: `Transform` is defined in `renderer/render_system.h`

To use `Transform` in a gameplay system, the include is `renderer/render_system.h`. This is conceptually wrong for a game developer -- `Transform` is a fundamental game object property, not a renderer concept. The first time a developer tries `#include "core/ecs.h"` and writes `world.addComponent<Transform>(...)`, they get a compile error because `Transform` is not in core. The `api-designer` review notes acknowledged this (API Review Note 2), but it still affects developers today.

**Impact:** Discoverability failure. A developer expects `Transform` to be in core. They have to learn it lives in a renderer header.

**Recommendation:** Move `Transform` to `engine/core/components.h` and forward-declare or re-export it from `renderer/render_system.h` for backward compatibility.

### F-5: `loadTexture` always uses LINEAR filter with no per-load override

From the renderer `.context.md`: "Note: `loadTexture` always uses `LINEAR` filter... If you need `NEAREST` filter for pixel art, use `rhi::createTexture()` directly." This means pixel art games must bypass the file-loading API entirely to get correct rendering. There is no `loadTexture(path, filter)` overload.

**Impact:** Pixel art games -- a primary FFE use case given the target hardware -- cannot use `loadTexture` and get correct results. They must manually decode via stb_image or use a different code path.

**Recommendation:** Add a `TextureFilter` parameter to `loadTexture` (defaulting to `LINEAR` for backward compatibility).

---

## Blockers

### DESIGN-1: `requestShutdown()` is not reachable from inside a system

`Application::requestShutdown()` exists and works, but it is a method on the `Application` object. Systems only receive `(World& world, float dt)`. There is no way to signal shutdown from within a system without storing an `Application*` externally.

**Workaround used:** Injected `Application*` into the ECS registry context via `world.registry().ctx().emplace<DemoContext>()`. The system retrieves it with `world.registry().ctx().find<DemoContext>()`. This works but it is an EnTT-specific escape hatch, not a documented pattern -- the game developer must know that the registry has a context store, which is not obvious from the `.context.md`.

**Impact:** Medium. Any game that needs ESC-to-quit from a system (which is virtually every game) needs this workaround. The issue was noted in the project memory as a known gap.

**Recommendation:** Add `World::requestShutdown()` as a forwarding method, or expose a `World::setShutdownFlag(bool*)` that points to `Application::m_running`. The simplest fix: add a `bool` to `World` that Application checks each tick.

---

## Missing Features

### M-1: No texture assets shipped with the engine

See F-2. The demos cannot exercise `loadTexture` without adding PNG files to the repository.

### M-2: No `Lua` ECS bindings yet

The `.context.md` notes that "additional bindings (entity creation, component access, input queries) will be added in later sessions." Currently, Lua scripts can only call `ffe.log()`. A game developer cannot write game logic in Lua -- only startup messages. The scripting system is present but not yet useful for the stated goal of "game behaviour belongs in Lua."

### M-3: No way to set the window clear colour from game code

`Application` has a private `m_clearColor` with no setter. Every game using FFE gets the same dark grey background. A game developer wanting a different sky colour has no documented path.

### M-4: No `Transform.velocity` or `Velocity` component in core

Every game that moves something needs a velocity. `hello_sprites` defines a local `Velocity` struct; the interactive demo could have used one too. This is a common enough component that shipping a minimal `Velocity` in core (or documenting a standard pattern) would reduce boilerplate.

### M-5: No `World::requestShutdown()` forwarding method

See DESIGN-1 above. This is missing enough to be listed as a missing feature, not just a design gap.

---

## API Discoverability Assessment

### What could be found from `.context.md` alone

- ECS entity/component lifecycle: excellent. The patterns are clear and correct.
- Sprite + Transform usage: good. Example 1 in the renderer context is copy-pasteable.
- Camera coordinate system: adequate. The defaults (centered, -640..640, -360..360) are visible in the `Camera` struct table, though no "this is what your screen coordinates look like" prose exists.
- Logging macros: excellent. Clear table, correct syntax.
- ScriptEngine: good. Init → doString lifecycle is well-documented.
- Input: mostly good, fails on exact key names (see F-1).
- Texture creation (raw RGBA): adequate, present in Pattern 2 of renderer context.

### What required reading engine headers directly

- `ffe::Key` enum value names (`W`, `S`, `A`, `D` rather than `KEY_W` etc.) — required reading `engine/core/input.h`
- Confirming `requestShutdown` is on `Application` not `World` — required reading `engine/core/application.h`
- Confirming `Transform` is in `renderer/render_system.h` — required reading after a compile error

### Overall discoverability rating: 7/10

For the documented patterns, the `.context.md` files are genuinely useful. The main gaps are the key name documentation failure (which caused a compile error) and the `requestShutdown` design gap (which required an undocumented workaround). A developer writing their first game with FFE would succeed, but would hit at least two moments of confusion that required header inspection to resolve.

---

## Files Created

- `/home/nigel/FastFreeEngine/examples/interactive_demo/main.cpp`
- `/home/nigel/FastFreeEngine/examples/interactive_demo/CMakeLists.txt`
- `/home/nigel/FastFreeEngine/examples/CMakeLists.txt` (added `add_subdirectory(interactive_demo)`)

## Build Status

Compiles clean on Clang-18 with `-Wall -Wextra -Wpedantic`, zero warnings, zero errors.
