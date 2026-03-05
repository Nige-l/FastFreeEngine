add_executable(ffe_tests
    test_main.cpp
    core/test_arena_allocator.cpp
    core/test_logging.cpp
    core/test_application.cpp
    core/test_ecs.cpp
)

target_link_libraries(ffe_tests
    PRIVATE
        ffe_core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(ffe_tests)
