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

TEST_CASE("DrawCommand is exactly 80 bytes", "[renderer]") {
    REQUIRE(sizeof(DrawCommand) == 80);
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
    //
    // Entities without PreviousTransform are static — they use the raw current
    // Transform position (Pass 2 / entt::exclude<PreviousTransform> view).

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

    // Create 3 entities, each with Transform + Sprite (no PreviousTransform — static path)
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

    // Run the system (alpha is unused by static entities)
    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 3);

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

// =============================================================================
// Interpolation lerp correctness — PreviousTransform path
// =============================================================================

TEST_CASE("renderPrepareSystem lerp: alpha=0 yields previous position", "[renderer][rendersystem][interp]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& curr = world.addComponent<ffe::Transform>(e);
    curr.position = {100.0f, 200.0f, 0.0f};
    curr.scale    = {1.0f, 1.0f, 1.0f};

    ffe::PreviousTransform& prev = world.addComponent<ffe::PreviousTransform>(e);
    prev.position = {0.0f, 0.0f, 0.0f};
    prev.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    // alpha=0 → lerped position = prev + (curr - prev) * 0 = prev
    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].posX == Catch::Approx(0.0f));
    REQUIRE(queue.commands[0].posY == Catch::Approx(0.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem lerp: alpha=1 yields current position", "[renderer][rendersystem][interp]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& curr = world.addComponent<ffe::Transform>(e);
    curr.position = {100.0f, 200.0f, 0.0f};
    curr.scale    = {1.0f, 1.0f, 1.0f};

    ffe::PreviousTransform& prev = world.addComponent<ffe::PreviousTransform>(e);
    prev.position = {0.0f, 0.0f, 0.0f};
    prev.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    // alpha=1 → lerped position = prev + (curr - prev) * 1 = curr
    ffe::renderer::renderPrepareSystem(world, 1.0f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].posX == Catch::Approx(100.0f));
    REQUIRE(queue.commands[0].posY == Catch::Approx(200.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem lerp: alpha=0.5 yields midpoint position", "[renderer][rendersystem][interp]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& curr = world.addComponent<ffe::Transform>(e);
    curr.position = {80.0f, 60.0f, 0.0f};
    curr.scale    = {1.0f, 1.0f, 1.0f};

    ffe::PreviousTransform& prev = world.addComponent<ffe::PreviousTransform>(e);
    prev.position = {20.0f, 40.0f, 0.0f};
    prev.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    // alpha=0.5 → x = 20 + (80 - 20) * 0.5 = 50, y = 40 + (60 - 40) * 0.5 = 50
    ffe::renderer::renderPrepareSystem(world, 0.5f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].posX == Catch::Approx(50.0f));
    REQUIRE(queue.commands[0].posY == Catch::Approx(50.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem: static entity without PreviousTransform uses raw position", "[renderer][rendersystem][interp]") {
    // Entities without PreviousTransform bypass the lerp entirely.
    // Their position in the DrawCommand must equal their Transform.position
    // regardless of alpha.

    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& t = world.addComponent<ffe::Transform>(e);
    t.position = {77.0f, -33.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};

    // No PreviousTransform — static entity
    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    ffe::renderer::renderPrepareSystem(world, 0.5f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].posX == Catch::Approx(77.0f));
    REQUIRE(queue.commands[0].posY == Catch::Approx(-33.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

// =============================================================================
// Rotation pass-through — renderPrepareSystem writes rotation into DrawCommand
// =============================================================================

TEST_CASE("renderPrepareSystem passes rotation through for static entities", "[renderer][rendersystem][rotation]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& t = world.addComponent<ffe::Transform>(e);
    t.position = {0.0f, 0.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};
    t.rotation = 1.57f; // ~pi/2

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].rotation == Catch::Approx(1.57f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem lerps rotation with alpha=0.5", "[renderer][rendersystem][rotation]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& curr = world.addComponent<ffe::Transform>(e);
    curr.position = {0.0f, 0.0f, 0.0f};
    curr.scale    = {1.0f, 1.0f, 1.0f};
    curr.rotation = 2.0f;

    ffe::PreviousTransform& prev = world.addComponent<ffe::PreviousTransform>(e);
    prev.position = {0.0f, 0.0f, 0.0f};
    prev.scale    = {1.0f, 1.0f, 1.0f};
    prev.rotation = 0.0f;

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    // alpha=0.5 → rotation = 0 + (2 - 0) * 0.5 = 1.0
    ffe::renderer::renderPrepareSystem(world, 0.5f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].rotation == Catch::Approx(1.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem rotation: alpha=0 yields previous rotation", "[renderer][rendersystem][rotation]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& curr = world.addComponent<ffe::Transform>(e);
    curr.position = {0.0f, 0.0f, 0.0f};
    curr.scale    = {1.0f, 1.0f, 1.0f};
    curr.rotation = 3.14f;

    ffe::PreviousTransform& prev = world.addComponent<ffe::PreviousTransform>(e);
    prev.position = {0.0f, 0.0f, 0.0f};
    prev.scale    = {1.0f, 1.0f, 1.0f};
    prev.rotation = 1.0f;

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};

    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].rotation == Catch::Approx(1.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("DrawCommand default rotation is zero", "[renderer][rotation]") {
    DrawCommand cmd{};
    REQUIRE(cmd.rotation == 0.0f);
}

// =============================================================================
// Sprite flipping — UV swap in renderPrepareSystem
// =============================================================================

TEST_CASE("renderPrepareSystem flips UVs horizontally when flipX is set", "[renderer][rendersystem][flip]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& t = world.addComponent<ffe::Transform>(e);
    t.position = {0.0f, 0.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};
    s.uvMin   = {0.0f, 0.0f};
    s.uvMax   = {1.0f, 1.0f};
    s.flipX   = true;
    s.flipY   = false;

    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 1);
    // flipX swaps uvMinX and uvMaxX
    REQUIRE(queue.commands[0].uvMinX == Catch::Approx(1.0f));
    REQUIRE(queue.commands[0].uvMaxX == Catch::Approx(0.0f));
    // Y unchanged
    REQUIRE(queue.commands[0].uvMinY == Catch::Approx(0.0f));
    REQUIRE(queue.commands[0].uvMaxY == Catch::Approx(1.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem flips UVs vertically when flipY is set", "[renderer][rendersystem][flip]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& t = world.addComponent<ffe::Transform>(e);
    t.position = {0.0f, 0.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};
    s.uvMin   = {0.0f, 0.0f};
    s.uvMax   = {1.0f, 1.0f};
    s.flipX   = false;
    s.flipY   = true;

    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 1);
    // X unchanged
    REQUIRE(queue.commands[0].uvMinX == Catch::Approx(0.0f));
    REQUIRE(queue.commands[0].uvMaxX == Catch::Approx(1.0f));
    // flipY swaps uvMinY and uvMaxY
    REQUIRE(queue.commands[0].uvMinY == Catch::Approx(1.0f));
    REQUIRE(queue.commands[0].uvMaxY == Catch::Approx(0.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

TEST_CASE("renderPrepareSystem no flip leaves UVs unchanged", "[renderer][rendersystem][flip]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    RenderQueue queue;
    initRenderQueue(queue, 8);

    ffe::World world;
    world.registry().ctx().emplace<RenderQueue*>(&queue);
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();

    ffe::Transform& t = world.addComponent<ffe::Transform>(e);
    t.position = {0.0f, 0.0f, 0.0f};
    t.scale    = {1.0f, 1.0f, 1.0f};

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size    = {32.0f, 32.0f};
    s.layer   = 0;
    s.texture = rhi::TextureHandle{1};
    s.color   = {1.0f, 1.0f, 1.0f, 1.0f};
    s.uvMin   = {0.25f, 0.5f};
    s.uvMax   = {0.75f, 1.0f};
    s.flipX   = false;
    s.flipY   = false;

    ffe::renderer::renderPrepareSystem(world, 0.0f);

    REQUIRE(queue.count == 1);
    REQUIRE(queue.commands[0].uvMinX == Catch::Approx(0.25f));
    REQUIRE(queue.commands[0].uvMinY == Catch::Approx(0.5f));
    REQUIRE(queue.commands[0].uvMaxX == Catch::Approx(0.75f));
    REQUIRE(queue.commands[0].uvMaxY == Catch::Approx(1.0f));

    destroyRenderQueue(queue);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

// =============================================================================
// Tilemap — initTilemap, destroyTilemap, renderTilemaps
// =============================================================================

TEST_CASE("initTilemap allocates tile data initialised to zero", "[renderer][tilemap]") {
    ffe::Tilemap tm;
    REQUIRE(ffe::initTilemap(tm, 10, 8));
    REQUIRE(tm.width == 10);
    REQUIRE(tm.height == 8);
    REQUIRE(tm.tiles != nullptr);

    // All tiles should be zero (empty)
    for (ffe::u32 i = 0; i < 10u * 8u; ++i) {
        REQUIRE(tm.tiles[i] == 0);
    }

    ffe::destroyTilemap(tm);
    REQUIRE(tm.tiles == nullptr);
}

TEST_CASE("initTilemap rejects zero dimensions", "[renderer][tilemap]") {
    ffe::Tilemap tm;
    REQUIRE_FALSE(ffe::initTilemap(tm, 0, 10));
    REQUIRE_FALSE(ffe::initTilemap(tm, 10, 0));
}

TEST_CASE("initTilemap rejects oversized dimensions", "[renderer][tilemap]") {
    ffe::Tilemap tm;
    REQUIRE_FALSE(ffe::initTilemap(tm, 1025, 10));
    REQUIRE_FALSE(ffe::initTilemap(tm, 10, 1025));
}

TEST_CASE("destroyTilemap is safe on uninitialised tilemap", "[renderer][tilemap]") {
    ffe::Tilemap tm;
    ffe::destroyTilemap(tm); // Should not crash
    REQUIRE(tm.tiles == nullptr);
}

TEST_CASE("renderTilemaps produces no output for empty tilemap", "[renderer][tilemap]") {
    RhiConfig rhiCfg;
    rhiCfg.headless = true;
    REQUIRE(init(rhiCfg) == RhiResult::OK);

    ShaderLibrary shaderLib;
    REQUIRE(initShaderLibrary(shaderLib));

    ffe::World world;
    world.registry().ctx().emplace<ShaderLibrary>(shaderLib);

    const ffe::EntityId e = world.createEntity();
    ffe::Transform& t = world.addComponent<ffe::Transform>(e);
    t.position = {0.0f, 0.0f, 0.0f};

    ffe::Tilemap& tm = world.addComponent<ffe::Tilemap>(e);
    REQUIRE(ffe::initTilemap(tm, 4, 4));
    tm.tileWidth  = 16.0f;
    tm.tileHeight = 16.0f;
    tm.texture    = rhi::TextureHandle{1};
    tm.columns    = 4;
    tm.tileCount  = 16;
    // All tiles are 0 (empty) — nothing should be rendered

    // Create a sprite batch for the test
    SpriteBatch batch;
    initSpriteBatch(batch, getShader(shaderLib, BuiltinShader::SPRITE));

    rhi::SpriteVertex staging[MAX_BATCH_VERTICES];
    beginSpriteBatch(batch, staging);
    renderTilemaps(world, batch);
    endSpriteBatch(batch);

    // No sprites should have been added (all tiles empty)
    REQUIRE(batch.drawCallCount == 0);

    ffe::destroyTilemap(tm);
    shutdownSpriteBatch(batch);
    shutdownShaderLibrary(shaderLib);
    shutdown();
}

// =============================================================================
// animationUpdateSystem — frame advancement and UV computation
// =============================================================================

TEST_CASE("animationUpdateSystem advances frame when elapsed exceeds frameTime", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 4;
    anim.columns = 4;   // 4 columns, 1 row
    anim.frameTime = 0.1f;
    anim.playing = true;
    anim.looping = true;
    anim.elapsed = 0.0f;
    anim.currentFrame = 0;

    // Tick with dt=0.15 — exceeds one frameTime (0.1), should advance to frame 1
    ffe::renderer::animationUpdateSystem(world, 0.15f);
    REQUIRE(anim.currentFrame == 1);
    // Residual elapsed should be ~0.05
    REQUIRE(anim.elapsed == Catch::Approx(0.05f));
}

TEST_CASE("animationUpdateSystem does not advance when not playing", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 4;
    anim.columns = 4;
    anim.frameTime = 0.1f;
    anim.playing = false;
    anim.currentFrame = 0;
    anim.elapsed = 0.0f;

    ffe::renderer::animationUpdateSystem(world, 1.0f);
    REQUIRE(anim.currentFrame == 0);
    REQUIRE(anim.elapsed == Catch::Approx(0.0f));
}

TEST_CASE("animationUpdateSystem wraps frame with looping enabled", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 3;
    anim.columns = 3;
    anim.frameTime = 0.1f;
    anim.playing = true;
    anim.looping = true;
    anim.currentFrame = 2;  // last frame
    anim.elapsed = 0.0f;

    // dt=0.1 pushes past the last frame → should wrap to 0
    ffe::renderer::animationUpdateSystem(world, 0.1f);
    REQUIRE(anim.currentFrame == 0);
    REQUIRE(anim.playing == true);
}

TEST_CASE("animationUpdateSystem stops at last frame when not looping", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 3;
    anim.columns = 3;
    anim.frameTime = 0.1f;
    anim.playing = true;
    anim.looping = false;
    anim.currentFrame = 2;  // last frame
    anim.elapsed = 0.0f;

    ffe::renderer::animationUpdateSystem(world, 0.1f);
    REQUIRE(anim.currentFrame == 2);  // stays at last frame
    REQUIRE(anim.playing == false);   // playing stops
}

TEST_CASE("animationUpdateSystem computes correct UVs for 2x2 grid frame 3", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 4;
    anim.columns = 2;   // 2 columns, 2 rows
    anim.frameTime = 0.1f;
    anim.playing = true;
    anim.looping = true;
    anim.currentFrame = 2;  // will advance to frame 3
    anim.elapsed = 0.0f;

    ffe::renderer::animationUpdateSystem(world, 0.1f);
    REQUIRE(anim.currentFrame == 3);

    // Frame 3 in a 2x2 grid: col=3%2=1, row=3/2=1
    // uWidth=0.5, vHeight=0.5
    REQUIRE(s.uvMin.x == Catch::Approx(0.5f));
    REQUIRE(s.uvMin.y == Catch::Approx(0.5f));
    REQUIRE(s.uvMax.x == Catch::Approx(1.0f));
    REQUIRE(s.uvMax.y == Catch::Approx(1.0f));
}

TEST_CASE("animationUpdateSystem computes correct UVs for 4x1 grid frame 2", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 4;
    anim.columns = 4;   // 4 columns, 1 row
    anim.frameTime = 0.1f;
    anim.playing = true;
    anim.looping = true;
    anim.currentFrame = 1;  // will advance to frame 2
    anim.elapsed = 0.0f;

    ffe::renderer::animationUpdateSystem(world, 0.1f);
    REQUIRE(anim.currentFrame == 2);

    // Frame 2 in a 4x1 grid: col=2%4=2, row=2/4=0
    // uWidth=0.25, vHeight=1.0
    REQUIRE(s.uvMin.x == Catch::Approx(0.5f));
    REQUIRE(s.uvMin.y == Catch::Approx(0.0f));
    REQUIRE(s.uvMax.x == Catch::Approx(0.75f));
    REQUIRE(s.uvMax.y == Catch::Approx(1.0f));
}

TEST_CASE("animationUpdateSystem handles multiple frame advance in single dt", "[renderer][animation]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();

    ffe::Sprite& s = world.addComponent<ffe::Sprite>(e);
    s.size = {32.0f, 32.0f};
    s.texture = rhi::TextureHandle{1};

    ffe::SpriteAnimation& anim = world.addComponent<ffe::SpriteAnimation>(e);
    anim.frameCount = 8;
    anim.columns = 8;
    anim.frameTime = 0.1f;
    anim.playing = true;
    anim.looping = true;
    anim.currentFrame = 0;
    anim.elapsed = 0.0f;

    // dt=0.35 → should advance 3 frames (0.35 / 0.1 = 3 full frames, 0.05 residual)
    ffe::renderer::animationUpdateSystem(world, 0.35f);
    REQUIRE(anim.currentFrame == 3);
    REQUIRE(anim.elapsed == Catch::Approx(0.05f).margin(1e-6f));
}
