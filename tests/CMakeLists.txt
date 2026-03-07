add_executable(ffe_tests
    test_main.cpp
    core/test_types.cpp
    core/test_arena_allocator.cpp
    core/test_logging.cpp
    core/test_application.cpp
    core/test_ecs.cpp
    core/test_renderer_headless.cpp
    core/test_input.cpp
    core/test_gamepad.cpp
    core/test_particles.cpp
    physics/test_narrow_phase.cpp
    renderer/test_ttf_font.cpp
    renderer/test_mesh_loader.cpp
    renderer/test_screenshot.cpp
    renderer/test_shadow_map.cpp
    renderer/test_point_lights_materials.cpp
    renderer/test_skybox.cpp
    renderer/test_skeleton.cpp
)

target_link_libraries(ffe_tests
    PRIVATE
        ffe_core
        ffe_renderer
        ffe_audio
        ffe_physics
        Catch2::Catch2WithMain
)

target_compile_definitions(ffe_tests PRIVATE FFE_TEST)

include(Catch)
catch_discover_tests(ffe_tests)

# --- Lua sandbox tests ---
# Separate executable to isolate Lua state and avoid linking scripting deps into ffe_tests.
add_executable(ffe_tests_scripting
    test_main.cpp
    scripting/test_lua_sandbox.cpp
    scripting/test_save_load.cpp
    scripting/test_camera3d_bindings.cpp
    scripting/test_point_light_material_bindings.cpp
    scripting/test_skybox_bindings.cpp
    scripting/test_animation3d_bindings.cpp
    scripting/test_audio3d_bindings.cpp
    scripting/test_physics3d_bindings.cpp
    scripting/test_physics3d_collision_bindings.cpp
)

target_link_libraries(ffe_tests_scripting
    PRIVATE
        ffe_scripting
        Catch2::Catch2WithMain
)

catch_discover_tests(ffe_tests_scripting)

# --- 3D Physics tests ---
# Separate executable because Jolt Physics init/shutdown mutate global state
# (Factory, RegisterTypes, PhysicsSystem). Isolation prevents cross-contamination
# with ffe_tests which may not have Jolt initialized.
add_executable(ffe_tests_physics
    test_main.cpp
    physics/test_physics3d.cpp
    physics/test_physics3d_collisions.cpp
    physics/test_physics3d_raycast.cpp
)

target_link_libraries(ffe_tests_physics
    PRIVATE
        ffe_physics
        Catch2::Catch2WithMain
)

catch_discover_tests(ffe_tests_physics)

# --- Texture loader tests ---
# Separate executable because texture_loader tests mutate global static state
# (setAssetRoot write-once) and the tests depend on declaration order within
# the translation unit. Isolation prevents cross-contamination with ffe_tests.
add_executable(ffe_tests_texture
    test_main.cpp
    renderer/test_texture_loader.cpp
)

target_link_libraries(ffe_tests_texture
    PRIVATE
        ffe_renderer
        Catch2::Catch2WithMain
)

catch_discover_tests(ffe_tests_texture)

# --- Audio tests ---
# Separate executable because audio tests mutate global static state
# (init/shutdown write-once semantics) and use Catch2 SECTION isolation.
# Isolation prevents cross-contamination with ffe_tests and other test suites.
add_executable(ffe_tests_audio
    test_main.cpp
    audio/test_audio.cpp
    audio/test_audio_3d.cpp
)

target_link_libraries(ffe_tests_audio
    PRIVATE
        ffe_audio
        Catch2::Catch2WithMain
)

catch_discover_tests(ffe_tests_audio)
