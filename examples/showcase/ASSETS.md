# Echoes of the Ancients — Asset Acquisition Plan

This document lists every asset needed for the Phase 6 showcase game, where to find it, and the rules governing acquisition. **No asset may be integrated until its license has been verified against the checklist in Section 6.**

---

## 1. 3D Models

All models must be in `.glb` (binary glTF) format. If a source provides `.gltf` + `.bin`, convert to `.glb` before committing.

### 1.1 Player Character

| Asset | Description | Candidates |
|-------|-------------|------------|
| Player model | Low-poly humanoid or stylized knight, ~500-2000 triangles | Quaternius "Ultimate Animated Characters" (CC0), Kenney "Animated Characters" (CC0) |
| Player animations (optional) | Idle, walk, attack — only if skeletal animation is used (stretch goal) | Same packs — both include rigged/animated variants |

If no suitable animated model is found, a static capsule or simple knight mesh with color-flash feedback is acceptable. The ADR explicitly states complex animation blending is not required.

### 1.2 Guardian Enemy

| Asset | Description | Candidates |
|-------|-------------|------------|
| Guardian model | Stone golem, skeleton warrior, or stylized creature, visually distinct from player | Quaternius "Free Animated Creatures" (CC0), Quaternius "Ultimate Monsters" (CC0) |
| Boss guardian variant | Same mesh scaled 1.5x with different material color, OR a second distinct mesh | Recolor/rescale of the base guardian model |

### 1.3 Environment — Dungeon / Castle Pieces

These are the building blocks for all three levels. Reuse aggressively with different scales, rotations, and materials.

| Asset | Description | Candidates |
|-------|-------------|------------|
| Wall section | Straight stone wall, modular (tileable end-to-end) | Kenney "Castle Kit" (CC0), Quaternius "Ultimate Modular Dungeon" (CC0) |
| Floor tile | Flat stone slab, 1x1 or 2x2 unit | Kenney "Castle Kit" (CC0), or scaled `cube.glb` |
| Pillar / column | Vertical stone pillar for temple interiors | Kenney "Castle Kit" (CC0), Quaternius dungeon pack |
| Archway / gate | Stone arch that can serve as a doorway or level transition | Kenney "Castle Kit" (CC0) |
| Door | Simple hinged or sliding stone door for puzzle gates | Kenney "Castle Kit" (CC0) |
| Stairs / ramp | Stepped or smooth incline | Kenney "Castle Kit" (CC0) |
| Bridge / platform | Flat slab for walkways and floating platforms | Scaled `cube.glb` (already in engine) or Kenney kit piece |
| Fountain basin | Central courtyard feature (Level 1) | Kenney "Medieval Town" (CC0) or stacked cylinder primitives |

**Target: 8-12 unique environment meshes total.** Variety comes from material/scale/rotation, not unique geometry.

### 1.4 Interactive Objects

| Asset | Description | Candidates |
|-------|-------------|------------|
| Crystal | Gem or crystal cluster for puzzle pedestals and light sources | Quaternius "Ultimate Gems and Minerals" (CC0), or simple procedural shape |
| Chest | Treasure chest for optional pickups or visual decoration | Kenney "Furniture Kit" (CC0), Quaternius "Treasure" (CC0) |
| Pressure plate | Flat disc or square plate that depresses on contact | Scaled `cube.glb` with distinct material color |
| Pushable block | Stone cube the player pushes onto pressure plates | `cube.glb` with stone texture |
| Artifact (collectible) | Glowing geometric shape — dodecahedron, orb, or stylized relic | Quaternius gems pack or Kenney item models (CC0) |
| Portal | Ring or arch with particle effect overlay for level transitions | Torus mesh from Kenney or simple arch + particle emitters |

### 1.5 Decoration

| Asset | Description | Candidates |
|-------|-------------|------------|
| Torch / wall sconce | Cylinder + cone bracket mounted on walls; flame is a particle emitter, not a mesh | Kenney "Furniture Kit" (CC0), Quaternius dungeon props |
| Barrel | Wooden barrel for visual clutter | Kenney "Furniture Kit" (CC0) |
| Crate | Wooden crate, stackable | Kenney "Furniture Kit" (CC0) |
| Vegetation (ivy, grass tufts) | Small low-poly plants for outdoor courtyard | Kenney "Nature Kit" (CC0), Quaternius "Ultimate Nature" (CC0) |
| Rubble / debris | Broken stone chunks for ruin atmosphere | Quaternius dungeon props or scattered scaled cubes |

---

## 2. Textures

All textures in `.png` format. Provide diffuse (albedo) at minimum. Normal maps where available. Specular maps optional (engine supports per-material specular value as fallback).

### 2.1 Surface Materials

| Texture | Resolution | Channels | Candidates |
|---------|------------|----------|------------|
| Stone wall (diffuse + normal) | 1024x1024 | RGB + RGB | ambientCG "Rock" or "Castle Wall" (CC0), Kenney "Prototype Textures" (CC0) |
| Stone floor (diffuse + normal) | 1024x1024 | RGB + RGB | ambientCG "Pavement" or "Stone Tiles" (CC0) |
| Brick (diffuse + normal) | 1024x1024 | RGB + RGB | ambientCG "Brick" variants (CC0) |
| Grass / ground (diffuse) | 1024x1024 | RGB | ambientCG "Grass" (CC0) |
| Wood (diffuse) | 512x512 | RGB | ambientCG "Wood" (CC0) — for barrels, crates, bridges |
| Lava (diffuse) | 512x512 | RGB | CC0 lava texture or solid orange-red with emissive feel |

### 2.2 Skyboxes (6-face cubemaps)

| Skybox | Resolution per face | Candidates |
|--------|-------------------|------------|
| Daytime clouds (Level 1 + Level 3) | 1024x1024 | Kenney "Skybox Pack" (CC0) — "Cloudy Lightrays" or similar |
| Sunset / dawn (Level 3 alternate) | 1024x1024 | Kenney "Skybox Pack" (CC0) — warm palette |
| Dark void / starfield (Level 2) | 512x512 | Solid dark color (procedural) or CC0 starfield from OpenGameArt |

### 2.3 UI / HUD

| Texture | Resolution | Candidates |
|---------|------------|------------|
| Health bar frame | 256x64 | Kenney "UI Pack" (CC0) or hand-drawn with `drawRect` (no texture needed) |
| Artifact icon | 64x64 | Kenney "Game Icons" (CC0) |
| Button backgrounds | 256x64 | Kenney "UI Pack" (CC0) |

**Note:** The HUD can be rendered entirely with `drawRect` and `drawFontText` — no textures strictly required. UI textures are a polish item.

### 2.4 Texture Budget

| Metric | Budget |
|--------|--------|
| Max resolution per texture | 2048x2048 |
| Typical resolution | 512x512 or 1024x1024 |
| Estimated total VRAM for textures | <100 MB |
| LEGACY tier VRAM minimum | 1 GB |

---

## 3. Audio

SFX in `.wav` format (uncompressed, for low-latency playback). Music in `.ogg` format (compressed, for streaming).

### 3.1 Music (3 tracks, loopable)

| Track | Mood | Duration | Candidates |
|-------|------|----------|------------|
| Level 1 — Courtyard | Warm, mysterious, exploratory | 2-4 min loop | OpenGameArt CC0 fantasy/ambient tracks, Kevin MacLeod "Perspectives" or "Shining Horizon" (CC-BY 3.0) |
| Level 2 — Temple | Dark, tense, oppressive | 2-4 min loop | OpenGameArt CC0 dungeon tracks, Kevin MacLeod "Dark Hallway" or "Darkest Child" (CC-BY 3.0) |
| Level 3 — Summit | Triumphant, soaring, epic | 2-4 min loop | OpenGameArt CC0 epic/adventure tracks, Kevin MacLeod "Ascending" (CC-BY 3.0) |

**Note:** Kevin MacLeod tracks require CC-BY 3.0 attribution. If used, add credit to the showcase README and in-game credits. Prefer CC0 if equally suitable tracks are available.

### 3.2 Sound Effects

| Sound | Format | Variants | Candidates |
|-------|--------|----------|------------|
| Footstep (stone) | .wav | 3 variants | Kenney "Impact Sounds" (CC0), freesound.org CC0 footstep packs |
| Footstep (grass) | .wav | 2 variants | freesound.org CC0 grass footstep recordings |
| Melee attack (whoosh) | .wav | 2 variants | Kenney "Impact Sounds" (CC0), freesound.org CC0 |
| Melee hit (impact) | .wav | 2 variants | Kenney "Impact Sounds" (CC0) |
| Guardian hit (stone crack) | .wav | 1 | freesound.org CC0 stone/rock impact |
| Guardian death (crumble) | .wav | 1 | freesound.org CC0 stone crumble/shatter |
| Artifact collect (chime) | .wav | 1 | Kenney "Digital Audio" (CC0), freesound.org CC0 pickup jingle |
| Gate / door open (grinding) | .wav | 1 | freesound.org CC0 stone grinding/sliding |
| Platform crumble | .wav | 1 | freesound.org CC0 stone crack/break |
| Jump | .wav | 1 | Kenney "Impact Sounds" (CC0) |
| Victory fanfare | .wav | 1 | OpenGameArt CC0, freesound.org CC0 |
| Menu select | .wav | 1 | Kenney "Interface Sounds" (CC0) |

### 3.3 Ambient Loops (spatial audio sources)

| Sound | Format | Candidates |
|-------|--------|------------|
| Wind (outdoor) | .ogg | freesound.org CC0 wind loop |
| Water / fountain | .ogg | freesound.org CC0 fountain/stream loop |
| Fire crackle (torches) | .ogg | freesound.org CC0 campfire/torch loop |
| Dripping water (temple) | .ogg | freesound.org CC0 cave drip loop |
| Low rumble (temple) | .ogg | freesound.org CC0 subterranean rumble |

---

## 4. Fonts

| Font | License | Source | Use |
|------|---------|--------|-----|
| Kenney "Kenney Fonts" (any variant) | CC0 | kenney.nl/assets/kenney-fonts | HUD text, menus, interaction prompts |
| Backup: Google Fonts "Press Start 2P" | SIL OFL | fonts.google.com | Retro alternative if Kenney font does not suit the style |

Load via `ffe.loadFont`. One font is sufficient for the entire showcase.

---

## 5. File Naming Convention and Directory Structure

```
examples/showcase/
  game.lua                    -- Main entry point: menu, level sequencing, save/load
  lib/
    player.lua                -- Player controller, combat, health
    camera.lua                -- Orbit camera with collision avoidance
    combat.lua                -- Melee attack, damage, hit feedback
    hud.lua                   -- Health bar, artifact count, prompts
    ai.lua                    -- Guardian patrol, chase, attack AI
    puzzle.lua                -- Shared puzzle mechanics (pressure plates, blocks)
  levels/
    level1.lua                -- Courtyard game logic
    level1.json               -- Courtyard scene data
    level2.lua                -- Temple game logic
    level2.json               -- Temple scene data
    level3.lua                -- Summit game logic
    level3.json               -- Summit scene data
  assets/
    models/
      player.glb
      guardian.glb
      crystal.glb
      torch.glb
      artifact.glb
      fountain.glb
      ... (environment kit pieces)
    textures/
      stone_wall_diffuse.png
      stone_wall_normal.png
      stone_floor_diffuse.png
      stone_floor_normal.png
      grass_diffuse.png
      wood_diffuse.png
      lava_diffuse.png
      skybox_day/               -- 6 faces: px.png, nx.png, py.png, ny.png, pz.png, nz.png
      skybox_sunset/
      skybox_dark/
      ui/
        artifact_icon.png
    audio/
      music/
        courtyard_theme.ogg
        temple_theme.ogg
        summit_theme.ogg
      sfx/
        footstep_stone_01.wav
        footstep_stone_02.wav
        footstep_stone_03.wav
        footstep_grass_01.wav
        footstep_grass_02.wav
        melee_whoosh_01.wav
        melee_whoosh_02.wav
        melee_hit_01.wav
        melee_hit_02.wav
        guardian_hit.wav
        guardian_death.wav
        artifact_collect.wav
        gate_open.wav
        platform_crumble.wav
        jump.wav
        victory_fanfare.wav
        menu_select.wav
      ambient/
        wind_loop.ogg
        fountain_loop.ogg
        fire_crackle_loop.ogg
        drip_loop.ogg
        rumble_loop.ogg
    fonts/
      hud_font.ttf
```

### Naming Rules

- All lowercase, `snake_case`
- Variants use `_01`, `_02` suffixes
- Diffuse textures: `*_diffuse.png`
- Normal maps: `*_normal.png`
- Specular maps: `*_specular.png` (if provided)
- Skybox faces follow engine convention: `px.png`, `nx.png`, `py.png`, `ny.png`, `pz.png`, `nz.png`

---

## 6. License Verification Checklist

Every asset committed to the repository MUST pass this checklist. No exceptions.

| # | Check | Pass? |
|---|-------|-------|
| 1 | Asset source URL recorded in this document or a `CREDITS.txt` in `examples/showcase/` | [ ] |
| 2 | License is CC0, MIT, Public Domain, or SIL OFL (fonts only) | [ ] |
| 3 | If CC-BY: attribution text added to `examples/showcase/CREDITS.txt` and in-game credits | [ ] |
| 4 | License file or text downloaded alongside the asset and stored in `examples/showcase/assets/LICENSES/` | [ ] |
| 5 | No "NC" (NonCommercial) clause — FFE is MIT and may be used commercially | [ ] |
| 6 | No "ND" (NoDerivatives) clause — we may modify assets (rescale, recolor, convert format) | [ ] |
| 7 | No "SA" (ShareAlike) clause — viral copyleft is incompatible with MIT engine license | [ ] |
| 8 | Asset is not ripped from a commercial game, extracted from a proprietary tool, or of unclear provenance | [ ] |

**Preferred license priority:** CC0 > MIT > Public Domain > SIL OFL (fonts) > CC-BY 3.0/4.0 (requires attribution).

**Rejected licenses:** CC-BY-NC, CC-BY-ND, CC-BY-SA, GPL, LGPL, proprietary, "free for personal use."

---

## 7. Asset Budget Per Level (from ADR Section 9)

| Metric | Budget | Notes |
|--------|--------|-------|
| Mesh entities | 50-80 | Each is a draw call; stay under 80 for LEGACY 60fps |
| Point lights | 4 max | Engine hard limit |
| Particle emitters | 16 max active | 128 particles each = 2048 max particles |
| Physics bodies | 40 max | Jolt handles this easily but keep bounded |
| Shadow map | 1024x1024 | Already proven in 3D demo |
| Total texture VRAM | <100 MB | Conservative for 1 GB LEGACY minimum |
| Lua update() cost | <2 ms/frame | 1M instruction budget already enforced |

### Per-Level Guidance

- **Level 1 (Courtyard):** ~60 mesh entities, 4 point lights (torches), 6-8 emitters (fire + dust), ~15 physics bodies (blocks, plates, guardians, player)
- **Level 2 (Temple):** ~50 mesh entities (fewer — darkness hides sparse geometry), 4 point lights (crystals + lava), 10-12 emitters (lava + crystals + sparkle), ~25 physics bodies (bridges, blocks, reflectors, guardians, player)
- **Level 3 (Summit):** ~70 mesh entities (floating platforms), 2-3 point lights, 8-10 emitters (wind + cloud wisps), ~30 physics bodies (platforms, guardians, player)

---

## 8. Recommended Sources — Quick Reference

| Source | URL | License | Best For |
|--------|-----|---------|----------|
| Kenney.nl | https://kenney.nl/assets | CC0 | Castle Kit, Furniture Kit, Nature Kit, UI Pack, Impact Sounds, Interface Sounds, Skybox Pack, Prototype Textures, Kenney Fonts |
| Quaternius | https://quaternius.com | CC0 | Animated characters, creatures, modular dungeons, gems/minerals, nature packs |
| ambientCG | https://ambientcg.com | CC0 | PBR textures (stone, brick, wood, grass, metal) — diffuse + normal + specular |
| OpenGameArt.org | https://opengameart.org | CC0 / CC-BY (verify per asset) | Music tracks, sound effects, misc 3D models |
| freesound.org | https://freesound.org | CC0 (filter by license) | Ambient loops, footsteps, impacts, environmental sounds |
| Kevin MacLeod | https://incompetech.com | CC-BY 3.0 | Music tracks (requires attribution) |
| Google Fonts | https://fonts.google.com | SIL OFL | Backup HUD font |

### Specific Packs to Evaluate First

1. **Kenney "Castle Kit"** — walls, floors, towers, gates, stairs. This is the primary environment source.
2. **Kenney "Furniture Kit"** — torches, barrels, crates, tables, chairs.
3. **Kenney "Prototype Textures"** — colored grid textures useful for prototyping before final art pass.
4. **Quaternius "Ultimate Modular Dungeon"** — pillars, arches, floor tiles. Complements Kenney Castle Kit.
5. **Quaternius "Ultimate Animated Characters"** — low-poly humanoids with walk/idle/attack animations.
6. **Quaternius "Free Animated Creatures"** — golem or skeleton for guardian enemies.
7. **ambientCG stone/brick textures** — `Rock006`, `Bricks052`, `PavingStones074` — diffuse + normal at 1K.
8. **Kenney "Skybox Pack"** — 6-face cubemaps ready to use.
9. **Kenney "Impact Sounds"** — footsteps, hits, collisions.
10. **Kenney "Interface Sounds"** — menu clicks, confirms.

---

## 9. Integration Notes

- **Model conversion:** If a pack provides `.fbx` or `.obj`, convert to `.glb` using `gltf-transform` or Blender CLI export. Only `.glb` files are committed.
- **Texture sizing:** Downscale any texture larger than 2048x2048 to 1024x1024. Most textures should be 512x512 or 1024x1024.
- **Audio normalization:** Normalize all SFX to -3 dB peak. Music to -6 dB peak. Prevents volume spikes.
- **Skybox orientation:** Verify face naming matches engine convention (`px`/`nx`/`py`/`ny`/`pz`/`nz`) before committing. Mislabeled faces cause visible seams.
- **Do NOT commit asset pack archives.** Download, extract what is needed, convert to the correct format, rename per Section 5 conventions, and commit only the final files.
- **Git LFS:** Consider enabling Git LFS for `examples/showcase/assets/` if total binary size exceeds 50 MB. Audio and textures accumulate fast.
