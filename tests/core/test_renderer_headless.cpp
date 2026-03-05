#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/camera.h"
#include "renderer/render_queue.h"
#include "renderer/render_system.h"
#include "renderer/shader_library.h"
#include "renderer/sprite_batch.h"
#include "core/ecs.h"

using namespace ffe;
using namespace ffe::rhi;
using namespace ffe::renderer;

TEST_CASE("RHI headless init and shutdown", "[renderer]") {
    RhiConfig config;
    config.headless = true;

    REQUIRE(init(config) == RhiResult::OK);
    shutdown();
}

TEST_CASE("RHI headless buffer create returns valid handle", "[renderer]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    BufferDesc desc;
    desc.type = BufferType::VERTEX;
    desc.usage = BufferUsage::DYNAMIC;
    desc.sizeBytes = 1024;
    desc.data = nullptr;

    const BufferHandle h = createBuffer(desc);
    REQUIRE(isValid(h));

    destroyBuffer(h);
    shutdown();
}

TEST_CASE("RHI headless texture create returns valid handle", "[renderer]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    TextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.format = TextureFormat::RGBA8;
    desc.pixelData = nullptr;

    const TextureHandle h = createTexture(desc);
    REQUIRE(isValid(h));

    destroyTexture(h);
    shutdown();
}

TEST_CASE("RHI headless shader create returns valid handle", "[renderer]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    ShaderDesc desc;
    desc.vertexSource = "#version 330 core\nvoid main() { gl_Position = vec4(0); }";
    desc.fragmentSource = "#version 330 core\nout vec4 c;\nvoid main() { c = vec4(1); }";
    desc.debugName = "test_headless";

    const ShaderHandle h = createShader(desc);
    REQUIRE(isValid(h));

    destroyShader(h);
    shutdown();
}

TEST_CASE("Shader library loads in headless mode", "[renderer]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    ShaderLibrary lib;
    REQUIRE(initShaderLibrary(lib));

    // All handles should be valid
    REQUIRE(isValid(getShader(lib, BuiltinShader::SOLID)));
    REQUIRE(isValid(getShader(lib, BuiltinShader::TEXTURED)));
    REQUIRE(isValid(getShader(lib, BuiltinShader::SPRITE)));

    shutdownShaderLibrary(lib);
    shutdown();
}

TEST_CASE("Camera computes view-projection matrix", "[renderer]") {
    Camera cam;
    cam.projType = ProjectionType::ORTHOGRAPHIC;
    cam.orthoLeft = -640.0f;
    cam.orthoRight = 640.0f;
    cam.orthoBottom = -360.0f;
    cam.orthoTop = 360.0f;
    cam.viewportWidth = 1280.0f;
    cam.viewportHeight = 720.0f;

    const glm::mat4 vp = computeViewProjectionMatrix(cam);
    // VP matrix should not be identity (ortho projection changes it)
    REQUIRE(vp != glm::mat4(1.0f));
}

TEST_CASE("DrawCommand is exactly 64 bytes", "[renderer]") {
    REQUIRE(sizeof(DrawCommand) == 64);
}

TEST_CASE("Sort key encoding round-trips correctly", "[renderer]") {
    const u64 key = makeSortKey(5, 2, 100, 0.5f, 42);
    // Layer should be in bits 63..60
    const u8 layer = static_cast<u8>((key >> 60) & 0xF);
    REQUIRE(layer == 5);

    // Shader ID in bits 59..52
    const u8 shader = static_cast<u8>((key >> 52) & 0xFF);
    REQUIRE(shader == 2);

    // Texture ID in bits 51..40
    const u16 texId = static_cast<u16>((key >> 40) & 0xFFF);
    REQUIRE(texId == 100);

    // Sub-order in bits 15..0
    const u16 sub = static_cast<u16>(key & 0xFFFF);
    REQUIRE(sub == 42);
}

TEST_CASE("Pipeline bits pack and unpack correctly", "[renderer]") {
    PipelineState ps;
    ps.blend = BlendMode::ALPHA;
    ps.depth = DepthFunc::LESS;
    ps.cull = CullMode::BACK;
    ps.depthWrite = true;
    ps.wireframe = false;

    const u8 bits = packPipelineBits(ps);
    const PipelineState unpacked = unpackPipelineBits(bits);

    REQUIRE(unpacked.blend == ps.blend);
    REQUIRE(unpacked.depth == ps.depth);
    REQUIRE(unpacked.cull == ps.cull);
    REQUIRE(unpacked.depthWrite == ps.depthWrite);
    REQUIRE(unpacked.wireframe == ps.wireframe);
}

TEST_CASE("RenderQueue sort orders by sort key", "[renderer]") {
    DrawCommand cmds[3] = {};
    cmds[0].sortKey = makeSortKey(2, 0, 0, 0.0f);
    cmds[1].sortKey = makeSortKey(0, 0, 0, 0.0f);
    cmds[2].sortKey = makeSortKey(1, 0, 0, 0.0f);

    RenderQueue queue;
    queue.commands = cmds;
    queue.count = 3;
    queue.capacity = 3;

    sortRenderQueue(queue);

    // Should be sorted by layer: 0, 1, 2
    const u8 layer0 = static_cast<u8>((queue.commands[0].sortKey >> 60) & 0xF);
    const u8 layer1 = static_cast<u8>((queue.commands[1].sortKey >> 60) & 0xF);
    const u8 layer2 = static_cast<u8>((queue.commands[2].sortKey >> 60) & 0xF);
    REQUIRE(layer0 == 0);
    REQUIRE(layer1 == 1);
    REQUIRE(layer2 == 2);
}

// =============================================================================
// createTexture boundary conditions (validated before any GL call)
// =============================================================================

TEST_CASE("createTexture with zero width returns invalid handle", "[renderer][texture]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    TextureDesc desc;
    desc.width  = 0;
    desc.height = 64;
    desc.format = TextureFormat::RGBA8;
    desc.pixelData = nullptr;

    const TextureHandle h = createTexture(desc);
    REQUIRE_FALSE(isValid(h));

    shutdown();
}

TEST_CASE("createTexture with zero height returns invalid handle", "[renderer][texture]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    TextureDesc desc;
    desc.width  = 64;
    desc.height = 0;
    desc.format = TextureFormat::RGBA8;
    desc.pixelData = nullptr;

    const TextureHandle h = createTexture(desc);
    REQUIRE_FALSE(isValid(h));

    shutdown();
}

TEST_CASE("createTexture with width exceeding max returns invalid handle", "[renderer][texture]") {
    // MAX_TEXTURE_DIMENSION is 8192 (file-local in rhi_opengl.cpp); 8193 is one over.
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    TextureDesc desc;
    desc.width  = 8193;
    desc.height = 64;
    desc.format = TextureFormat::RGBA8;
    desc.pixelData = nullptr;

    const TextureHandle h = createTexture(desc);
    REQUIRE_FALSE(isValid(h));

    shutdown();
}

TEST_CASE("createTexture with height exceeding max returns invalid handle", "[renderer][texture]") {
    RhiConfig config;
    config.headless = true;
    REQUIRE(init(config) == RhiResult::OK);

    TextureDesc desc;
    desc.width  = 64;
    desc.height = 8193;
    desc.format = TextureFormat::RGBA8;
    desc.pixelData = nullptr;

    const TextureHandle h = createTexture(desc);
    REQUIRE_FALSE(isValid(h));

    shutdown();
}

// =============================================================================
// RenderQueue lifecycle and capacity
// =============================================================================

TEST_CASE("initRenderQueue and destroyRenderQueue complete without crash", "[renderer][renderqueue]") {
    RenderQueue queue;
    initRenderQueue(queue, 64);
    REQUIRE(queue.capacity == 64);
    REQUIRE(queue.count == 0);
    REQUIRE(queue.commands != nullptr);
    destroyRenderQueue(queue);
}

TEST_CASE("pushDrawCommand fills queue to capacity - count equals capacity", "[renderer][renderqueue]") {
    RenderQueue queue;
    constexpr u32 CAP = 16;
    initRenderQueue(queue, CAP);

    DrawCommand cmd{};
    for (u32 i = 0; i < CAP; ++i) {
        pushDrawCommand(queue, cmd);
    }

    REQUIRE(queue.count == CAP);

    destroyRenderQueue(queue);
}

TEST_CASE("pushDrawCommand on full queue does not exceed capacity", "[renderer][renderqueue]") {
    RenderQueue queue;
    constexpr u32 CAP = 8;
    initRenderQueue(queue, CAP);

    DrawCommand cmd{};
    // Fill to capacity
    for (u32 i = 0; i < CAP; ++i) {
        pushDrawCommand(queue, cmd);
    }
    // Push one more — must be silently dropped
    pushDrawCommand(queue, cmd);

    REQUIRE(queue.count == CAP);

    destroyRenderQueue(queue);
}

TEST_CASE("sortRenderQueue on empty queue does not crash", "[renderer][renderqueue]") {
    RenderQueue queue;
    initRenderQueue(queue, 32);
    // count is already 0
    sortRenderQueue(queue);
    REQUIRE(queue.count == 0);
    destroyRenderQueue(queue);
}

TEST_CASE("makeSortKey with layer > 15 masks to 4 bits (layer=16 same as layer=0)", "[renderer][renderqueue]") {
    // The layer field is 4 bits wide (bits 63..60).
    // layer=16 (0b10000) masked to 4 bits = 0 → same key as layer=0 with identical other fields.
    const u64 keyLayer0  = makeSortKey(0,  0, 0, 0.0f);
    const u64 keyLayer16 = makeSortKey(16, 0, 0, 0.0f);
    REQUIRE(keyLayer0 == keyLayer16);

    // layer=31 (0b11111) masked to 4 bits = 15 → same as layer=15
    const u64 keyLayer15 = makeSortKey(15, 0, 0, 0.0f);
    const u64 keyLayer31 = makeSortKey(31, 0, 0, 0.0f);
    REQUIRE(keyLayer15 == keyLayer31);
}

// =============================================================================
// Camera correctness — VP matrix maps known world position to clip space
// =============================================================================

TEST_CASE("Camera VP matrix maps world centre to clip origin for known ortho setup", "[renderer][camera]") {
    // Ortho camera covering [0, 800] x [0, 600] in world space.
    // Position at (0,0,5) looking at (0,0,0) — the default view direction.
    // World point (400, 300, 0) is the exact centre of the viewport and
    // should project to clip coordinates (0, 0).

    Camera cam;
    cam.projType     = ProjectionType::ORTHOGRAPHIC;
    cam.orthoLeft    = 0.0f;
    cam.orthoRight   = 800.0f;
    cam.orthoBottom  = 0.0f;
    cam.orthoTop     = 600.0f;
    cam.nearPlane    = 0.1f;
    cam.farPlane     = 100.0f;
    cam.position     = {0.0f, 0.0f, 5.0f};
    cam.target       = {0.0f, 0.0f, 0.0f};
    cam.up           = {0.0f, 1.0f, 0.0f};
    cam.viewportWidth  = 800.0f;
    cam.viewportHeight = 600.0f;

    const glm::mat4 vp = computeViewProjectionMatrix(cam);

    // Apply VP to world point (400, 300, 0, 1)
    const glm::vec4 world = {400.0f, 300.0f, 0.0f, 1.0f};
    const glm::vec4 clip  = vp * world;

    // x and y clip coordinates should be 0 (centre of the ortho volume).
    // Use margin (absolute tolerance) because epsilon is relative and
    // epsilon * 0.0 = 0 would give zero tolerance.
    constexpr float MARGIN = 1e-4f;
    REQUIRE(clip.x == Catch::Approx(0.0f).margin(MARGIN));
    REQUIRE(clip.y == Catch::Approx(0.0f).margin(MARGIN));
}

// =============================================================================
// renderPrepareSystem integration (headless — no real GL context required)
// =============================================================================

TEST_CASE("renderPrepareSystem populates render queue with one entry per sprite entity", "[renderer][rendersystem]") {
    // renderPrepareSystem reads from the ECS context:
    //   - RenderQueue* (pointer stored in registry ctx)
    //   - ShaderLibrary (value stored in registry ctx)
    //
    // In headless mode the ShaderLibrary handles are non-zero (the headless
    // path in the RHI returns monotonically-increasing IDs), so the shader
    // handles are valid and the system can run.

    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 64);

    // Build an ECS world and inject the required context objects
    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    // Create 3 entities, each with Transform + Sprite
    for (int i = 0; i < 3; ++i) {
        const ffe::EntityId e = world.createEntity();
        ffe::Transform& t = world.addComponent<ffe::Transform>(e);
        t.position = {static_cast<float>(i) * 32.0f, 0.0f, 0.0f};
        t.scale    = {1.0f, 1.0f, 1.0f};
        t.rotation = 0.0f;

        ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
        s.size    = {32.0f, 32.0f};
        s.layer   = 0;
        s.texture = rhi::TextureHandle{1}; // non-zero = valid in headless
        s.color   = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    // Run the system
    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 3);

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}
