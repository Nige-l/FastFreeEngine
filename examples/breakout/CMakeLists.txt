add_executable(ffe_breakout main.cpp)
target_link_libraries(ffe_breakout PRIVATE ffe_engine)

ffe_copy_example_lua(TARGET ffe_breakout FILES breakout.lua)
