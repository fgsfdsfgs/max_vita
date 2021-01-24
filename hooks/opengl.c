#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "../config.h"
#include "../util.h"
#include "../so_util.h"

static EGLDisplay display = NULL;
static EGLSurface surface = NULL;
static EGLContext context = NULL;

void NVEventEGLSwapBuffers(void) {
  eglSwapBuffers(display, surface);
}

void NVEventEGLMakeCurrent(void) {
}

void NVEventEGLUnmakeCurrent(void) {
}

int NVEventEGLInit(void) {
  EGLint majorVersion;
  EGLint minorVersion;
  EGLint numConfigs = 0;
  EGLConfig config;

  EGLint configAttribs[] = {
    EGL_CONFIG_ID, 2,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 32,
    EGL_STENCIL_SIZE, 8,
    EGL_SURFACE_TYPE, 5,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };

  const EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  display = eglGetDisplay(0);

  eglInitialize(display, &majorVersion, &minorVersion);
  eglBindAPI(EGL_OPENGL_ES_API);
  eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);

  surface = eglCreateWindowSurface(display, config, VITA_WINDOW_960X544, NULL);
  context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);

  eglMakeCurrent(display, surface, surface, context);

  debugPrintf("GL_EXTENSIONS: %s\n", glGetString(GL_EXTENSIONS));

  return 1; // success
}

static const char *cg_mipmapReplace[] = {
  "#define TEX2D_MIPSAMPLE(samp, uv) tex2Dbias(samp, half4(uv, 0.0, -1.0))",
  "#define TEX2D_MIPSAMPLE(samp, uv) tex2D(samp, uv)",
  "#define TEX2D_MIPSAMPLE(samp, uv) tex2Dbias(samp, half4(uv, 0.0, 1.0))",
};

static const char *cg_fragColorReplace[] = {
  "fixed4 gl_FragColor = ",
  "fixed4 gl_FragColor = fixed4(ColorAdd, 0.0) + ",
};

static const char *cg_colorReplace[] = {
  "",
  ", uniform half3 ColorAdd"
};

void patch_opengl(void) {
  // the game replaces some keywords in shaders with common code blocks to generate variants
  // replace the string replacements
  memcpy((void *)so_find_addr("mipmapReplace"), cg_mipmapReplace, sizeof(cg_mipmapReplace));
  memcpy((void *)so_find_addr("fragColorReplace"), cg_fragColorReplace, sizeof(cg_fragColorReplace));
  memcpy((void *)so_find_addr("colorReplace"), cg_colorReplace, sizeof(cg_colorReplace));
  // memcpy((void *)so_find_addr("alphaTestReplace"), cg_alphaTestReplace, sizeof(cg_alphaTestReplace)); // original works in cg

  // patch egl stuff
  hook_thumb(so_find_addr("_Z14NVEventEGLInitv"), (uintptr_t)NVEventEGLInit);
  hook_thumb(so_find_addr("_Z21NVEventEGLMakeCurrentv"), (uintptr_t)NVEventEGLMakeCurrent);
  hook_thumb(so_find_addr("_Z23NVEventEGLUnmakeCurrentv"), (uintptr_t)NVEventEGLUnmakeCurrent);
  hook_thumb(so_find_addr("_Z21NVEventEGLSwapBuffersv"), (uintptr_t)NVEventEGLSwapBuffers);
}
