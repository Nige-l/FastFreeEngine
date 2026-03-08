add_executable(ffe_net_demo main.cpp)
target_link_libraries(ffe_net_demo PRIVATE ffe_engine)

ffe_copy_example_lua(TARGET ffe_net_demo FILES net_arena.lua)
