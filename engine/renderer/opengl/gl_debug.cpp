#include "renderer/opengl/gl_debug.h"
#include "core/logging.h"

#include <glad/glad.h>

namespace ffe::gl {

static void GLAD_API_PTR debugCallback(
    [[maybe_unused]] GLenum source,
    GLenum type,
    [[maybe_unused]] GLuint id,
    GLenum severity,
    [[maybe_unused]] GLsizei length,
    const GLchar* message,
    [[maybe_unused]] const void* userParam)
{
    // Skip notifications — too noisy
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

    const char* typeStr = "OTHER";
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:               typeStr = "ERROR"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "DEPRECATED"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeStr = "UNDEFINED"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         typeStr = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         typeStr = "PERFORMANCE"; break;
        default: break;
    }

    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            FFE_LOG_ERROR("GL", "[%s] %s", typeStr, message);
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            FFE_LOG_WARN("GL", "[%s] %s", typeStr, message);
            break;
        case GL_DEBUG_SEVERITY_LOW:
            FFE_LOG_INFO("GL", "[%s] %s", typeStr, message);
            break;
        default:
            break;
    }
}

void setupDebugOutput() {
    if (glad_glDebugMessageCallback != nullptr) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, nullptr);
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR,
                              GL_DEBUG_SEVERITY_HIGH, 0, nullptr, GL_TRUE);
        FFE_LOG_INFO("GL", "Debug output enabled");
    } else {
        FFE_LOG_INFO("GL", "Debug output not available (GL 3.3 — extension not present)");
    }
}

bool checkGlError(const char* context) {
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        const char* errStr = "UNKNOWN";
        switch (err) {
            case GL_INVALID_ENUM:                  errStr = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                 errStr = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:             errStr = "INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:                 errStr = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "INVALID_FRAMEBUFFER_OPERATION"; break;
            default: break;
        }
        FFE_LOG_ERROR("GL", "Error %s at %s", errStr, context);
        return true;
    }
    return false;
}

} // namespace ffe::gl
