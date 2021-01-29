/* opengl.c -- OpenGL and shader generator hooks and patches
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "../config.h"
#include "../util.h"
#include "../so_util.h"

void NVEventEGLSwapBuffers(void) {
  vglSwapBuffers(GL_FALSE);
}

void NVEventEGLMakeCurrent(void) {
}

void NVEventEGLUnmakeCurrent(void) {
}

int NVEventEGLInit(void) {
  vglWaitVblankStart(GL_TRUE);
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
