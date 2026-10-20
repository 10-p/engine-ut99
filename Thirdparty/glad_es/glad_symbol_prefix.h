/*=============================================================================
	glad_symbol_prefix.h: give this (GLES2) glad loader a private symbol namespace.

	NOpenGLESDrv vendors this GLES2 loader and XOpenGLDrv its own desktop-GL one; both generators
	emit the same external names for every entry point the two APIs share (`glad_glClear`,
	`GLVersion`, ...), a `multiple definition` error once both drivers sit in one static binary.
	The rename lives here because XOpenGLDrv is fetched from its own repository, and this loader
	is not compiled on the web (NOpenGLESDrv takes Emscripten's GLES2 entry points there).

	Link-time names only: glad reaches every function through its own `glClear -> glad_glClear`
	macro, so no call site changes.

	Regenerate after re-running the glad generator:
	    cc -c glad.c -I. -o /tmp/glad.o
	    nm -g --defined-only /tmp/glad.o | awk '{print $3}' | grep -v '^__x86' \
	      | sort -u | awk '{printf "#define %-46s UTGLES_%s\n", $1, $1}'
=============================================================================*/

#ifndef __glad_es_symbol_prefix_h_
#define __glad_es_symbol_prefix_h_

#define GLAD_GL_APPLE_texture_format_BGRA8888          UTGLES_GLAD_GL_APPLE_texture_format_BGRA8888
#define GLAD_GL_ES_VERSION_2_0                         UTGLES_GLAD_GL_ES_VERSION_2_0
#define GLAD_GL_EXT_depth_clamp                        UTGLES_GLAD_GL_EXT_depth_clamp
#define GLAD_GL_EXT_texture_format_BGRA8888            UTGLES_GLAD_GL_EXT_texture_format_BGRA8888
#define GLAD_GL_MESA_bgra                              UTGLES_GLAD_GL_MESA_bgra
#define GLVersion                                      UTGLES_GLVersion
#define gladLoadGLES2Loader                            UTGLES_gladLoadGLES2Loader
#define glad_glActiveTexture                           UTGLES_glad_glActiveTexture
#define glad_glAttachShader                            UTGLES_glad_glAttachShader
#define glad_glBindAttribLocation                      UTGLES_glad_glBindAttribLocation
#define glad_glBindBuffer                              UTGLES_glad_glBindBuffer
#define glad_glBindFramebuffer                         UTGLES_glad_glBindFramebuffer
#define glad_glBindRenderbuffer                        UTGLES_glad_glBindRenderbuffer
#define glad_glBindTexture                             UTGLES_glad_glBindTexture
#define glad_glBlendColor                              UTGLES_glad_glBlendColor
#define glad_glBlendEquation                           UTGLES_glad_glBlendEquation
#define glad_glBlendEquationSeparate                   UTGLES_glad_glBlendEquationSeparate
#define glad_glBlendFunc                               UTGLES_glad_glBlendFunc
#define glad_glBlendFuncSeparate                       UTGLES_glad_glBlendFuncSeparate
#define glad_glBufferData                              UTGLES_glad_glBufferData
#define glad_glBufferSubData                           UTGLES_glad_glBufferSubData
#define glad_glCheckFramebufferStatus                  UTGLES_glad_glCheckFramebufferStatus
#define glad_glClear                                   UTGLES_glad_glClear
#define glad_glClearColor                              UTGLES_glad_glClearColor
#define glad_glClearDepthf                             UTGLES_glad_glClearDepthf
#define glad_glClearStencil                            UTGLES_glad_glClearStencil
#define glad_glColorMask                               UTGLES_glad_glColorMask
#define glad_glCompileShader                           UTGLES_glad_glCompileShader
#define glad_glCompressedTexImage2D                    UTGLES_glad_glCompressedTexImage2D
#define glad_glCompressedTexSubImage2D                 UTGLES_glad_glCompressedTexSubImage2D
#define glad_glCopyTexImage2D                          UTGLES_glad_glCopyTexImage2D
#define glad_glCopyTexSubImage2D                       UTGLES_glad_glCopyTexSubImage2D
#define glad_glCreateProgram                           UTGLES_glad_glCreateProgram
#define glad_glCreateShader                            UTGLES_glad_glCreateShader
#define glad_glCullFace                                UTGLES_glad_glCullFace
#define glad_glDeleteBuffers                           UTGLES_glad_glDeleteBuffers
#define glad_glDeleteFramebuffers                      UTGLES_glad_glDeleteFramebuffers
#define glad_glDeleteProgram                           UTGLES_glad_glDeleteProgram
#define glad_glDeleteRenderbuffers                     UTGLES_glad_glDeleteRenderbuffers
#define glad_glDeleteShader                            UTGLES_glad_glDeleteShader
#define glad_glDeleteTextures                          UTGLES_glad_glDeleteTextures
#define glad_glDepthFunc                               UTGLES_glad_glDepthFunc
#define glad_glDepthMask                               UTGLES_glad_glDepthMask
#define glad_glDepthRangef                             UTGLES_glad_glDepthRangef
#define glad_glDetachShader                            UTGLES_glad_glDetachShader
#define glad_glDisable                                 UTGLES_glad_glDisable
#define glad_glDisableVertexAttribArray                UTGLES_glad_glDisableVertexAttribArray
#define glad_glDrawArrays                              UTGLES_glad_glDrawArrays
#define glad_glDrawElements                            UTGLES_glad_glDrawElements
#define glad_glEnable                                  UTGLES_glad_glEnable
#define glad_glEnableVertexAttribArray                 UTGLES_glad_glEnableVertexAttribArray
#define glad_glFinish                                  UTGLES_glad_glFinish
#define glad_glFlush                                   UTGLES_glad_glFlush
#define glad_glFramebufferRenderbuffer                 UTGLES_glad_glFramebufferRenderbuffer
#define glad_glFramebufferTexture2D                    UTGLES_glad_glFramebufferTexture2D
#define glad_glFrontFace                               UTGLES_glad_glFrontFace
#define glad_glGenBuffers                              UTGLES_glad_glGenBuffers
#define glad_glGenFramebuffers                         UTGLES_glad_glGenFramebuffers
#define glad_glGenRenderbuffers                        UTGLES_glad_glGenRenderbuffers
#define glad_glGenTextures                             UTGLES_glad_glGenTextures
#define glad_glGenerateMipmap                          UTGLES_glad_glGenerateMipmap
#define glad_glGetActiveAttrib                         UTGLES_glad_glGetActiveAttrib
#define glad_glGetActiveUniform                        UTGLES_glad_glGetActiveUniform
#define glad_glGetAttachedShaders                      UTGLES_glad_glGetAttachedShaders
#define glad_glGetAttribLocation                       UTGLES_glad_glGetAttribLocation
#define glad_glGetBooleanv                             UTGLES_glad_glGetBooleanv
#define glad_glGetBufferParameteriv                    UTGLES_glad_glGetBufferParameteriv
#define glad_glGetError                                UTGLES_glad_glGetError
#define glad_glGetFloatv                               UTGLES_glad_glGetFloatv
#define glad_glGetFramebufferAttachmentParameteriv     UTGLES_glad_glGetFramebufferAttachmentParameteriv
#define glad_glGetIntegerv                             UTGLES_glad_glGetIntegerv
#define glad_glGetProgramInfoLog                       UTGLES_glad_glGetProgramInfoLog
#define glad_glGetProgramiv                            UTGLES_glad_glGetProgramiv
#define glad_glGetRenderbufferParameteriv              UTGLES_glad_glGetRenderbufferParameteriv
#define glad_glGetShaderInfoLog                        UTGLES_glad_glGetShaderInfoLog
#define glad_glGetShaderPrecisionFormat                UTGLES_glad_glGetShaderPrecisionFormat
#define glad_glGetShaderSource                         UTGLES_glad_glGetShaderSource
#define glad_glGetShaderiv                             UTGLES_glad_glGetShaderiv
#define glad_glGetString                               UTGLES_glad_glGetString
#define glad_glGetTexParameterfv                       UTGLES_glad_glGetTexParameterfv
#define glad_glGetTexParameteriv                       UTGLES_glad_glGetTexParameteriv
#define glad_glGetUniformLocation                      UTGLES_glad_glGetUniformLocation
#define glad_glGetUniformfv                            UTGLES_glad_glGetUniformfv
#define glad_glGetUniformiv                            UTGLES_glad_glGetUniformiv
#define glad_glGetVertexAttribPointerv                 UTGLES_glad_glGetVertexAttribPointerv
#define glad_glGetVertexAttribfv                       UTGLES_glad_glGetVertexAttribfv
#define glad_glGetVertexAttribiv                       UTGLES_glad_glGetVertexAttribiv
#define glad_glHint                                    UTGLES_glad_glHint
#define glad_glIsBuffer                                UTGLES_glad_glIsBuffer
#define glad_glIsEnabled                               UTGLES_glad_glIsEnabled
#define glad_glIsFramebuffer                           UTGLES_glad_glIsFramebuffer
#define glad_glIsProgram                               UTGLES_glad_glIsProgram
#define glad_glIsRenderbuffer                          UTGLES_glad_glIsRenderbuffer
#define glad_glIsShader                                UTGLES_glad_glIsShader
#define glad_glIsTexture                               UTGLES_glad_glIsTexture
#define glad_glLineWidth                               UTGLES_glad_glLineWidth
#define glad_glLinkProgram                             UTGLES_glad_glLinkProgram
#define glad_glPixelStorei                             UTGLES_glad_glPixelStorei
#define glad_glPolygonOffset                           UTGLES_glad_glPolygonOffset
#define glad_glReadPixels                              UTGLES_glad_glReadPixels
#define glad_glReleaseShaderCompiler                   UTGLES_glad_glReleaseShaderCompiler
#define glad_glRenderbufferStorage                     UTGLES_glad_glRenderbufferStorage
#define glad_glSampleCoverage                          UTGLES_glad_glSampleCoverage
#define glad_glScissor                                 UTGLES_glad_glScissor
#define glad_glShaderBinary                            UTGLES_glad_glShaderBinary
#define glad_glShaderSource                            UTGLES_glad_glShaderSource
#define glad_glStencilFunc                             UTGLES_glad_glStencilFunc
#define glad_glStencilFuncSeparate                     UTGLES_glad_glStencilFuncSeparate
#define glad_glStencilMask                             UTGLES_glad_glStencilMask
#define glad_glStencilMaskSeparate                     UTGLES_glad_glStencilMaskSeparate
#define glad_glStencilOp                               UTGLES_glad_glStencilOp
#define glad_glStencilOpSeparate                       UTGLES_glad_glStencilOpSeparate
#define glad_glTexImage2D                              UTGLES_glad_glTexImage2D
#define glad_glTexParameterf                           UTGLES_glad_glTexParameterf
#define glad_glTexParameterfv                          UTGLES_glad_glTexParameterfv
#define glad_glTexParameteri                           UTGLES_glad_glTexParameteri
#define glad_glTexParameteriv                          UTGLES_glad_glTexParameteriv
#define glad_glTexSubImage2D                           UTGLES_glad_glTexSubImage2D
#define glad_glUniform1f                               UTGLES_glad_glUniform1f
#define glad_glUniform1fv                              UTGLES_glad_glUniform1fv
#define glad_glUniform1i                               UTGLES_glad_glUniform1i
#define glad_glUniform1iv                              UTGLES_glad_glUniform1iv
#define glad_glUniform2f                               UTGLES_glad_glUniform2f
#define glad_glUniform2fv                              UTGLES_glad_glUniform2fv
#define glad_glUniform2i                               UTGLES_glad_glUniform2i
#define glad_glUniform2iv                              UTGLES_glad_glUniform2iv
#define glad_glUniform3f                               UTGLES_glad_glUniform3f
#define glad_glUniform3fv                              UTGLES_glad_glUniform3fv
#define glad_glUniform3i                               UTGLES_glad_glUniform3i
#define glad_glUniform3iv                              UTGLES_glad_glUniform3iv
#define glad_glUniform4f                               UTGLES_glad_glUniform4f
#define glad_glUniform4fv                              UTGLES_glad_glUniform4fv
#define glad_glUniform4i                               UTGLES_glad_glUniform4i
#define glad_glUniform4iv                              UTGLES_glad_glUniform4iv
#define glad_glUniformMatrix2fv                        UTGLES_glad_glUniformMatrix2fv
#define glad_glUniformMatrix3fv                        UTGLES_glad_glUniformMatrix3fv
#define glad_glUniformMatrix4fv                        UTGLES_glad_glUniformMatrix4fv
#define glad_glUseProgram                              UTGLES_glad_glUseProgram
#define glad_glValidateProgram                         UTGLES_glad_glValidateProgram
#define glad_glVertexAttrib1f                          UTGLES_glad_glVertexAttrib1f
#define glad_glVertexAttrib1fv                         UTGLES_glad_glVertexAttrib1fv
#define glad_glVertexAttrib2f                          UTGLES_glad_glVertexAttrib2f
#define glad_glVertexAttrib2fv                         UTGLES_glad_glVertexAttrib2fv
#define glad_glVertexAttrib3f                          UTGLES_glad_glVertexAttrib3f
#define glad_glVertexAttrib3fv                         UTGLES_glad_glVertexAttrib3fv
#define glad_glVertexAttrib4f                          UTGLES_glad_glVertexAttrib4f
#define glad_glVertexAttrib4fv                         UTGLES_glad_glVertexAttrib4fv
#define glad_glVertexAttribPointer                     UTGLES_glad_glVertexAttribPointer
#define glad_glViewport                                UTGLES_glad_glViewport

#endif // __glad_es_symbol_prefix_h_
