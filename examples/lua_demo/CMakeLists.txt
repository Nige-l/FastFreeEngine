add_executable(ffe_lua_demo main.cpp)
target_link_libraries(ffe_lua_demo PRIVATE ffe_engine)

ffe_copy_example_lua(TARGET ffe_lua_demo FILES game.lua)
