add_executable(ffe_3d_feature_test main.cpp)
target_link_libraries(ffe_3d_feature_test PRIVATE ffe_engine)

ffe_copy_example_lua(TARGET ffe_3d_feature_test FILES game.lua)
