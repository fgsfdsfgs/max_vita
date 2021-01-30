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

static int *deviceChip;
static int *deviceForm;
static int *definedDevice;

static int cur_language = -1; // unset

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
  return (MEMORY_MB << 6) | (3 << 2) | 0x1;
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
  char fullpath[1024];
  snprintf(fullpath, sizeof(fullpath), "%s/%s", fs_root, file);

  debugPrintf("ReadDataFromPrivateStorage %s\n", fullpath);

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
  char fullpath[1024];
  snprintf(fullpath, sizeof(fullpath), "%s/%s", fs_root, file);

  debugPrintf("WriteDataToPrivateStorage %s\n", fullpath);

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
int WarGamepad_GetGamepadType(int padnum) {
  return 8;
}

int WarGamepad_GetGamepadButtons(int padnum) {
  int mask = 0;

  SceCtrlData pad;
  sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

  SceTouchData touch;
  sceTouchPeek(0, &touch, 1);

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
/*
  if (pad.buttons & SCE_CTRL_L1)
    mask |= 0x40;
  if (pad.buttons & SCE_CTRL_R1)
    mask |= 0x80;
*/
  if (pad.buttons & SCE_CTRL_UP)
    mask |= 0x100;
  if (pad.buttons & SCE_CTRL_DOWN)
    mask |= 0x200;
  if (pad.buttons & SCE_CTRL_LEFT)
    mask |= 0x400;
  if (pad.buttons & SCE_CTRL_RIGHT)
    mask |= 0x800;

  for (int i = 0; i < touch.reportNum; i++) {
    if (touch.report[i].y > 1088/2) {
      if (touch.report[i].x < 1920/2)
        mask |= 0x1000; // L3
      else
        mask |= 0x2000; // R3
    }
  }

  return mask;
}

float WarGamepad_GetGamepadAxis(int padnum, int axis) {
  SceCtrlData pad;
  sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

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

int GetAndroidCurrentLanguage(void) {
  if (cur_language < 0) {
    // read it from a file if available, otherwise set to english
    char fname[0x200];
    snprintf(fname, sizeof(fname), "%s/language.txt", fs_root);
    FILE *f = fopen(fname, "r");
    if (f) {
      fscanf(f, "%d", &cur_language);
      fclose(f);
    }
    if (cur_language < 0)
      cur_language = 0; // english
  }
  return cur_language;
}

void SetAndroidCurrentLanguage(int lang) {
  if (cur_language != lang) {
    // changed; save it to a file
    char fname[0x200];
    snprintf(fname, sizeof(fname), "%s/language.txt", fs_root);
    FILE *f = fopen(fname, "w");
    if (f) {
      fprintf(f, "%d", lang);
      fclose(f);
    }
    cur_language = lang;
  }
}

void patch_game(void) {
  // make it crash in an obvious location when it calls JNI methods
  hook_thumb(so_find_addr("_Z24NVThreadGetCurrentJNIEnvv"), (uintptr_t)0x1337);

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

  // enable shadows
  hook_thumb(so_find_addr("_ZN13X_DetailLevel19getCharacterShadowsEv"), (uintptr_t)ret1);

  // vars used in AND_SystemInitialize
  deviceChip = (int *)so_find_addr("deviceChip");
  deviceForm = (int *)so_find_addr("deviceForm");
  definedDevice = (int *)so_find_addr("definedDevice");
}
