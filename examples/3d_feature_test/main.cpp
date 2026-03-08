// FFE -- 3D Feature Test Demo
//
// Comprehensive visual test that exercises ALL 3D engine features in a single
// scene.  All game logic lives in game.lua.  The C++ host is identical in
// structure to examples/3d_demo/main.cpp.
//
// Features exercised:
//   - Mesh loading (cube, damaged_helmet, fox, cesium_man, duck)
//   - Per-instance color (setMeshColor), specular (setMeshSpecular)
//   - Diffuse textures (setMeshTexture)
//   - Point lights (addPointLight, setPointLightPosition)
//   - Shadow mapping (enableShadows, setShadowBias, setShadowArea)
//   - Skeletal animation (playAnimation3D, crossfadeAnimation3D)
//   - PBR materials (damaged_helmet auto-detects)
//   - Fog (setFog)
//   - SSAO (enableSSAO)
//   - Post-processing: bloom, ACES tone mapping, FXAA
//   - Physics (createPhysicsBody, onCollision3D)
//   - Orbit camera with manual controls
//   - HUD overlay with feature toggles
//
// Window: 1280 x 720.

#include "core/application.h"
#include "core/ecs.h"
#include "core/logging.h"
#include "audio/audio.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/texture_loader.h"
#include "scripting/script_engine.h"
#include "../demo_paths.h"

namespace {

struct FeatureTestContext {
    ffe::EntityId      hostEntity   = ffe::NULL_ENTITY;
    ffe::ScriptEngine* scripts      = nullptr;
    bool               sceneReady   = false;
    bool               startupDone  = false;
};

void featureTestSystem(ffe::World& world, const float dt)
{
    auto* ctx = world.registry().ctx().find<FeatureTestContext>();
    if (ctx == nullptr) { return; }

    if (!ctx->sceneReady) {
        static char assetRootBuf[512];
        if (!demoAssetRoot(assetRootBuf, sizeof(assetRootBuf))) {
            FFE_LOG_ERROR("3DFeatureTest", "Failed to resolve asset root");
            return;
        }
        ffe::renderer::setAssetRoot(assetRootBuf);

        ctx->hostEntity = world.createEntity();
        auto& tf        = world.addComponent<ffe::Transform>(ctx->hostEntity);
        tf.position     = {0.0f, 0.0f, 0.0f};

        ctx->sceneReady = true;
        FFE_LOG_INFO("3DFeatureTest", "Host entity created (id=%u)", ctx->hostEntity);
    }

    if (!ctx->startupDone && ctx->scripts != nullptr &&
        ctx->scripts->isInitialised() && ctx->hostEntity != ffe::NULL_ENTITY)
    {
        ctx->scripts->setWorld(&world);

        static char scriptRootBuf[512];
        if (!demoScriptRoot("3d_feature_test", scriptRootBuf, sizeof(scriptRootBuf))) {
            FFE_LOG_ERROR("3DFeatureTest", "Failed to resolve script root");
            return;
        }
        const char* SCRIPT_ROOT = scriptRootBuf;
        ctx->scripts->setScriptRoot(SCRIPT_ROOT);

        static char saveRootBuf[512];
        if (demoProjectRoot(saveRootBuf, sizeof(saveRootBuf))) {
            ctx->scripts->setSaveRoot(saveRootBuf);
        }

        // Pass command line args so Lua can read them (e.g. for headless mode)
        const bool ok = ctx->scripts->doFile("game.lua", SCRIPT_ROOT);
        if (!ok) {
            FFE_LOG_ERROR("3DFeatureTest", "game.lua failed to load");
        } else {
            FFE_LOG_INFO("3DFeatureTest", "game.lua loaded; feature test active");
        }

        ctx->startupDone = true;
    }

    if (ctx->startupDone && ctx->hostEntity != ffe::NULL_ENTITY &&
        ctx->scripts != nullptr && ctx->scripts->isInitialised())
    {
        ctx->scripts->callFunction("update",
                                   static_cast<ffe::i64>(ctx->hostEntity),
                                   static_cast<double>(dt));

        ctx->scripts->tickTimers(dt);
    }
}

} // anonymous namespace

int main()
{
    ffe::ApplicationConfig config;
    config.windowTitle  = "FFE 3D Feature Test";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.headless     = false;

    ffe::Application app(config);

    if (!ffe::audio::init(false)) {
        FFE_LOG_WARN("3DFeatureTest", "Audio init failed -- audio disabled");
    }

    FeatureTestContext demoCtx;
    app.world().registry().ctx().emplace<FeatureTestContext>(demoCtx);

    static ffe::ScriptEngine scriptEngine;
    if (!scriptEngine.init()) {
        FFE_LOG_ERROR("3DFeatureTest", "ScriptEngine init failed");
    } else {
        app.world().registry().ctx().get<FeatureTestContext>().scripts = &scriptEngine;
    }

    app.world().registerSystem(FFE_SYSTEM(
        "3DFeatureTest",
        featureTestSystem,
        100
    ));
    app.world().sortSystems();

    const int32_t result = app.run();

    ffe::audio::shutdown();
    return result;
}
