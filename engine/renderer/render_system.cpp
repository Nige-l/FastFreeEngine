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

void animationUpdateSystem(World& world, const float dt) {
    ZoneScopedN("AnimationUpdate");

    const auto view = world.view<Sprite, SpriteAnimation>();
    for (const auto entity : view) {
        auto& anim = view.get<SpriteAnimation>(entity);
        if (!anim.playing) continue;

        anim.elapsed += dt;
        while (anim.elapsed >= anim.frameTime) {
            anim.elapsed -= anim.frameTime;
            ++anim.currentFrame;
            if (anim.currentFrame >= anim.frameCount) {
                if (anim.looping) {
                    anim.currentFrame = 0;
                } else {
                    anim.currentFrame = anim.frameCount - 1;
                    anim.playing = false;
                    break;
                }
            }
        }

        // Compute UV coordinates from frame index and grid layout.
        // Assumes the full texture is a uniform grid: columns x rows.
        auto& sprite = view.get<Sprite>(entity);
        const u16 col  = anim.currentFrame % anim.columns;
        const u16 row  = anim.currentFrame / anim.columns;
        const u16 rows = (anim.frameCount + anim.columns - 1) / anim.columns;

        const f32 uWidth  = 1.0f / static_cast<f32>(anim.columns);
        const f32 vHeight = 1.0f / static_cast<f32>(rows);

        sprite.uvMin.x = static_cast<f32>(col) * uWidth;
        sprite.uvMin.y = static_cast<f32>(row) * vHeight;
        sprite.uvMax.x = sprite.uvMin.x + uWidth;
        sprite.uvMax.y = sprite.uvMin.y + vHeight;
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
                                  const f32 scaleX, const f32 scaleY, const f32 posZ,
                                  const f32 rotation) {
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
        cmd.posX     = posX;
        cmd.posY     = posY;
        cmd.scaleX   = scaleX;
        cmd.scaleY   = scaleY;
        cmd.rotation = rotation;
        cmd.uvMinX  = sprite.flipX ? sprite.uvMax.x : sprite.uvMin.x;
        cmd.uvMinY  = sprite.flipY ? sprite.uvMax.y : sprite.uvMin.y;
        cmd.uvMaxX  = sprite.flipX ? sprite.uvMin.x : sprite.uvMax.x;
        cmd.uvMaxY  = sprite.flipY ? sprite.uvMin.y : sprite.uvMax.y;
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

        const f32 posX     = prev.position.x + (curr.position.x - prev.position.x) * alpha;
        const f32 posY     = prev.position.y + (curr.position.y - prev.position.y) * alpha;
        const f32 scaleX   = (prev.scale.x + (curr.scale.x - prev.scale.x) * alpha) * sprite.size.x;
        const f32 scaleY   = (prev.scale.y + (curr.scale.y - prev.scale.y) * alpha) * sprite.size.y;
        const f32 rotation = prev.rotation + (curr.rotation - prev.rotation) * alpha;

        // Use current Z for depth sort (Z changes between ticks are uncommon in 2D)
        writeCommand(sprite, posX, posY, scaleX, scaleY, curr.position.z, rotation);
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
                     transform.position.z,
                     transform.rotation);
    }
}

} // namespace ffe::renderer
