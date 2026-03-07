#pragma once

// terrain_renderer.h -- Terrain render system for FFE.
//
// Queries ECS for entities with Transform3D + Terrain components and draws
// all chunks using the MESH_BLINN_PHONG shader. Called from Application::render()
// after meshRenderSystem, before skybox.
//
// The terrain render system sets 3D pipeline state (depth LESS, cull BACK),
// issues indexed draw calls per chunk, and restores 2D-compatible state after.
//
// No per-frame allocations. All GPU resources owned by the terrain asset cache.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).

#include "core/types.h"
#include "core/ecs.h"
#include "renderer/camera.h"
#include "renderer/shadow_map.h"
#include "renderer/mesh_renderer.h"  // FogParams, SceneLighting3D

namespace ffe::renderer {

// Render all entities with Transform3D + Terrain components.
//
// Uses MESH_BLINN_PHONG shader, same uniform setup as meshRenderSystem.
// Participates in shadow mapping if shadowCfg.enabled is true.
//
// Sets up depth test LESS, cull BACK, no blend.
// After rendering, restores 2D-compatible state (depth off, cull off, blend alpha).
//
// If no entities have Terrain + Transform3D, returns immediately.
void terrainRenderSystem(World& world, const Camera& camera3d,
                         const ShadowConfig& shadowCfg, const ShadowMap& shadowMap,
                         const FogParams& fog = FogParams{});

} // namespace ffe::renderer
