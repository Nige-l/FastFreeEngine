# engine/physics — 2D collision detection + 3D rigid body physics (Jolt)
#
# 2D: Pure CPU. No GPU dependency. No external libraries.
#      Arena-allocated spatial hash for broad phase. Inline narrow phase.
#
# 3D: Jolt Physics backend. Pre-allocated temp allocator (10 MB).
#      Single-thread job system for LEGACY tier.

find_package(Jolt CONFIG REQUIRED)

add_library(ffe_physics STATIC
    collision_system.cpp
    physics3d.cpp
    physics3d_system.cpp
)

target_include_directories(ffe_physics
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine   # so includes are: #include "physics/collider2d.h"
)

target_link_libraries(ffe_physics
    PUBLIC
        ffe_core        # types, ECS, arena allocator, logging
        ffe_renderer    # Transform component (in render_system.h)
    PRIVATE
        Jolt::Jolt      # Jolt Physics (vcpkg)
)

target_compile_features(ffe_physics PUBLIC cxx_std_20)
