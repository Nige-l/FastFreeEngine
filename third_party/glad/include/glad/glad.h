/*
    glad - GL Loader Generator
    Generated for OpenGL 3.3 Core Profile
    Minimal loader for FastFreeEngine LEGACY tier.

    Based on the glad project (https://github.com/Dav1dde/glad)
    License: MIT / Public Domain
*/

#ifndef GLAD_H_
#define GLAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <KHR/khrplatform.h>

/* Calling convention */
#ifndef GLAD_API_PTR
#define GLAD_API_PTR
#endif

/* Type definitions */
typedef void GLvoid;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef double GLdouble;
typedef unsigned int GLuint;
typedef unsigned char GLboolean;
typedef khronos_uint8_t GLubyte;
typedef khronos_int8_t GLbyte;
typedef khronos_int16_t GLshort;
typedef khronos_uint16_t GLushort;
typedef khronos_ssize_t GLsizeiptr;
typedef khronos_intptr_t GLintptr;
typedef char GLchar;
typedef khronos_float_t GLclampf;
typedef double GLclampd;

/* GL boolean values */
#define GL_FALSE 0
#define GL_TRUE  1

/* GL types for handles */
typedef void (*GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,
                            GLsizei length, const GLchar* message, const void* userParam);
typedef void (*GLDEBUGPROCARB)(GLenum source, GLenum type, GLuint id, GLenum severity,
                               GLsizei length, const GLchar* message, const void* userParam);

/* Function pointer type for glad loading */
typedef void* (*GLADloadproc)(const char* name);

/* Version info set after gladLoadGLLoader succeeds */
extern int GLVersion_major;
extern int GLVersion_minor;

/* The glad loader. Returns non-zero on success. */
int gladLoadGLLoader(GLADloadproc load);

/* ==================== GL Constants ==================== */

/* Errors */
#define GL_NO_ERROR                       0
#define GL_INVALID_ENUM                   0x0500
#define GL_INVALID_VALUE                  0x0501
#define GL_INVALID_OPERATION              0x0502
#define GL_OUT_OF_MEMORY                  0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION  0x0506

/* Data types */
#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406

/* Primitives */
#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006

/* Buffer objects */
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_UNIFORM_BUFFER                 0x8A11
#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_STREAM_DRAW                    0x88E0

/* Framebuffer */
#define GL_FRAMEBUFFER                    0x8D40
#define GL_RENDERBUFFER                   0x8D41
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_STENCIL_ATTACHMENT             0x8D20
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5

/* Texture */
#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_LINEAR           0x2703
#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_MIRRORED_REPEAT                0x8370

/* Texture formats */
#define GL_RED                            0x1903
#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_R8                             0x8229
#define GL_RGB8                           0x8051
#define GL_RGBA8                          0x8058
#define GL_RGBA16F                        0x881A

/* Pixel store */
#define GL_UNPACK_ALIGNMENT               0x0CF5

/* Blending */
#define GL_BLEND                          0x0BE2
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_ONE                            1
#define GL_ZERO                           0

/* Depth */
#define GL_DEPTH_TEST                     0x0B71
#define GL_LESS                           0x0201
#define GL_LEQUAL                         0x0203
#define GL_ALWAYS                         0x0207
#define GL_DEPTH_BUFFER_BIT               0x00000100

/* Culling */
#define GL_CULL_FACE                      0x0B44
#define GL_BACK                           0x0405
#define GL_FRONT                          0x0404
#define GL_FRONT_AND_BACK                 0x0408

/* Polygon mode */
#define GL_LINE                           0x1B01
#define GL_FILL                           0x1B02

/* Clear */
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_STENCIL_BUFFER_BIT             0x00000400

/* Shader */
#define GL_VERTEX_SHADER                  0x8B31
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_ACTIVE_UNIFORMS                0x8B86

/* String queries */
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C
#define GL_EXTENSIONS                     0x1F03
#define GL_MAX_TEXTURE_SIZE               0x0D33

/* Scissor */
#define GL_SCISSOR_TEST                   0x0C11

/* Debug output (GL_KHR_debug / GL 4.3, but we try to load it as extension) */
#define GL_DEBUG_OUTPUT                   0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242
#define GL_DEBUG_SOURCE_API               0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM     0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER   0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY       0x8249
#define GL_DEBUG_SOURCE_APPLICATION       0x824A
#define GL_DEBUG_SOURCE_OTHER             0x824B
#define GL_DEBUG_TYPE_ERROR               0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR  0x824E
#define GL_DEBUG_TYPE_PORTABILITY         0x824F
#define GL_DEBUG_TYPE_PERFORMANCE         0x8250
#define GL_DEBUG_TYPE_OTHER               0x8251
#define GL_DEBUG_SEVERITY_HIGH            0x9146
#define GL_DEBUG_SEVERITY_MEDIUM          0x9147
#define GL_DEBUG_SEVERITY_LOW             0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B

/* Half float */
#define GL_HALF_FLOAT                     0x140B

/* ==================== GL Function Declarations ==================== */

/* These are function pointers loaded by gladLoadGLLoader */

/* Core profile functions */
typedef void (GLAD_API_PTR *PFNGLCLEARCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (GLAD_API_PTR *PFNGLCLEARPROC)(GLbitfield);
typedef void (GLAD_API_PTR *PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (GLAD_API_PTR *PFNGLSCISSORPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (GLAD_API_PTR *PFNGLENABLEPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLDISABLEPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLBLENDFUNCPROC)(GLenum, GLenum);
typedef void (GLAD_API_PTR *PFNGLDEPTHFUNCPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLDEPTHMASKPROC)(GLboolean);
typedef void (GLAD_API_PTR *PFNGLCULLFACEPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLPOLYGONMODEPROC)(GLenum, GLenum);
typedef GLenum (GLAD_API_PTR *PFNGLGETERRORPROC)(void);
typedef const GLubyte* (GLAD_API_PTR *PFNGLGETSTRINGPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLGETINTEGERVPROC)(GLenum, GLint*);
typedef void (GLAD_API_PTR *PFNGLPIXELSTOREIPROC)(GLenum, GLint);

/* Buffer functions */
typedef void (GLAD_API_PTR *PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void (GLAD_API_PTR *PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void (GLAD_API_PTR *PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (GLAD_API_PTR *PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (GLAD_API_PTR *PFNGLBUFFERSUBDATAPROC)(GLenum, GLintptr, GLsizeiptr, const void*);

/* Vertex array functions */
typedef void (GLAD_API_PTR *PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void (GLAD_API_PTR *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint*);
typedef void (GLAD_API_PTR *PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);

/* Texture functions */
typedef void (GLAD_API_PTR *PFNGLGENTEXTURESPROC)(GLsizei, GLuint*);
typedef void (GLAD_API_PTR *PFNGLDELETETEXTURESPROC)(GLsizei, const GLuint*);
typedef void (GLAD_API_PTR *PFNGLBINDTEXTUREPROC)(GLenum, GLuint);
typedef void (GLAD_API_PTR *PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLTEXIMAGE2DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (GLAD_API_PTR *PFNGLTEXPARAMETERIPROC)(GLenum, GLenum, GLint);
typedef void (GLAD_API_PTR *PFNGLGENERATEMIPMAPPROC)(GLenum);

/* Shader functions */
typedef GLuint (GLAD_API_PTR *PFNGLCREATESHADERPROC)(GLenum);
typedef void (GLAD_API_PTR *PFNGLDELETESHADERPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (GLAD_API_PTR *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void (GLAD_API_PTR *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);

/* Program functions */
typedef GLuint (GLAD_API_PTR *PFNGLCREATEPROGRAMPROC)(void);
typedef void (GLAD_API_PTR *PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (GLAD_API_PTR *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (GLAD_API_PTR *PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void (GLAD_API_PTR *PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLint (GLAD_API_PTR *PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);

/* Uniform functions */
typedef void (GLAD_API_PTR *PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (GLAD_API_PTR *PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void (GLAD_API_PTR *PFNGLUNIFORM2FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void (GLAD_API_PTR *PFNGLUNIFORM3FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void (GLAD_API_PTR *PFNGLUNIFORM4FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void (GLAD_API_PTR *PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (GLAD_API_PTR *PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);

/* Draw functions */
typedef void (GLAD_API_PTR *PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
typedef void (GLAD_API_PTR *PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const void*);

/* Debug output (extension, may not be available on GL 3.3) */
typedef void (GLAD_API_PTR *PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC, const void*);
typedef void (GLAD_API_PTR *PFNGLDEBUGMESSAGECONTROLPROC)(GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean);

/* ==================== GL Function Pointer Extern Declarations ==================== */

extern PFNGLCLEARCOLORPROC              glad_glClearColor;
extern PFNGLCLEARPROC                   glad_glClear;
extern PFNGLVIEWPORTPROC                glad_glViewport;
extern PFNGLSCISSORPROC                 glad_glScissor;
extern PFNGLENABLEPROC                  glad_glEnable;
extern PFNGLDISABLEPROC                 glad_glDisable;
extern PFNGLBLENDFUNCPROC               glad_glBlendFunc;
extern PFNGLDEPTHFUNCPROC               glad_glDepthFunc;
extern PFNGLDEPTHMASKPROC               glad_glDepthMask;
extern PFNGLCULLFACEPROC                glad_glCullFace;
extern PFNGLPOLYGONMODEPROC             glad_glPolygonMode;
extern PFNGLGETERRORPROC                glad_glGetError;
extern PFNGLGETSTRINGPROC               glad_glGetString;
extern PFNGLGETINTEGERVPROC             glad_glGetIntegerv;
extern PFNGLPIXELSTOREIPROC             glad_glPixelStorei;

extern PFNGLGENBUFFERSPROC              glad_glGenBuffers;
extern PFNGLDELETEBUFFERSPROC           glad_glDeleteBuffers;
extern PFNGLBINDBUFFERPROC              glad_glBindBuffer;
extern PFNGLBUFFERDATAPROC              glad_glBufferData;
extern PFNGLBUFFERSUBDATAPROC           glad_glBufferSubData;

extern PFNGLGENVERTEXARRAYSPROC         glad_glGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC      glad_glDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC         glad_glBindVertexArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC     glad_glVertexAttribPointer;

extern PFNGLGENTEXTURESPROC             glad_glGenTextures;
extern PFNGLDELETETEXTURESPROC          glad_glDeleteTextures;
extern PFNGLBINDTEXTUREPROC             glad_glBindTexture;
extern PFNGLACTIVETEXTUREPROC           glad_glActiveTexture;
extern PFNGLTEXIMAGE2DPROC              glad_glTexImage2D;
extern PFNGLTEXPARAMETERIPROC           glad_glTexParameteri;
extern PFNGLGENERATEMIPMAPPROC          glad_glGenerateMipmap;

extern PFNGLCREATESHADERPROC            glad_glCreateShader;
extern PFNGLDELETESHADERPROC            glad_glDeleteShader;
extern PFNGLSHADERSOURCEPROC            glad_glShaderSource;
extern PFNGLCOMPILESHADERPROC           glad_glCompileShader;
extern PFNGLGETSHADERIVPROC             glad_glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC        glad_glGetShaderInfoLog;

extern PFNGLCREATEPROGRAMPROC           glad_glCreateProgram;
extern PFNGLDELETEPROGRAMPROC           glad_glDeleteProgram;
extern PFNGLATTACHSHADERPROC            glad_glAttachShader;
extern PFNGLLINKPROGRAMPROC             glad_glLinkProgram;
extern PFNGLUSEPROGRAMPROC              glad_glUseProgram;
extern PFNGLGETPROGRAMIVPROC            glad_glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC       glad_glGetProgramInfoLog;
extern PFNGLGETUNIFORMLOCATIONPROC      glad_glGetUniformLocation;

extern PFNGLUNIFORM1IPROC              glad_glUniform1i;
extern PFNGLUNIFORM1FPROC              glad_glUniform1f;
extern PFNGLUNIFORM2FVPROC             glad_glUniform2fv;
extern PFNGLUNIFORM3FVPROC             glad_glUniform3fv;
extern PFNGLUNIFORM4FVPROC             glad_glUniform4fv;
extern PFNGLUNIFORMMATRIX3FVPROC       glad_glUniformMatrix3fv;
extern PFNGLUNIFORMMATRIX4FVPROC       glad_glUniformMatrix4fv;

extern PFNGLDRAWARRAYSPROC              glad_glDrawArrays;
extern PFNGLDRAWELEMENTSPROC            glad_glDrawElements;

extern PFNGLDEBUGMESSAGECALLBACKPROC    glad_glDebugMessageCallback;
extern PFNGLDEBUGMESSAGECONTROLPROC     glad_glDebugMessageControl;

/* ==================== GL Function Macros ==================== */

#define glClearColor              glad_glClearColor
#define glClear                   glad_glClear
#define glViewport                glad_glViewport
#define glScissor                 glad_glScissor
#define glEnable                  glad_glEnable
#define glDisable                 glad_glDisable
#define glBlendFunc               glad_glBlendFunc
#define glDepthFunc               glad_glDepthFunc
#define glDepthMask               glad_glDepthMask
#define glCullFace                glad_glCullFace
#define glPolygonMode             glad_glPolygonMode
#define glGetError                glad_glGetError
#define glGetString               glad_glGetString
#define glGetIntegerv             glad_glGetIntegerv
#define glPixelStorei             glad_glPixelStorei

#define glGenBuffers              glad_glGenBuffers
#define glDeleteBuffers           glad_glDeleteBuffers
#define glBindBuffer              glad_glBindBuffer
#define glBufferData              glad_glBufferData
#define glBufferSubData           glad_glBufferSubData

#define glGenVertexArrays         glad_glGenVertexArrays
#define glDeleteVertexArrays      glad_glDeleteVertexArrays
#define glBindVertexArray         glad_glBindVertexArray
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glDisableVertexAttribArray glad_glDisableVertexAttribArray
#define glVertexAttribPointer     glad_glVertexAttribPointer

#define glGenTextures             glad_glGenTextures
#define glDeleteTextures          glad_glDeleteTextures
#define glBindTexture             glad_glBindTexture
#define glActiveTexture           glad_glActiveTexture
#define glTexImage2D              glad_glTexImage2D
#define glTexParameteri           glad_glTexParameteri
#define glGenerateMipmap          glad_glGenerateMipmap

#define glCreateShader            glad_glCreateShader
#define glDeleteShader            glad_glDeleteShader
#define glShaderSource            glad_glShaderSource
#define glCompileShader           glad_glCompileShader
#define glGetShaderiv             glad_glGetShaderiv
#define glGetShaderInfoLog        glad_glGetShaderInfoLog

#define glCreateProgram           glad_glCreateProgram
#define glDeleteProgram           glad_glDeleteProgram
#define glAttachShader            glad_glAttachShader
#define glLinkProgram             glad_glLinkProgram
#define glUseProgram              glad_glUseProgram
#define glGetProgramiv            glad_glGetProgramiv
#define glGetProgramInfoLog       glad_glGetProgramInfoLog
#define glGetUniformLocation      glad_glGetUniformLocation

#define glUniform1i               glad_glUniform1i
#define glUniform1f               glad_glUniform1f
#define glUniform2fv              glad_glUniform2fv
#define glUniform3fv              glad_glUniform3fv
#define glUniform4fv              glad_glUniform4fv
#define glUniformMatrix3fv        glad_glUniformMatrix3fv
#define glUniformMatrix4fv        glad_glUniformMatrix4fv

#define glDrawArrays              glad_glDrawArrays
#define glDrawElements            glad_glDrawElements

#define glDebugMessageCallback    glad_glDebugMessageCallback
#define glDebugMessageControl     glad_glDebugMessageControl

#ifdef __cplusplus
}
#endif

#endif /* GLAD_H_ */
