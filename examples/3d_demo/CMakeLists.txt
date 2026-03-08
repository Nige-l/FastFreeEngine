add_executable(ffe_3d_demo main.cpp)
target_link_libraries(ffe_3d_demo PRIVATE ffe_engine)

ffe_copy_example_lua(TARGET ffe_3d_demo FILES game.lua)
