#include "renderer/render_system.h"
#include "renderer/shader_library.h"
#include "core/ecs.h"

#include <tracy/Tracy.hpp>

namespace ffe::renderer {

void renderPrepareSystem(World& world, const float dt) {
    ZoneScopedN("RenderPrepare");
    (void)dt;

    // Get the render queue from the world context
    auto* queuePtr = world.registry().ctx().find<RenderQueue*>();
    if (queuePtr == nullptr || *queuePtr == nullptr) return;
    RenderQueue& rq = **queuePtr;

    // Get the shader library from context
    const auto* shaderLib = world.registry().ctx().find<ShaderLibrary>();
    if (shaderLib == nullptr) return;

    // Iterate all entities with Transform + Sprite
    const auto spriteView = world.view<const Transform, const Sprite>();
    for (const auto entity : spriteView) {
        if (rq.count >= rq.capacity) break;

        const auto& transform = spriteView.get<const Transform>(entity);
        const auto& sprite    = spriteView.get<const Sprite>(entity);

        DrawCommand& cmd = rq.commands[rq.count++];
        cmd.sortKey = makeSortKey(
            static_cast<u8>(glm::clamp(static_cast<i32>(sprite.layer), 0, 15)),
            static_cast<u8>(BuiltinShader::SPRITE),
            static_cast<u16>(sprite.texture.id & 0xFFF),
            transform.position.z,
            static_cast<u16>(sprite.sortOrder & 0xFFFF)
        );

        cmd.shader  = getShader(*shaderLib, BuiltinShader::SPRITE);
        cmd.texture = sprite.texture;

        cmd.posX   = transform.position.x;
        cmd.posY   = transform.position.y;
        cmd.scaleX = transform.scale.x * sprite.size.x;
        cmd.scaleY = transform.scale.y * sprite.size.y;

        // Sprite pipeline: alpha blend, no depth test, no culling
        rhi::PipelineState ps;
        ps.blend      = rhi::BlendMode::ALPHA;
        ps.depth      = rhi::DepthFunc::NONE;
        ps.cull       = rhi::CullMode::NONE;
        ps.depthWrite = false;
        cmd.pipelineBits = packPipelineBits(ps);
    }
}

} // namespace ffe::renderer
