/* error.c -- error handler
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/message_dialog.h>
#include <psp2/display.h>
#include <psp2/apputil.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "util.h"
#include "error.h"

// taken straight from the ime sample
// need all this GXM code because initializing vitaGL before the .so loader runs
// is somewhat of a pain, and commondialog shit still needs GXM

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define DISPLAY_WIDTH             960
#define DISPLAY_HEIGHT            544
#define DISPLAY_STRIDE_IN_PIXELS  1024
#define DISPLAY_BUFFER_COUNT      2
#define DISPLAY_MAX_PENDING_SWAPS 1

typedef struct {
  void *data;
  SceGxmSyncObject *sync;
  SceGxmColorSurface surf;
  SceUID uid;
} DisplayBuffer;

static unsigned int backbuf_idx = 0;
static unsigned int frontbuf_idx = 0;
static DisplayBuffer dbuf[DISPLAY_BUFFER_COUNT];

static void *dram_alloc(unsigned int size, SceUID *uid) {
  void *mem = NULL;
  size = ALIGN(size, 256 * 1024);
  *uid = sceKernelAllocMemBlock("gpu_mem", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
  sceKernelGetMemBlockBase(*uid, &mem);
  sceGxmMapMemory(mem, size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
  return mem;
}

static void gxm_vsync_cb(const void *callback_data) {
  sceDisplaySetFrameBuf(
    &(SceDisplayFrameBuf){
      sizeof(SceDisplayFrameBuf),
      *((void **)callback_data),
      DISPLAY_STRIDE_IN_PIXELS,
      0,
      DISPLAY_WIDTH,
      DISPLAY_HEIGHT
    }, 
    SCE_DISPLAY_SETBUF_NEXTFRAME
  );
}

static void gxm_init(void) {
  sceGxmInitialize(
    &(SceGxmInitializeParams) {
      0,
      DISPLAY_MAX_PENDING_SWAPS,
      gxm_vsync_cb,
      sizeof(void *),
      SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE
    }
  );

  for (unsigned int i = 0; i < DISPLAY_BUFFER_COUNT; i++) {
    dbuf[i].data = dram_alloc(4 * DISPLAY_STRIDE_IN_PIXELS * DISPLAY_HEIGHT, &dbuf[i].uid);
    sceGxmColorSurfaceInit(
      &dbuf[i].surf,
      SCE_GXM_COLOR_FORMAT_A8B8G8R8,
      SCE_GXM_COLOR_SURFACE_LINEAR,
      SCE_GXM_COLOR_SURFACE_SCALE_NONE,
      SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
      DISPLAY_WIDTH, DISPLAY_HEIGHT,
      DISPLAY_STRIDE_IN_PIXELS,
      dbuf[i].data
    );
    sceGxmSyncObjectCreate(&dbuf[i].sync);
  }
}

static void gxm_swap(void) {
  sceGxmPadHeartbeat(&dbuf[backbuf_idx].surf, dbuf[backbuf_idx].sync);
  sceGxmDisplayQueueAddEntry(dbuf[frontbuf_idx].sync, dbuf[backbuf_idx].sync, &dbuf[backbuf_idx].data);
  frontbuf_idx = backbuf_idx;
  backbuf_idx = (backbuf_idx + 1) % DISPLAY_BUFFER_COUNT;
}

static void gxm_term(void) {
  sceGxmTerminate();
  for (int i = 0; i < DISPLAY_BUFFER_COUNT; ++i)
    sceKernelFreeMemBlock(dbuf[i].uid);
}

void fatal_error(const char *fmt, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, fmt);
  vsnprintf(string, sizeof(string), fmt, list);
  va_end(list);

  debugPrintf("FATAL ERROR: %s\n", string);

  sceAppUtilInit(&(SceAppUtilInitParam){ }, &(SceAppUtilBootParam){ });
  sceCommonDialogSetConfigParam(&(SceCommonDialogConfigParam){ });

  gxm_init();

  SceMsgDialogUserMessageParam umsg_param;
  memset(&umsg_param, 0, sizeof(umsg_param));
  umsg_param.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
  umsg_param.msg = (const SceChar8 *)string;

  SceMsgDialogParam msg_param;
  sceMsgDialogParamInit(&msg_param);
  _sceCommonDialogSetMagicNumber(&msg_param.commonParam);
  msg_param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
  msg_param.userMsgParam = &umsg_param;

  if (sceMsgDialogInit(&msg_param) == 0) {
    while (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
      sceCommonDialogUpdate(
        &(SceCommonDialogUpdateParam) {
          { NULL, dbuf[backbuf_idx].data, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE_IN_PIXELS },
          dbuf[backbuf_idx].sync
        }
      );
      gxm_swap();
      sceDisplayWaitVblankStart();
    }
    sceMsgDialogTerm();
  }

  gxm_term();

  sceKernelExitProcess(0);

  // shut up gcc, this doesn't return
  while (1);
}
