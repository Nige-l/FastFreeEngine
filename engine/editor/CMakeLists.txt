# engine/editor/CMakeLists.txt
# Dear ImGui integration for the editor overlay (Debug builds only via FFE_EDITOR)

add_library(ffe_editor STATIC
    editor.cpp
    graph_editor_panel.cpp
    llm_panel.cpp
)

target_include_directories(ffe_editor PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/third_party
)

# Link OpenSSL for cpp-httplib HTTPS support (CPPHTTPLIB_OPENSSL_SUPPORT).
# On Linux with OpenSSL 3.x available system-wide, find_package locates it.
# On MinGW Windows without system OpenSSL, add "openssl" to vcpkg.json (OQ-2).
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND OR OPENSSL_FOUND)
    target_link_libraries(ffe_editor PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(ffe_editor PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT=1)
else()
    message(STATUS "LLMPanel: OpenSSL not found — httplib will use stub (no HTTPS). "
                   "Add openssl to vcpkg.json for MinGW/Windows builds.")
endif()

target_link_libraries(ffe_editor PUBLIC
    imgui::imgui
    ffe_core
    ffe_renderer
    ffe_audio
)

target_compile_definitions(ffe_editor PUBLIC
    $<$<CONFIG:Debug>:FFE_EDITOR>
    FFE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)
