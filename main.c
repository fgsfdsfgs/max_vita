/* main.c
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vitasdk.h>
#include <vitaGL.h>
#include <kubridge.h>

#include "config.h"
#include "util.h"
#include "error.h"
#include "so_util.h"
#include "hooks.h"
#include "imports.h"

int sceLibcHeapSize = 8 * 1024 * 1024;

int _newlib_heap_size_user = MEMORY_MB * 1024 * 1024;

static int find_data(void) {
  const char *drives[] = { "uma0", "imc0", "ux0" };
  // check if a maxpayne folder exists on one of the drives
  // default to the last one (ux0)
  for (unsigned int i = 0; i < sizeof(drives) / sizeof(*drives); ++i) {
    snprintf(fs_root, 0x200, "%s:" DATA_PATH, drives[i]);
    SceUID dir = sceIoDopen(fs_root);
    if (dir >= 0) {
      sceIoDclose(dir);
      return 0;
    }
  }
  // not found
  return -1;
}

static void check_data(void) {
  const char *files[] = {
    "MaxPayneSoundsv2.msf",
    "x_data.ras",
    "x_english.ras",
    "x_level1.ras",
    "x_level2.ras",
    "x_level3.ras",
    "data",
    "es2",
    // if this is missing, assets folder hasn't been merged in
    "es2/DefaultPixel.txt"
  };
  char path[0x200];
  SceIoStat stat;
  // check if all the required files are present
  for (unsigned int i = 0; i < sizeof(files) / sizeof(*files); ++i) {
    snprintf(path, sizeof(path), "%s/%s", fs_root, files[i]);
    if (sceIoGetstat(path, &stat) < 0) {
      fatal_error("Could not find\n%s.\nCheck your data files.", path);
      break;
    }
  }
}

static int check_kubridge(void) {
  // taken from VitaShell
  extern SceUID _vshKernelSearchModuleByName(const char *, int *);
  int search_unk[2];
  return _vshKernelSearchModuleByName("kubridge", search_unk);
}

static int check_shacccg(void) {
  SceIoStat stat;
  const char *path = "ur0:/data/libshacccg.suprx";
  return sceIoGetstat(path, &stat);
}

int main(void) {
  char path[0x200];

  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  if (check_kubridge() < 0)
    fatal_error("It appears that kubridge is not loaded.\nPlease install it and reboot.");

  if (check_shacccg() < 0)
    fatal_error("It appears that you don't have libshacccg.suprx in ur0:/data/.");

  if (find_data() < 0)
    fatal_error("Could not find\n" DATA_PATH "\non uma0, imc0 or ux0.");

  check_data();

  // try to read the config file and create one with default values if it's missing
  snprintf(path, sizeof(path), "%s/" CONFIG_NAME, fs_root);
  if (read_config(path) < 0)
    write_config(path);

  snprintf(path, sizeof(path), "%s/" SO_NAME, fs_root);
  if (so_load(path) < 0)
    fatal_error("Could not load\n%s.", path);

  update_imports();
  so_resolve(dynlib_functions, dynlib_numfunctions);

  patch_openal();
  patch_opengl();
  patch_game();
  if (config.use_fios2)
    patch_io();

  // can't set it in the initializer because it's not constant
  stderr_fake = stderr;

  so_flush_caches();

  so_excute_init_array();
  so_free_temp();

  // won't save without it
  snprintf(path, sizeof(path), "%s/savegames", fs_root);
  sceIoMkdir(path, 0777);

  // initialize fios2 if it's enabled
  if (config.use_fios2) {
    const int res = fios_init();
    if (res < 0)
      fatal_error("Could not init FIOS2:\nerror %08x", (unsigned)res);
  }

  strcpy((char *)so_find_addr("StorageRootBuffer"), fs_root);
  *(uint8_t *)so_find_addr("IsAndroidPaused") = 0;
  *(uint8_t *)so_find_addr("UseRGBA8") = 1; // RGB565 fbos are not supported by vgl

  uint32_t (* initGraphics)(void) = (void *)so_find_addr("_Z12initGraphicsv");
  uint32_t (* ShowJoystick)(int show) = (void *)so_find_addr("_Z12ShowJoystickb");
  int (* NVEventAppMain)(int argc, char *argv[]) = (void *)so_find_addr("_Z14NVEventAppMainiPPc");

  // init vgl as late as possible in case any of the symbols above are missing
  vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
  vglInitExtended(0, SCREEN_W, SCREEN_H, 0x1000000, config.msaa);
  vglUseVram(GL_TRUE);

  initGraphics();
  ShowJoystick(0);
  NVEventAppMain(0, NULL);

  return 0;
}
