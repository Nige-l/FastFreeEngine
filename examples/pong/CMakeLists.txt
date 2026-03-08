add_executable(ffe_pong main.cpp)
target_link_libraries(ffe_pong PRIVATE ffe_engine)

ffe_copy_example_lua(TARGET ffe_pong FILES pong.lua)
