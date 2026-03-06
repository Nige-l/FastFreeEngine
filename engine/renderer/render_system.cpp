#include "renderer/render_system.h"
#include "renderer/shader_library.h"
#include "core/ecs.h"

#include <tracy/Tracy.hpp>

namespace ffe::renderer {

void copyTransformSystem(World& world, const float /*dt*/) {
    ZoneScopedN("CopyTransform");

    // Snapshot current Transform into PreviousTransform for every entity that
    // has both components. This must execute before any gameplay system
    // modifies Transform so that the snapshot captures the pre-update position.
    const auto view = world.view<const Transform, PreviousTransform>();
    for (const auto entity : view) {
        const auto& curr = view.get<const Transform>(entity);
        auto& prev       = view.get<PreviousTransform>(entity);
        prev.position = curr.position;
        prev.scale    = curr.scale;
        prev.rotation = curr.rotation;
    }
}

void renderPrepareSystem(World& world, const float alpha) {
    ZoneScopedN("RenderPrepare");

    // Get the render queue from the world context
    auto* queuePtr = world.registry().ctx().find<RenderQueue*>();
    if (queuePtr == nullptr || *queuePtr == nullptr) return;
    RenderQueue& rq = **queuePtr;

    // Get the shader library from context
    const auto* shaderLib = world.registry().ctx().find<ShaderLibrary>();
    if (shaderLib == nullptr) return;

    // Sprite pipeline state is constant for all sprites — pack it once before the loop.
    rhi::PipelineState spritePipeline;
    spritePipeline.blend      = rhi::BlendMode::ALPHA;
    spritePipeline.depth      = rhi::DepthFunc::NONE;
    spritePipeline.cull       = rhi::CullMode::NONE;
    spritePipeline.depthWrite = false;
    const u8 spritePipelineBits = packPipelineBits(spritePipeline);

    // Helper lambda — writes a DrawCommand from already-resolved position and scale.
    // Avoids duplicating the sort key / color pack code in two loops below.
    const auto writeCommand = [&](const Sprite& sprite, const f32 posX, const f32 posY,
                                  const f32 scaleX, const f32 scaleY, const f32 posZ) {
        if (rq.count >= rq.capacity) return;
        DrawCommand& cmd = rq.commands[rq.count++];
        cmd.sortKey = makeSortKey(
            static_cast<u8>(glm::clamp(static_cast<i32>(sprite.layer), 0, 15)),
            static_cast<u8>(BuiltinShader::SPRITE),
            static_cast<u16>(sprite.texture.id & 0xFFF),
            posZ,
            static_cast<u16>(sprite.sortOrder & 0xFFFF)
        );
        cmd.shader  = getShader(*shaderLib, BuiltinShader::SPRITE);
        cmd.texture = sprite.texture;
        cmd.posX    = posX;
        cmd.posY    = posY;
        cmd.scaleX  = scaleX;
        cmd.scaleY  = scaleY;
        cmd.colorR  = static_cast<u8>(glm::clamp(sprite.color.r, 0.0f, 1.0f) * 255.0f);
        cmd.colorG  = static_cast<u8>(glm::clamp(sprite.color.g, 0.0f, 1.0f) * 255.0f);
        cmd.colorB  = static_cast<u8>(glm::clamp(sprite.color.b, 0.0f, 1.0f) * 255.0f);
        cmd.colorA  = static_cast<u8>(glm::clamp(sprite.color.a, 0.0f, 1.0f) * 255.0f);
        cmd.pipelineBits = spritePipelineBits;
    };

    // Pass 1: entities with PreviousTransform — lerp between previous and current.
    // EnTT guarantees no entity appears in both views when the exclude<> filter is used.
    const auto interpView = world.view<const PreviousTransform, const Transform, const Sprite>();
    for (const auto entity : interpView) {
        const auto& prev  = interpView.get<const PreviousTransform>(entity);
        const auto& curr  = interpView.get<const Transform>(entity);
        const auto& sprite = interpView.get<const Sprite>(entity);

        const f32 posX   = prev.position.x + (curr.position.x - prev.position.x) * alpha;
        const f32 posY   = prev.position.y + (curr.position.y - prev.position.y) * alpha;
        const f32 scaleX = (prev.scale.x + (curr.scale.x - prev.scale.x) * alpha) * sprite.size.x;
        const f32 scaleY = (prev.scale.y + (curr.scale.y - prev.scale.y) * alpha) * sprite.size.y;

        // Use current Z for depth sort (Z changes between ticks are uncommon in 2D)
        writeCommand(sprite, posX, posY, scaleX, scaleY, curr.position.z);
    }

    // Pass 2: entities without PreviousTransform (static scenery, UI) — no lerp.
    // Uses registry() directly because World::view() does not expose the exclude overload.
    const auto staticView = world.registry().view<const Transform, const Sprite>(
        entt::exclude<PreviousTransform>);
    for (const auto entity : staticView) {
        const auto& transform = staticView.get<const Transform>(entity);
        const auto& sprite    = staticView.get<const Sprite>(entity);

        writeCommand(sprite,
                     transform.position.x,
                     transform.position.y,
                     transform.scale.x * sprite.size.x,
                     transform.scale.y * sprite.size.y,
                     transform.position.z);
    }
}

} // namespace ffe::renderer
