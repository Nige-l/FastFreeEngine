# ADR: RETRO Tier Deprecation

**Status:** ACCEPTED
**Author:** architect
**Date:** 2026-03-08

---

## Context

The RETRO hardware tier (OpenGL 2.1, ~2005 era, 512 MB VRAM) was defined in the original engine constitution as one of four hardware tiers. It has never been implemented. No engine code targets it, no shaders are written for it, and no tests validate it.

OpenGL 2.1 imposes severe constraints that are incompatible with FFE's existing rendering architecture:

- **No programmable pipeline beyond GLSL 120.** FFE's entire shader library (16 shaders) uses GLSL 330 core features: `layout` qualifiers, `in`/`out` varyings, `uniform` blocks, integer textures. Backporting to GLSL 120 would require a parallel shader codebase with attribute-based varyings, `gl_FragColor` output, and `texture2D()` calls.
- **No framebuffer objects (core).** Shadows, post-processing (bloom, tone mapping, FXAA), SSAO, and the water reflection system all depend on FBOs. The `EXT_framebuffer_object` extension exists but is not guaranteed and has driver-specific quirks on 2005-era hardware.
- **No instanced rendering.** GPU instancing (1024 meshes/batch) requires `glDrawElementsInstanced` (OpenGL 3.1+). RETRO tier would fall back to individual draw calls, making the vegetation system (256 grass instances/chunk) and any scene with repeated geometry unusable at 60 fps.
- **No uniform buffer objects.** The engine passes lighting, shadow, and camera data via UBOs. RETRO would require per-uniform `glUniform*` calls, adding CPU overhead and requiring a separate uniform upload path.
- **No vertex array objects (core).** VAOs simplify state management throughout the RHI. Without them, every draw call needs explicit attribute pointer setup.
- **No sRGB framebuffer support.** The post-processing pipeline assumes linear-space rendering with gamma correction in the tone mapping pass.

The maintenance cost of supporting RETRO is not just the initial backport. Every new rendering feature would need a RETRO-compatible path: no FBOs, no instancing, no UBOs, GLSL 120 shaders. This doubles the surface area for bugs and testing in the renderer, which is already the largest subsystem (25+ source files, 16 shaders, 500+ tests).

Meanwhile, the LEGACY tier (OpenGL 3.3, ~2012 era, 1 GB VRAM) already covers hardware that is 14 years old. OpenGL 3.3 is supported by:

- Intel HD Graphics 2000+ (Sandy Bridge, 2011)
- NVIDIA GeForce 8000+ (2006, via driver updates)
- AMD Radeon HD 5000+ (2009)
- Mesa llvmpipe (software fallback on any Linux system)

Any machine too old for OpenGL 3.3 is realistically too old to run a game built on a modern ECS with PBR rendering, even at minimum settings. The mission of FFE is to make game development accessible on older hardware -- not on museum hardware.

## Decision

**Formally deprecate the RETRO tier (OpenGL 2.1) with no plans to implement.**

- Remove RETRO from the active tier table in all documentation.
- The tier enum value may remain in code as `RETRO = 0` if it exists, marked `[[deprecated]]`, to avoid breaking any hypothetical downstream references.
- No new features will consider RETRO compatibility.
- No CI or test infrastructure will target OpenGL 2.1.
- If a future contributor wants to support pre-3.3 hardware, they should propose a new ADR with a concrete plan for handling the constraints listed above. The burden of proof is on the proposer.

LEGACY (OpenGL 3.3) remains the default and minimum supported tier.

## Consequences

**Positive:**
- Every renderer feature can assume GLSL 330, FBOs, VAOs, UBOs, and instancing as baseline capabilities. This eliminates an entire category of conditional code paths that would otherwise be needed.
- Shader library remains a single codebase (GLSL 330 core) rather than maintaining parallel 120/330 versions.
- Test matrix stays manageable: LEGACY (OpenGL 3.3), STANDARD (OpenGL 4.5 / Vulkan), MODERN (Vulkan).
- New contributors will not waste time attempting a RETRO implementation that conflicts with the engine's architectural assumptions.

**Negative:**
- Hardware from before ~2011 that cannot run OpenGL 3.3 drivers is excluded. In practice, this is a negligible user population for a game engine shipping in 2026.
- If a niche use case arises (e.g., embedded systems with only OpenGL ES 2.0), a separate effort would be needed. This ADR does not preclude that -- it simply establishes that the mainline engine will not carry the cost.

**Neutral:**
- The three remaining tiers (LEGACY, STANDARD, MODERN) provide clear coverage from 2012-era integrated GPUs through current-generation discrete GPUs with ray tracing. No meaningful hardware gap exists.
