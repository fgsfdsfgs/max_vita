/* game.c -- hooks and patches for everything other than AL and GL
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include "../config.h"
#include "../util.h"
#include "../so_util.h"
#include "../hooks.h"

#define APK_PATH "main.obb"

extern int __cxa_guard_acquire;
extern int __cxa_guard_release;
extern int __cxa_throw;

static int *deviceChip;
static int *deviceForm;
static int *definedDevice;

static SceCtrlData pad;
static SceTouchData touch_front;
static SceTouchPanelInfo panelInfoFront;

// control binding array
typedef struct {
  int unk[14];
} MaxPayne_InputControl;
static MaxPayne_InputControl *sm_control = NULL; // [32]

int NvAPKOpen(const char *path) {
  // debugPrintf("NvAPKOpen: %s\n", path);
  return 0;
}

int ProcessEvents(void) {
  return 0; // 1 is exit!
}

int AND_DeviceType(void) {
  // 0x1: phone
  // 0x2: tegra
  // low memory is < 256
  return (MEMORY_NEWLIB_MB << 6) | (3 << 2) | 0x1;
}

int AND_DeviceLocale(void) {
  return 0; // english
}

int AND_SystemInitialize(void) {
  // set device information in such a way that bloom isn't enabled
  *deviceForm = 1; // phone
  *deviceChip = 19; // not a tegra? tegras are 12, 13, 14
  *definedDevice = 27; // not a tegra?
  return 0;
}

int OS_ScreenGetHeight(void) {
  return SCREEN_H;
}

int OS_ScreenGetWidth(void) {
  return SCREEN_W;
}

char *OS_FileGetArchiveName(int mode) {
  char *out = malloc(strlen(APK_PATH) + 1);
  out[0] = '\0';
  if (mode == 1) // main
    strcpy(out, APK_PATH);
  return out;
}

void ExitAndroidGame(int code) {
  fios_terminate(); // won't do anything if not initialized
  sceKernelExitProcess(0);
}

int thread_stub(SceSize args, int *argp) {
  int (* func)(void *arg) = (void *)argp[0];
  void *arg = (void *)argp[1];
  char *out = (char *)argp[2];
  out[0x41] = 1; // running
  int ret = func(arg);
  // all threads in Max that are spawned this way are one-shot,
  // so it's our responsibility to free the allocated resources
  sceKernelExitDeleteThread(ret);
  return 0;
}

// this is supposed to allocate and return a thread handle struct, but the game never uses it
// and never frees it, so we just return a pointer to some static garbage
void *OS_ThreadLaunch(int (* func)(), void *arg, int r2, char *name, int r4, int priority) {
  static char buf[0x48];
  const int min_priority = 0xA0;
  const int max_priority = 0x60;
  int vita_priority;

  switch (priority) {
    case 0:
      vita_priority = min_priority;
      break;
    case 1:
      vita_priority = min_priority - 2 * (min_priority - max_priority) / 3;
      break;
    case 2:
      vita_priority = min_priority - 4 * (min_priority - max_priority) / 5;
      break;
    case 3:
      vita_priority = max_priority;
      break;
    default:
      vita_priority = 0x10000100;
      break;
  }

  // debugPrintf("OS_ThreadLaunch %s with priority %x\n", name, vita_priority);

  SceUID thid = sceKernelCreateThread(name, (SceKernelThreadEntry)thread_stub, vita_priority, 128 * 1024, 0, 0, NULL);
  if (thid >= 0) {
    // fake thread handle (see above)
    char *out = buf;
    *(int *)(out + 0x24) = thid;

    uintptr_t args[3];
    args[0] = (uintptr_t)func;
    args[1] = (uintptr_t)arg;
    args[2] = (uintptr_t)out;
    sceKernelStartThread(thid, sizeof(args), args);

    return out;
  }

  return NULL;
}

int ReadDataFromPrivateStorage(const char *file, void **data, int *size) {
  char fullpath[512];
  snprintf(fullpath, sizeof(fullpath), "%s/%s", fs_root, file);

  FILE *f = fopen(fullpath, "rb");
  if (!f) return 0;

  fseek(f, 0, SEEK_END);
  const int sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  int ret = 0;

  if (sz > 0) {
    void *buf = malloc(sz);
    if (buf && fread(buf, sz, 1, f)) {
      ret = 1;
      *size = sz;
      *data = buf;
    } else {
      free(buf);
    }
  }

  fclose(f);

  return ret;
}

int WriteDataToPrivateStorage(const char *file, const void *data, int size) {
  char fullpath[512];
  snprintf(fullpath, sizeof(fullpath), "%s/%s", fs_root, file);

  FILE *f = fopen(fullpath, "wb");
  if (!f) return 0;

  const int ret = fwrite(data, size, 1, f);
  fclose(f);

  return ret;
}

// 0, 5, 6: XBOX 360
// 4: MogaPocket
// 7: MogaPro
// 8: PS3
// 9: IOSExtended
// 10: IOSSimple
int WarGamepad_GetGamepadType(int port) {
  if (port != 0 && port != 1)
    return -1;

  if (sceCtrlPeekBufferPositiveExt2(port == 0 ? 0 : 2, &pad, 1) < 0)
    return -1;

  if (port == 0) {
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch_front, 1);
  }

  return 8;
}

int WarGamepad_GetGamepadButtons(int port) {
  int mask = 0;

  if (pad.buttons & SCE_CTRL_CROSS)
    mask |= 0x1;
  if (pad.buttons & SCE_CTRL_CIRCLE)
    mask |= 0x2;
  if (pad.buttons & SCE_CTRL_SQUARE)
    mask |= 0x4;
  if (pad.buttons & SCE_CTRL_TRIANGLE)
    mask |= 0x8;
  if (pad.buttons & SCE_CTRL_START)
    mask |= 0x10;
  if (pad.buttons & SCE_CTRL_SELECT)
    mask |= 0x20;
  if (pad.buttons & SCE_CTRL_L2)
    mask |= 0x40; // L1
  if (pad.buttons & SCE_CTRL_R2)
    mask |= 0x80; // R1
  if (pad.buttons & SCE_CTRL_UP)
    mask |= 0x100;
  if (pad.buttons & SCE_CTRL_DOWN)
    mask |= 0x200;
  if (pad.buttons & SCE_CTRL_LEFT)
    mask |= 0x400;
  if (pad.buttons & SCE_CTRL_RIGHT)
    mask |= 0x800;

  for (int i = 0; i < touch_front.reportNum; i++) {
    for (int i = 0; i < touch_front.reportNum; i++) {
      if (touch_front.report[i].y >= (panelInfoFront.minAaY + panelInfoFront.maxAaY) / 2) {
        if (touch_front.report[i].x < (panelInfoFront.minAaX + panelInfoFront.maxAaX) / 2) {
          if (touch_front.report[i].x >= config.touch_x_margin)
            mask |= 0x40; // L1
        } else {
          if (touch_front.report[i].x < (panelInfoFront.maxAaX - config.touch_x_margin))
            mask |= 0x80; // R1
        }
      }
    }
  }

  return mask;
}

float WarGamepad_GetGamepadAxis(int port, int axis) {
  float val = 0.0f;

  switch (axis) {
    case 0:
      val = ((float)pad.lx - 128.0f) / 128.0f;
      break;
    case 1:
      val = ((float)pad.ly - 128.0f) / 128.0f;
      break;
    case 2:
      val = ((float)pad.rx - 128.0f) / 128.0f;
      break;
    case 3:
      val = ((float)pad.ry - 128.0f) / 128.0f;
      break;
    case 4: // L2
      val = (pad.buttons & SCE_CTRL_L1) ? 1.0f : 0.0f;
      break;
    case 5: // R2
      val = (pad.buttons & SCE_CTRL_R1) ? 1.0f : 0.0f;
      break;
  }

  if (fabsf(val) > 0.2f)
    return val;

  return 0.0f;
}

static int (* MaxPayne_InputControl_getButton)(MaxPayne_InputControl *, int);

int MaxPayne_ConfiguredInput_readCrouch(void *this) {
  static int prev = 0;
  static int latch = 0;
  // crouch is control #5
  const int new = MaxPayne_InputControl_getButton(&sm_control[5], 0);
  if (prev != new) {
    prev = new;
    if (new) latch = !latch;
  }
  return latch;
}

int GetAndroidCurrentLanguage(void) {
  // this will be loaded from config.txt; cap it
  if (config.language < 0 || config.language > 6)
    config.language = 0; // english
  return config.language;
}

void SetAndroidCurrentLanguage(int lang) {
  if (config.language != lang) {
    // changed; save config
    config.language = lang;
    char fname[0x200];
    snprintf(fname, sizeof(fname), "%s/" CONFIG_NAME, fs_root);
    write_config(fname);
  }
}

static int (* R_File_loadArchives)(void *this);
static void (* R_File_unloadArchives)(void *this);
static void (* R_File_enablePriorityArchive)(void *this, const char *arc);

int R_File_setFileSystemRoot(void *this, const char *root) {
  // root appears to be unused?
  R_File_unloadArchives(this);
  const int res = R_File_loadArchives(this);
  R_File_enablePriorityArchive(this, config.mod_file);
  return res;
}

int X_DetailLevel_getCharacterShadows(void) {
  return config.character_shadows;
}

int X_DetailLevel_getDropHighestLOD(void) {
  return config.drop_highest_lod;
}

float X_DetailLevel_getDecalLimitMultiplier(void) {
  return config.decal_limit;
}

float X_DetailLevel_getDebrisProjectileLimitMultiplier(void) {
  return config.debris_limit;
}

void patch_game(void) {
  // make it crash in an obvious location when it calls JNI methods
  hook_thumb(so_find_addr("_Z24NVThreadGetCurrentJNIEnvv"), (uintptr_t)0x1337);

  hook_thumb(so_find_addr("__cxa_throw"), (uintptr_t)&__cxa_throw);
  hook_thumb(so_find_addr("__cxa_guard_acquire"), (uintptr_t)&__cxa_guard_acquire);
  hook_thumb(so_find_addr("__cxa_guard_release"), (uintptr_t)&__cxa_guard_release);

  hook_thumb(so_find_addr("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority"), (uintptr_t)OS_ThreadLaunch);

  // used to check some flags
  hook_thumb(so_find_addr("_Z20OS_ServiceAppCommandPKcS0_"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z23OS_ServiceAppCommandIntPKci"), (uintptr_t)ret0);
  // this is checked on startup
  hook_thumb(so_find_addr("_Z25OS_ServiceIsWifiAvailablev"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z28OS_ServiceIsNetworkAvailablev"), (uintptr_t)ret0);
  // don't bother opening links
  hook_thumb(so_find_addr("_Z18OS_ServiceOpenLinkPKc"), (uintptr_t)ret0);

  // don't have movie playback yet
  hook_thumb(so_find_addr("_Z12OS_MoviePlayPKcbbf"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z12OS_MovieStopv"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z20OS_MovieSetSkippableb"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z17OS_MovieTextScalei"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z17OS_MovieIsPlayingPi"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z20OS_MoviePlayinWindowPKciiiibbf"), (uintptr_t)ret0);

  hook_thumb(so_find_addr("_Z17OS_ScreenGetWidthv"), (uintptr_t)OS_ScreenGetWidth);
  hook_thumb(so_find_addr("_Z18OS_ScreenGetHeightv"), (uintptr_t)OS_ScreenGetHeight);

  hook_thumb(so_find_addr("_Z9NvAPKOpenPKc"), (uintptr_t)ret0);

  // TODO: implement touch here
  hook_thumb(so_find_addr("_Z13ProcessEventsb"), (uintptr_t)ProcessEvents);

  // both set and get are called, remember the language that it sets
  hook_thumb(so_find_addr("_Z25GetAndroidCurrentLanguagev"), (uintptr_t)GetAndroidCurrentLanguage);
  hook_thumb(so_find_addr("_Z25SetAndroidCurrentLanguagei"), (uintptr_t)SetAndroidCurrentLanguage);

  hook_thumb(so_find_addr("_Z14AND_DeviceTypev"), (uintptr_t)AND_DeviceType);
  hook_thumb(so_find_addr("_Z16AND_DeviceLocalev"), (uintptr_t)AND_DeviceLocale);
  hook_thumb(so_find_addr("_Z20AND_SystemInitializev"), (uintptr_t)AND_SystemInitialize);
  hook_thumb(so_find_addr("_Z21AND_ScreenSetWakeLockb"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z22AND_FileGetArchiveName13OSFileArchive"), (uintptr_t)OS_FileGetArchiveName);

  hook_thumb(so_find_addr("_Z26ReadDataFromPrivateStoragePKcRPcRi"), (uintptr_t)ReadDataFromPrivateStorage);
  hook_thumb(so_find_addr("_Z25WriteDataToPrivateStoragePKcS0_i"), (uintptr_t)WriteDataToPrivateStorage);

  hook_thumb(so_find_addr("_Z25WarGamepad_GetGamepadTypei"), (uintptr_t)WarGamepad_GetGamepadType);
  hook_thumb(so_find_addr("_Z28WarGamepad_GetGamepadButtonsi"), (uintptr_t)WarGamepad_GetGamepadButtons);
  hook_thumb(so_find_addr("_Z25WarGamepad_GetGamepadAxisii"), (uintptr_t)WarGamepad_GetGamepadAxis);

  // no vibration of any kind
  hook_thumb(so_find_addr("_Z12VibratePhonei"), (uintptr_t)ret0);
  hook_thumb(so_find_addr("_Z14Mobile_Vibratei"), (uintptr_t)ret0);

  // no touchsense
  hook_thumb(so_find_addr("_ZN10TouchSenseC2Ev"), (uintptr_t)ret0);

  hook_thumb(so_find_addr("_Z15ExitAndroidGamev"), (uintptr_t)ExitAndroidGame);

  // hook detail level getters to our own settings
  hook_thumb(so_find_addr("_ZN13X_DetailLevel19getCharacterShadowsEv"), (uintptr_t)X_DetailLevel_getCharacterShadows);
  hook_thumb(so_find_addr("_ZN13X_DetailLevel34getDebrisProjectileLimitMultiplierEv"), (uintptr_t)X_DetailLevel_getDebrisProjectileLimitMultiplier);
  hook_thumb(so_find_addr("_ZN13X_DetailLevel23getDecalLimitMultiplierEv"), (uintptr_t)X_DetailLevel_getDecalLimitMultiplier);
  hook_thumb(so_find_addr("_ZN13X_DetailLevel13dropHighesLODEv"), (uintptr_t)X_DetailLevel_getDropHighestLOD);

  // crouch toggle
  if (config.crouch_toggle) {
    sm_control = (void *)so_find_addr("_ZN24MaxPayne_ConfiguredInput10sm_controlE");
    MaxPayne_InputControl_getButton = (void *)so_find_addr("_ZNK21MaxPayne_InputControl9getButtonEi");
    hook_thumb(so_find_addr("_ZNK24MaxPayne_ConfiguredInput10readCrouchEv"), (uintptr_t)MaxPayne_ConfiguredInput_readCrouch);
  }

  // if mod file is enabled, hook into R_File::setFileSystemRoot to set the mod as the priority archive
  // before R_File::loadArchives is called
  if (config.mod_file[0]) {
    R_File_unloadArchives = (void *)so_find_addr("_ZN6R_File14unloadArchivesEv");
    R_File_loadArchives = (void *)so_find_addr("_ZN6R_File12loadArchivesEv");
    R_File_enablePriorityArchive = (void *)so_find_addr("_ZN6R_File21enablePriorityArchiveEPKc");
    hook_thumb(so_find_addr("_ZN6R_File17setFileSystemRootEPKc"), (uintptr_t)R_File_setFileSystemRoot);
  }

  // vars used in AND_SystemInitialize
  deviceChip = (int *)so_find_addr("deviceChip");
  deviceForm = (int *)so_find_addr("deviceForm");
  definedDevice = (int *)so_find_addr("definedDevice");

  sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panelInfoFront);
}
