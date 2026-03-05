/*
    glad - GL Loader Generator
    Minimal GL 3.3 Core Profile loader for FastFreeEngine.
*/

#include <glad/glad.h>
#include <string.h>
#include <stdlib.h>

int GLVersion_major = 0;
int GLVersion_minor = 0;

/* Function pointer definitions */
PFNGLCLEARCOLORPROC              glad_glClearColor = NULL;
PFNGLCLEARPROC                   glad_glClear = NULL;
PFNGLVIEWPORTPROC                glad_glViewport = NULL;
PFNGLSCISSORPROC                 glad_glScissor = NULL;
PFNGLENABLEPROC                  glad_glEnable = NULL;
PFNGLDISABLEPROC                 glad_glDisable = NULL;
PFNGLBLENDFUNCPROC               glad_glBlendFunc = NULL;
PFNGLDEPTHFUNCPROC               glad_glDepthFunc = NULL;
PFNGLDEPTHMASKPROC               glad_glDepthMask = NULL;
PFNGLCULLFACEPROC                glad_glCullFace = NULL;
PFNGLPOLYGONMODEPROC             glad_glPolygonMode = NULL;
PFNGLGETERRORPROC                glad_glGetError = NULL;
PFNGLGETSTRINGPROC               glad_glGetString = NULL;
PFNGLGETINTEGERVPROC             glad_glGetIntegerv = NULL;
PFNGLPIXELSTOREIPROC             glad_glPixelStorei = NULL;

PFNGLGENBUFFERSPROC              glad_glGenBuffers = NULL;
PFNGLDELETEBUFFERSPROC           glad_glDeleteBuffers = NULL;
PFNGLBINDBUFFERPROC              glad_glBindBuffer = NULL;
PFNGLBUFFERDATAPROC              glad_glBufferData = NULL;
PFNGLBUFFERSUBDATAPROC           glad_glBufferSubData = NULL;

PFNGLGENVERTEXARRAYSPROC         glad_glGenVertexArrays = NULL;
PFNGLDELETEVERTEXARRAYSPROC      glad_glDeleteVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC         glad_glBindVertexArray = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray = NULL;
PFNGLVERTEXATTRIBPOINTERPROC     glad_glVertexAttribPointer = NULL;

PFNGLGENTEXTURESPROC             glad_glGenTextures = NULL;
PFNGLDELETETEXTURESPROC          glad_glDeleteTextures = NULL;
PFNGLBINDTEXTUREPROC             glad_glBindTexture = NULL;
PFNGLACTIVETEXTUREPROC           glad_glActiveTexture = NULL;
PFNGLTEXIMAGE2DPROC              glad_glTexImage2D = NULL;
PFNGLTEXPARAMETERIPROC           glad_glTexParameteri = NULL;
PFNGLGENERATEMIPMAPPROC          glad_glGenerateMipmap = NULL;

PFNGLCREATESHADERPROC            glad_glCreateShader = NULL;
PFNGLDELETESHADERPROC            glad_glDeleteShader = NULL;
PFNGLSHADERSOURCEPROC            glad_glShaderSource = NULL;
PFNGLCOMPILESHADERPROC           glad_glCompileShader = NULL;
PFNGLGETSHADERIVPROC             glad_glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC        glad_glGetShaderInfoLog = NULL;

PFNGLCREATEPROGRAMPROC           glad_glCreateProgram = NULL;
PFNGLDELETEPROGRAMPROC           glad_glDeleteProgram = NULL;
PFNGLATTACHSHADERPROC            glad_glAttachShader = NULL;
PFNGLLINKPROGRAMPROC             glad_glLinkProgram = NULL;
PFNGLUSEPROGRAMPROC              glad_glUseProgram = NULL;
PFNGLGETPROGRAMIVPROC            glad_glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC       glad_glGetProgramInfoLog = NULL;
PFNGLGETUNIFORMLOCATIONPROC      glad_glGetUniformLocation = NULL;

PFNGLUNIFORM1IPROC               glad_glUniform1i = NULL;
PFNGLUNIFORM1FPROC               glad_glUniform1f = NULL;
PFNGLUNIFORM2FVPROC              glad_glUniform2fv = NULL;
PFNGLUNIFORM3FVPROC              glad_glUniform3fv = NULL;
PFNGLUNIFORM4FVPROC              glad_glUniform4fv = NULL;
PFNGLUNIFORMMATRIX3FVPROC        glad_glUniformMatrix3fv = NULL;
PFNGLUNIFORMMATRIX4FVPROC        glad_glUniformMatrix4fv = NULL;

PFNGLDRAWARRAYSPROC              glad_glDrawArrays = NULL;
PFNGLDRAWELEMENTSPROC            glad_glDrawElements = NULL;

PFNGLDEBUGMESSAGECALLBACKPROC    glad_glDebugMessageCallback = NULL;
PFNGLDEBUGMESSAGECONTROLPROC     glad_glDebugMessageControl = NULL;

static void glad_parse_version(void) {
    const char* version;
    int major = 0, minor = 0;

    version = (const char*)glad_glGetString(GL_VERSION);
    if (version == NULL) {
        GLVersion_major = 0;
        GLVersion_minor = 0;
        return;
    }

    /* Parse "major.minor" from version string */
    for (const char* p = version; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            major = *p - '0';
            if (*(p + 1) == '.') {
                minor = *(p + 2) - '0';
            }
            break;
        }
    }

    GLVersion_major = major;
    GLVersion_minor = minor;
}

int gladLoadGLLoader(GLADloadproc load) {
    if (load == NULL) return 0;

    /* Load fundamental functions first */
    glad_glGetString = (PFNGLGETSTRINGPROC)load("glGetString");
    if (glad_glGetString == NULL) return 0;

    /* Parse version */
    glad_parse_version();
    if (GLVersion_major < 3 || (GLVersion_major == 3 && GLVersion_minor < 3)) {
        return 0;
    }

    /* Load all GL 3.3 core functions */
    glad_glClearColor = (PFNGLCLEARCOLORPROC)load("glClearColor");
    glad_glClear = (PFNGLCLEARPROC)load("glClear");
    glad_glViewport = (PFNGLVIEWPORTPROC)load("glViewport");
    glad_glScissor = (PFNGLSCISSORPROC)load("glScissor");
    glad_glEnable = (PFNGLENABLEPROC)load("glEnable");
    glad_glDisable = (PFNGLDISABLEPROC)load("glDisable");
    glad_glBlendFunc = (PFNGLBLENDFUNCPROC)load("glBlendFunc");
    glad_glDepthFunc = (PFNGLDEPTHFUNCPROC)load("glDepthFunc");
    glad_glDepthMask = (PFNGLDEPTHMASKPROC)load("glDepthMask");
    glad_glCullFace = (PFNGLCULLFACEPROC)load("glCullFace");
    glad_glPolygonMode = (PFNGLPOLYGONMODEPROC)load("glPolygonMode");
    glad_glGetError = (PFNGLGETERRORPROC)load("glGetError");
    glad_glGetIntegerv = (PFNGLGETINTEGERVPROC)load("glGetIntegerv");
    glad_glPixelStorei = (PFNGLPIXELSTOREIPROC)load("glPixelStorei");

    glad_glGenBuffers = (PFNGLGENBUFFERSPROC)load("glGenBuffers");
    glad_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)load("glDeleteBuffers");
    glad_glBindBuffer = (PFNGLBINDBUFFERPROC)load("glBindBuffer");
    glad_glBufferData = (PFNGLBUFFERDATAPROC)load("glBufferData");
    glad_glBufferSubData = (PFNGLBUFFERSUBDATAPROC)load("glBufferSubData");

    glad_glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)load("glGenVertexArrays");
    glad_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)load("glDeleteVertexArrays");
    glad_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)load("glBindVertexArray");
    glad_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)load("glEnableVertexAttribArray");
    glad_glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)load("glDisableVertexAttribArray");
    glad_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)load("glVertexAttribPointer");

    glad_glGenTextures = (PFNGLGENTEXTURESPROC)load("glGenTextures");
    glad_glDeleteTextures = (PFNGLDELETETEXTURESPROC)load("glDeleteTextures");
    glad_glBindTexture = (PFNGLBINDTEXTUREPROC)load("glBindTexture");
    glad_glActiveTexture = (PFNGLACTIVETEXTUREPROC)load("glActiveTexture");
    glad_glTexImage2D = (PFNGLTEXIMAGE2DPROC)load("glTexImage2D");
    glad_glTexParameteri = (PFNGLTEXPARAMETERIPROC)load("glTexParameteri");
    glad_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)load("glGenerateMipmap");

    glad_glCreateShader = (PFNGLCREATESHADERPROC)load("glCreateShader");
    glad_glDeleteShader = (PFNGLDELETESHADERPROC)load("glDeleteShader");
    glad_glShaderSource = (PFNGLSHADERSOURCEPROC)load("glShaderSource");
    glad_glCompileShader = (PFNGLCOMPILESHADERPROC)load("glCompileShader");
    glad_glGetShaderiv = (PFNGLGETSHADERIVPROC)load("glGetShaderiv");
    glad_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)load("glGetShaderInfoLog");

    glad_glCreateProgram = (PFNGLCREATEPROGRAMPROC)load("glCreateProgram");
    glad_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)load("glDeleteProgram");
    glad_glAttachShader = (PFNGLATTACHSHADERPROC)load("glAttachShader");
    glad_glLinkProgram = (PFNGLLINKPROGRAMPROC)load("glLinkProgram");
    glad_glUseProgram = (PFNGLUSEPROGRAMPROC)load("glUseProgram");
    glad_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)load("glGetProgramiv");
    glad_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)load("glGetProgramInfoLog");
    glad_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)load("glGetUniformLocation");

    glad_glUniform1i = (PFNGLUNIFORM1IPROC)load("glUniform1i");
    glad_glUniform1f = (PFNGLUNIFORM1FPROC)load("glUniform1f");
    glad_glUniform2fv = (PFNGLUNIFORM2FVPROC)load("glUniform2fv");
    glad_glUniform3fv = (PFNGLUNIFORM3FVPROC)load("glUniform3fv");
    glad_glUniform4fv = (PFNGLUNIFORM4FVPROC)load("glUniform4fv");
    glad_glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC)load("glUniformMatrix3fv");
    glad_glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)load("glUniformMatrix4fv");

    glad_glDrawArrays = (PFNGLDRAWARRAYSPROC)load("glDrawArrays");
    glad_glDrawElements = (PFNGLDRAWELEMENTSPROC)load("glDrawElements");

    /* Optional: debug output (may not be available on GL 3.3, that's OK) */
    glad_glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)load("glDebugMessageCallback");
    glad_glDebugMessageControl = (PFNGLDEBUGMESSAGECONTROLPROC)load("glDebugMessageControl");

    return 1;
}
