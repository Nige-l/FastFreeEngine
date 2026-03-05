# Minimal glad GL 3.3 core loader - embedded in project
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)
# Suppress pedantic warnings for void* -> function pointer casts
# This is standard practice for all GL loaders and unavoidable in C.
target_compile_options(glad PRIVATE -Wno-pedantic)
