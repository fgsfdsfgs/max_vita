#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <kubridge.h>

#include "config.h"

uintptr_t find_addr_by_symbol(char *symbol);
void debugPrintf(const char *fmt, ...);

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
  // load cg shaders from a separate folder for convenience
  // FIXME: these offsets are for v1.7, and there's probably a better way to do this
  kuKernelCpuUnrestrictedMemcpy((char *)text_base + 0x83dd08, "../cgs/", 7);
  kuKernelCpuUnrestrictedMemcpy((char *)text_base + 0x83dd10, "cgs/", 4);
  // in 1.7 the "es2/" copy is optimized into `mov r3, #0x2f327365`, so we have to patch the instruction
  // this also forces me to use "cgs/" instead of "cg/" because next copy starts at offset 4 into the buffer
  kuKernelCpuUnrestrictedMemcpy((char *)text_base + 0x50c2b8, "\x46\xf2\x63\x73\xc2\xf6\x73\x73", 8);

  // the game replaces some keywords in shaders with common code blocks to generate variants
  // replace the string replacements
  memcpy((void *)find_addr_by_symbol("mipmapReplace"), cg_mipmapReplace, sizeof(cg_mipmapReplace));
  memcpy((void *)find_addr_by_symbol("fragColorReplace"), cg_fragColorReplace, sizeof(cg_fragColorReplace));
  memcpy((void *)find_addr_by_symbol("colorReplace"), cg_colorReplace, sizeof(cg_colorReplace));
  // memcpy((void *)find_addr_by_symbol("alphaTestReplace"), cg_alphaTestReplace, sizeof(cg_alphaTestReplace)); // original works in cg
}
