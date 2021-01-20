#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <kubridge.h>

#include "config.h"

uintptr_t find_addr_by_symbol(char *symbol);
void hook_arm(uintptr_t addr, uintptr_t dst);
void debugPrintf(const char *fmt, ...);

extern void *text_base;

static const char *cg_mipmap[] = {
  "#define TEX2D_MIPSAMPLE(samp, uv) tex2D(samp, uv)",
  "#define TEX2D_MIPSAMPLE(samp, uv) tex2Dbias(samp, half4(uv, 0.0, 1.0))"
};

uint32_t ReplaceStringHack(char *buf, const char *what, const char *withwhat) {
  // this function is only used in the "shader patcher" to replace a few keywords with
  // common code blocks

  if (!strcmp(what, "MIPMAP")) {
    // this originally added `, -1.0` to the texture2D call, which adds mipmap level bias
    // the cg version of that is tex2Dbias, which takes the bias in uv.w
    // in cg shaders we use a macro instead
    withwhat = cg_mipmap[(withwhat[0] != '\0')];
  }

  // SETFRAGCOLOR, PREFIX are not used in cg shaders

  char *found = strstr(buf, what);
  if (found) {
    char tmp[0x800];
    const int oldsize = strlen(what);
    const int newsize = strlen(withwhat);
    char *rest = found + oldsize;
    const int restsize = strlen(rest);
    memcpy(tmp, withwhat, newsize + 1);
    memcpy(tmp + newsize, rest, restsize + 1);
    memcpy(found, tmp, (found - buf) + newsize + restsize + 1);
  }

  return 0;
}

void opengl_patch() {
  // load cg shaders from a separate folder for convenience
  // FIXME: these offsets are for v1.1, and there's probably a better way to do this
  kuKernelCpuUnrestrictedMemcpy((char *)text_base + 0xb08564, "../cg/", 7);
  kuKernelCpuUnrestrictedMemcpy((char *)text_base + 0xb08598, "cg/", 4);

  hook_arm(find_addr_by_symbol("_Z13ReplaceStringPcPKcS1_"), (uintptr_t)ReplaceStringHack);
}
