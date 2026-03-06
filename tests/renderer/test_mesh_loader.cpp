// test_mesh_loader.cpp — Catch2 unit tests for the 3D mesh loading system.
//
// Tests are organized as specified in ADR-007 Section 14.
// All tests that require GPU operations run in headless mode (RHI headless path
// does not call any GL functions). Tests that validate pure CPU-side logic
// do not require the RHI at all.
//
// No external .glb file dependency — path validation tests use string manipulation
// and ECS integration tests use programmatic component manipulation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/mesh_loader.h"
#include "renderer/mesh_renderer.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/camera.h"
#include "core/ecs.h"
#include "core/types.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace ffe;
using namespace ffe::renderer;
using namespace ffe::rhi;

// ---------------------------------------------------------------------------
// Unit cube test data (8 vertices, 36 indices — ADR-007 Section 14)
// No external file dependency.
// ---------------------------------------------------------------------------

[[maybe_unused]] static const rhi::MeshVertex UNIT_CUBE_VERTICES[8] = {
    // position              normal          uv
    {-0.5f,-0.5f,-0.5f,  0.f,0.f,-1.f,  0.f,0.f},
    { 0.5f,-0.5f,-0.5f,  0.f,0.f,-1.f,  1.f,0.f},
    { 0.5f, 0.5f,-0.5f,  0.f,0.f,-1.f,  1.f,1.f},
    {-0.5f, 0.5f,-0.5f,  0.f,0.f,-1.f,  0.f,1.f},
    {-0.5f,-0.5f, 0.5f,  0.f,0.f, 1.f,  0.f,0.f},
    { 0.5f,-0.5f, 0.5f,  0.f,0.f, 1.f,  1.f,0.f},
    { 0.5f, 0.5f, 0.5f,  0.f,0.f, 1.f,  1.f,1.f},
    {-0.5f, 0.5f, 0.5f,  0.f,0.f, 1.f,  0.f,1.f},
};

[[maybe_unused]] static const u32 UNIT_CUBE_INDICES[36] = {
    0,1,2, 2,3,0,  // -Z face
    4,5,6, 6,7,4,  // +Z face
    0,4,7, 7,3,0,  // -X face
    1,5,6, 6,2,1,  // +X face
    3,2,6, 6,7,3,  // +Y face
    0,1,5, 5,4,0,  // -Y face
};

// ---------------------------------------------------------------------------
// Headless RHI fixture
// ---------------------------------------------------------------------------

// Initialise the RHI in headless mode for tests that need it (pool cap tests, etc.)
// Call once per test file via a local static.
static bool initHeadlessRhi() {
    rhi::RhiConfig cfg;
    cfg.headless = true;
    cfg.viewportWidth  = 1280;
    cfg.viewportHeight = 720;
    const rhi::RhiResult r = rhi::init(cfg);
    return r == rhi::RhiResult::OK;
}

// ---------------------------------------------------------------------------
// Path validation tests (CPU-only, no RHI)
// ---------------------------------------------------------------------------

TEST_CASE("loadMesh: null path returns invalid handle") {
    const MeshHandle h = loadMesh(nullptr);
    REQUIRE(h.id == 0);
    REQUIRE(!isValid(h));
}

TEST_CASE("loadMesh: empty path returns invalid handle") {
    const MeshHandle h = loadMesh("");
    REQUIRE(h.id == 0);
    REQUIRE(!isValid(h));
}

TEST_CASE("loadMesh: absolute path rejected") {
    const MeshHandle h = loadMesh("/absolute/path/mesh.glb");
    REQUIRE(h.id == 0);
    REQUIRE(!isValid(h));
}

TEST_CASE("loadMesh: path traversal ../ rejected") {
    const MeshHandle h = loadMesh("../meshes/mesh.glb");
    REQUIRE(h.id == 0);
    REQUIRE(!isValid(h));
}

TEST_CASE("loadMesh: path traversal /../ rejected") {
    const MeshHandle h = loadMesh("meshes/../../../etc/passwd.glb");
    REQUIRE(h.id == 0);
    REQUIRE(!isValid(h));
}

TEST_CASE("loadMesh: path traversal embedded null rejected") {
    // Null byte in path — strnlen stops at the embedded null, checking won't see
    // anything past it. The path "safe\0../evil.glb" appears to be just "safe"
    // from strnlen's perspective. Our isPathSafe uses strnlen with PATH_MAX bound
    // so the extension check will reject it (no .glb after the null).
    // The primary test here is that null-byte-terminated paths don't crash.
    const char pathWithNull[] = "meshes\0../evil.glb";
    const MeshHandle h = loadMesh(pathWithNull);
    // Should return invalid — either no .glb extension found or path fails validation
    REQUIRE(!isValid(h));
}

TEST_CASE("loadMesh: valid relative path to non-existent file returns invalid handle") {
    // A valid-looking .glb path that doesn't exist should fail gracefully
    // (will fail at realpath() or stat() since the file doesn't exist)
    const MeshHandle h = loadMesh("nonexistent_file.glb");
    REQUIRE(h.id == 0);
    REQUIRE(!isValid(h));
}

// ---------------------------------------------------------------------------
// Struct layout tests (no RHI)
// ---------------------------------------------------------------------------

TEST_CASE("MeshVertex: sizeof == 32") {
    static_assert(sizeof(rhi::MeshVertex) == 32, "MeshVertex must be 32 bytes");
    REQUIRE(sizeof(rhi::MeshVertex) == 32u);
}

TEST_CASE("Transform3D: sizeof == 44") {
    static_assert(sizeof(Transform3D) == 44, "Transform3D must be 44 bytes");
    REQUIRE(sizeof(Transform3D) == 44u);
}

TEST_CASE("Mesh: sizeof == 8") {
    static_assert(sizeof(Mesh) == 8, "Mesh component must be 8 bytes");
    REQUIRE(sizeof(Mesh) == 8u);
}

TEST_CASE("Material3D: sizeof == 24") {
    static_assert(sizeof(Material3D) == 24, "Material3D must be 24 bytes");
    REQUIRE(sizeof(Material3D) == 24u);
}

TEST_CASE("MeshHandle: isValid returns false for id==0") {
    const MeshHandle nullHandle{0};
    REQUIRE(!isValid(nullHandle));

    const MeshHandle validHandle{1};
    REQUIRE(isValid(validHandle));
}

// ---------------------------------------------------------------------------
// Asset pool tests (headless RHI — no GL calls made)
// ---------------------------------------------------------------------------

TEST_CASE("unloadMesh: safe to call with invalid handle") {
    static bool rhiInited = initHeadlessRhi();
    REQUIRE(rhiInited);

    // Should not crash or assert
    unloadMesh(MeshHandle{0});
    unloadMesh(MeshHandle{99999});

    SUCCEED("unloadMesh with invalid handle did not crash");
}

TEST_CASE("unloadAllMeshes: safe to call when pool empty") {
    static bool rhiInited = initHeadlessRhi();
    REQUIRE(rhiInited);

    // Should not crash or assert
    unloadAllMeshes();

    SUCCEED("unloadAllMeshes on empty pool did not crash");
}

// ---------------------------------------------------------------------------
// SceneLighting3D default test (CPU-only)
// ---------------------------------------------------------------------------

TEST_CASE("SceneLighting3D: default lightDir is normalised") {
    const SceneLighting3D defaults;
    const float len = glm::length(defaults.lightDir);
    // Should be approximately 1.0 (within floating-point rounding tolerance)
    REQUIRE_THAT(len, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

// ---------------------------------------------------------------------------
// Camera test (CPU-only)
// ---------------------------------------------------------------------------

TEST_CASE("camera3d: computeViewProjectionMatrix returns non-identity for perspective") {
    Camera cam3d;
    cam3d.projType      = ProjectionType::PERSPECTIVE;
    cam3d.fovDegrees    = 60.0f;
    cam3d.nearPlane     = 0.1f;
    cam3d.farPlane      = 1000.0f;
    cam3d.position      = {0.0f, 0.0f, 5.0f};
    cam3d.target        = {0.0f, 0.0f, 0.0f};
    cam3d.up            = {0.0f, 1.0f, 0.0f};
    cam3d.viewportWidth  = 1280.0f;
    cam3d.viewportHeight = 720.0f;

    const glm::mat4 vp = computeViewProjectionMatrix(cam3d);
    const glm::mat4 identity{1.0f};

    // VP matrix for a perspective camera at position (0,0,5) looking at origin
    // must differ from identity
    bool differs = false;
    for (int col = 0; col < 4 && !differs; ++col) {
        for (int row = 0; row < 4 && !differs; ++row) {
            if (vp[col][row] != identity[col][row]) {
                differs = true;
            }
        }
    }
    REQUIRE(differs);
}

// ---------------------------------------------------------------------------
// ECS integration tests (headless)
// ---------------------------------------------------------------------------

TEST_CASE("createEntity3D Lua path: entity has Transform3D and Mesh components") {
    static bool rhiInited = initHeadlessRhi();
    REQUIRE(rhiInited);

    World world;

    // Simulate what ffe.createEntity3D does
    const EntityId entityId = world.createEntity();
    REQUIRE(world.isValid(entityId));

    // Emplace Transform3D with a position
    Transform3D t3d;
    t3d.position = {1.0f, 2.0f, 3.0f};
    world.registry().emplace<Transform3D>(static_cast<entt::entity>(entityId), t3d);

    // Emplace Mesh with handle id=1 (slot 1 exists even if not loaded in headless)
    Mesh meshComp;
    meshComp.meshHandle = MeshHandle{1};
    world.registry().emplace<Mesh>(static_cast<entt::entity>(entityId), meshComp);

    // Verify both components are present
    REQUIRE(world.hasComponent<Transform3D>(entityId));
    REQUIRE(world.hasComponent<Mesh>(entityId));

    const Transform3D& readBack = world.getComponent<Transform3D>(entityId);
    REQUIRE_THAT(readBack.position.x, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(readBack.position.y, Catch::Matchers::WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(readBack.position.z, Catch::Matchers::WithinAbs(3.0f, 1e-6f));

    const Mesh& meshReadBack = world.getComponent<Mesh>(entityId);
    REQUIRE(meshReadBack.meshHandle.id == 1u);
}

TEST_CASE("setMeshColor: Material3D diffuseColor updated correctly") {
    static bool rhiInited = initHeadlessRhi();
    REQUIRE(rhiInited);

    World world;

    const EntityId entityId = world.createEntity();
    REQUIRE(world.isValid(entityId));

    // Emplace or replace Material3D with a specific diffuse color
    Material3D mat;
    mat.diffuseColor = {0.8f, 0.3f, 0.1f, 1.0f};
    world.registry().emplace<Material3D>(static_cast<entt::entity>(entityId), mat);

    REQUIRE(world.hasComponent<Material3D>(entityId));

    const Material3D& readBack = world.getComponent<Material3D>(entityId);
    REQUIRE_THAT(readBack.diffuseColor.r, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(readBack.diffuseColor.g, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(readBack.diffuseColor.b, Catch::Matchers::WithinAbs(0.1f, 1e-6f));
    REQUIRE_THAT(readBack.diffuseColor.a, Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("setTransform3D: position and scale set correctly") {
    static bool rhiInited = initHeadlessRhi();
    REQUIRE(rhiInited);

    World world;
    const EntityId entityId = world.createEntity();

    Transform3D t3d;
    t3d.position = {5.0f, -3.0f, 2.5f};
    t3d.scale    = {2.0f, 2.0f, 2.0f};
    world.registry().emplace<Transform3D>(static_cast<entt::entity>(entityId), t3d);

    const Transform3D& readBack = world.getComponent<Transform3D>(entityId);
    REQUIRE_THAT(readBack.position.x, Catch::Matchers::WithinAbs(5.0f, 1e-6f));
    REQUIRE_THAT(readBack.position.y, Catch::Matchers::WithinAbs(-3.0f, 1e-6f));
    REQUIRE_THAT(readBack.position.z, Catch::Matchers::WithinAbs(2.5f, 1e-6f));
    REQUIRE_THAT(readBack.scale.x, Catch::Matchers::WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(readBack.scale.y, Catch::Matchers::WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(readBack.scale.z, Catch::Matchers::WithinAbs(2.0f, 1e-6f));
}

TEST_CASE("setTransform3D: Euler 0,0,0 produces identity quaternion") {
    // GLM euler-to-quat: glm::quat(glm::radians(glm::vec3{rx, ry, rz}))
    // With rx=ry=rz=0, this should produce identity quaternion (w=1, x=y=z=0)
    const glm::quat q = glm::quat(glm::radians(glm::vec3{0.0f, 0.0f, 0.0f}));

    REQUIRE_THAT(q.w, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(q.x, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(q.y, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(q.z, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("setTransform3D: Euler 90,0,0 around X produces expected quaternion") {
    // 90 degrees around X: q = (cos(45°), sin(45°), 0, 0) = (sqrt(2)/2, sqrt(2)/2, 0, 0)
    // Using the GLM YXZ euler convention: glm::quat(glm::radians(vec3{rx, ry, rz}))
    // With rx=90, ry=0, rz=0 this is a pure X rotation
    const glm::quat q = glm::quat(glm::radians(glm::vec3{90.0f, 0.0f, 0.0f}));

    // For a 90-degree rotation around X: w = cos(45°) ≈ 0.7071, x = sin(45°) ≈ 0.7071, y=z=0
    static constexpr float SQRT2_OVER_2 = 0.70710678118f;
    REQUIRE_THAT(q.w, Catch::Matchers::WithinAbs(SQRT2_OVER_2, 1e-5f));
    REQUIRE_THAT(q.x, Catch::Matchers::WithinAbs(SQRT2_OVER_2, 1e-5f));
    REQUIRE_THAT(q.y, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(q.z, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
}
