#include <catch2/catch_test_macros.hpp>

#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/camera.h"
#include "renderer/render_queue.h"
#include "renderer/shader_library.h"
#include "renderer/sprite_batch.h"

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
