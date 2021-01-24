#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <psp2/io/dirent.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <pib.h>
#include <kubridge.h>

#include "config.h"
#include "util.h"
#include "so_util.h"
#include "hooks.h"
#include "imports.h"

int sceLibcHeapSize = 8 * 1024 * 1024;

int _newlib_heap_size_user = MEMORY_MB * 1024 * 1024;

int main(void) {
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  pibInit(PIB_SHACCCG | PIB_GET_PROC_ADDR_CORE);

  so_load(SO_PATH);
  so_resolve(dynlib_functions, dynlib_numfunctions);

  patch_openal();
  patch_opengl();
  patch_game();

  // can't set it in the initializer because it's not constant
  stderr_fake = stderr;

  so_flush_caches();

  so_excute_init();

  so_free_temp();

  // won't save without it
  sceIoMkdir(DATA_PATH "/savegames", 0777);

  strcpy((char *)so_find_addr("StorageRootBuffer"), DATA_PATH);
  *(uint8_t *)so_find_addr("IsAndroidPaused") = 0;

  uint32_t (* initGraphics)(void) = (void *)so_find_addr("_Z12initGraphicsv");
  initGraphics();

  uint32_t (* ShowJoystick)(int show) = (void *)so_find_addr("_Z12ShowJoystickb");
  ShowJoystick(0);

  int (* NVEventAppMain)(int argc, char *argv[]) = (void *)so_find_addr("_Z14NVEventAppMainiPPc");
  NVEventAppMain(0, NULL);

  return 0;
}
