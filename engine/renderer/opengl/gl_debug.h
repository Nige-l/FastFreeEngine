#pragma once

namespace ffe::gl {

// Setup GL debug output callback if available (GL_KHR_debug / GL 4.3+).
// On GL 3.3 this may not be available — that is fine, we just skip it.
void setupDebugOutput();

// Check for GL errors and log them. Returns true if an error was found.
bool checkGlError(const char* context);

} // namespace ffe::gl
