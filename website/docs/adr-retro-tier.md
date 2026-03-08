# ADR: RETRO Tier Deprecation

**Status:** ACCEPTED
**Date:** 2026-03-08

---

## Context

The RETRO hardware tier (OpenGL 2.1, ~2005 era, 512 MB VRAM) was defined in the original engine specification as one of four hardware tiers. It has never been implemented. No engine code targets it, no shaders are written for it, and no tests validate it.

OpenGL 2.1 imposes severe constraints that are incompatible with FFE's existing rendering architecture:

- **No programmable pipeline beyond GLSL 120.** FFE's entire shader library (16 shaders) uses GLSL 330 core features: `layout` qualifiers, `in`/`out` varyings, `uniform` blocks, integer textures. Backporting to GLSL 120 would require a parallel shader codebase.
- **No framebuffer objects (core).** Shadows, post-processing (bloom, tone mapping, FXAA), SSAO, and the water reflection system all depend on FBOs.
- **No instanced rendering.** GPU instancing (1024 meshes/batch) requires `glDrawElementsInstanced` (OpenGL 3.1+). The vegetation system (256 grass instances/chunk) would be unusable at 60 fps.
- **No uniform buffer objects.** The engine passes lighting, shadow, and camera data via UBOs. RETRO would require per-uniform `glUniform*` calls.
- **No vertex array objects (core).** VAOs simplify state management throughout the RHI.
- **No sRGB framebuffer support.** The post-processing pipeline assumes linear-space rendering with gamma correction in the tone mapping pass.

The maintenance cost of supporting RETRO is high: every new rendering feature would need a RETRO-compatible path (no FBOs, no instancing, no UBOs, GLSL 120 shaders), doubling the surface area for bugs and testing.

Meanwhile, the LEGACY tier (OpenGL 3.3, ~2012 era, 1 GB VRAM) already covers hardware that is 14 years old. OpenGL 3.3 is supported by:

- Intel HD Graphics 2000+ (Sandy Bridge, 2011)
- NVIDIA GeForce 8000+ (2006, via driver updates)
- AMD Radeon HD 5000+ (2009)
- Mesa llvmpipe (software fallback on any Linux system)

Any machine too old for OpenGL 3.3 is realistically too old to run a game built on a modern ECS with PBR rendering, even at minimum settings.

## Decision

**Formally deprecate the RETRO tier (OpenGL 2.1) with no plans to implement.**

- RETRO is removed from the active tier table in documentation.
- The tier enum value (`RETRO = 0`) may remain in code, marked `[[deprecated]]`, to avoid breaking any downstream references.
- No new features will consider RETRO compatibility.
- No CI or test infrastructure will target OpenGL 2.1.
- If a future contributor wants to support pre-3.3 hardware, they should propose a new ADR with a concrete plan. The burden of proof is on the proposer.

LEGACY (OpenGL 3.3) remains the default and minimum supported tier.

## Consequences

**Positive:**
- Every renderer feature can assume GLSL 330, FBOs, VAOs, UBOs, and instancing as baseline capabilities. This eliminates conditional code paths.
- Shader library remains a single codebase (GLSL 330 core) rather than maintaining parallel 120/330 versions.
- Test matrix stays manageable: LEGACY (OpenGL 3.3), STANDARD (OpenGL 4.5 / Vulkan), MODERN (Vulkan).

**Negative:**
- Hardware from before ~2011 that cannot run OpenGL 3.3 drivers is excluded. In practice, this is a negligible user population for a game engine shipping in 2026.

**Neutral:**
- The three remaining tiers (LEGACY, STANDARD, MODERN) provide clear coverage from 2012-era integrated GPUs through current-generation discrete GPUs with ray tracing.
