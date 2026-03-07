#include "editor_app.h"
#include "core/application.h"
#include "scene/scene_serialiser.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace ffe::editor_app {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

bool EditorApp::init() {
    // 1. Initialize GLFW
    if (glfwInit() == GLFW_FALSE) {
        return false;
    }

    // 2. Create window with OpenGL 3.3 core profile (LEGACY tier)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

#ifdef __APPLE__
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    m_window = glfwCreateWindow(1280, 720, "FastFreeEngine Editor", nullptr, nullptr);
    if (m_window == nullptr) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // VSync

    // 3. Load OpenGL function pointers via glad
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
        return false;
    }

    // 4. Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 5. Initialize the engine in editor-hosted mode
    ApplicationConfig config;
    config.windowTitle  = "FastFreeEngine Editor";
    config.windowWidth  = 1280;
    config.windowHeight = 720;
    config.tier         = HardwareTier::LEGACY;
    config.headless     = false;

    m_app = std::make_unique<Application>(config);
    m_app->setWindow(m_window);

    if (!m_app->initSubsystems()) {
        return false;
    }

    // 6. Initialize the viewport panel FBO (default 800x600, resizes with panel)
    m_viewport.init(800, 600);

    return true;
}

void EditorApp::run() {
    while (glfwWindowShouldClose(m_window) == GLFW_FALSE) {
        glfwPollEvents();

        // Begin ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render editor UI
        renderMenuBar();
        renderPanels();

        // Render ImGui draw data
        ImGui::Render();

        // Clear the screen and render ImGui
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(m_window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }
}

void EditorApp::shutdown() {
    // 0. Destroy viewport FBO (needs GL context alive)
    m_viewport.shutdown();

    // 1. Shutdown ImGui backends (must happen while the GL context + window are alive)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // 2. Shutdown engine subsystems (this destroys the GLFW window and calls glfwTerminate)
    if (m_app) {
        m_app->shutdownSubsystems();
        m_app.reset();
    }

    // Window is already destroyed by Application::shutdown() — just null our pointer.
    m_window = nullptr;
}

void EditorApp::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                // TODO: clear scene
            }
            if (ImGui::MenuItem("Open Scene...")) {
                m_pendingDialogMode = ffe::editor::FileDialogMode::OPEN;
                m_fileDialog.open(ffe::editor::FileDialogMode::OPEN, ".");
            }
            if (ImGui::MenuItem("Save Scene")) {
                if (!m_currentScenePath.empty() && m_app) {
                    ffe::scene::saveScene(m_app->world(), m_currentScenePath);
                } else {
                    m_pendingDialogMode = ffe::editor::FileDialogMode::SAVE;
                    m_fileDialog.open(ffe::editor::FileDialogMode::SAVE, ".");
                }
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                m_pendingDialogMode = ffe::editor::FileDialogMode::SAVE;
                m_fileDialog.open(ffe::editor::FileDialogMode::SAVE, ".");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Viewport", nullptr, &m_showViewport);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorApp::renderPanels() {
    if (m_showViewport && m_app) {
        m_viewport.render(*m_app);
    }

    // File dialog
    if (m_fileDialog.isOpen() && m_fileDialog.render()) {
        const auto& path = m_fileDialog.selectedPath();
        if (m_app && !path.empty()) {
            if (m_pendingDialogMode == ffe::editor::FileDialogMode::OPEN) {
                m_app->world().clearAllEntities();
                if (ffe::scene::loadScene(m_app->world(), path)) {
                    m_currentScenePath = path;
                }
            } else {
                if (ffe::scene::saveScene(m_app->world(), path)) {
                    m_currentScenePath = path;
                }
            }
        }
    }
}

} // namespace ffe::editor_app
