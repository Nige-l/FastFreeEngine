add_executable(ffe_tests
    test_main.cpp
    core/test_types.cpp
    core/test_arena_allocator.cpp
    core/test_logging.cpp
    core/test_application.cpp
    core/test_ecs.cpp
    core/test_renderer_headless.cpp
    core/test_input.cpp
)

target_link_libraries(ffe_tests
    PRIVATE
        ffe_core
        ffe_renderer
        Catch2::Catch2WithMain
)

target_compile_definitions(ffe_tests PRIVATE FFE_TEST)

include(Catch)
catch_discover_tests(ffe_tests)
