#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

uintptr_t find_addr_by_symbol(char *symbol);

extern void *text_base;

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

void opengl_patch() {
  // the game replaces some keywords in shaders with common code blocks to generate variants
  // replace the string replacements
  memcpy((void *)find_addr_by_symbol("mipmapReplace"), cg_mipmapReplace, sizeof(cg_mipmapReplace));
  memcpy((void *)find_addr_by_symbol("fragColorReplace"), cg_fragColorReplace, sizeof(cg_fragColorReplace));
  memcpy((void *)find_addr_by_symbol("colorReplace"), cg_colorReplace, sizeof(cg_colorReplace));
  // memcpy((void *)find_addr_by_symbol("alphaTestReplace"), cg_alphaTestReplace, sizeof(cg_alphaTestReplace)); // original works in cg
}
