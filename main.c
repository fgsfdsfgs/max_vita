#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <pib.h>
#include <kubridge.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <wchar.h>
#include <wctype.h>

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/reent.h>

#include <math_neon.h>

#include "elf.h"

#include "config.h"

int sceLibcHeapSize = 8 * 1024 * 1024;

int _newlib_heap_size_user = MEMORY_MB * 1024 * 1024;

void *memcpy_neon(void *destination, const void *source, size_t num);

void openal_patch();
void opengl_patch();

void *text_base, *data_base;
uint32_t text_size, data_size;

static Elf32_Sym *syms;
static int n_syms;

static char *dynstrtab;

extern int __aeabi_memclr;
extern int __aeabi_memclr4;
extern int __aeabi_memclr8;
extern int __aeabi_memcpy;
extern int __aeabi_memcpy4;
extern int __aeabi_memcpy8;
extern int __aeabi_memmove;
extern int __aeabi_memmove4;
extern int __aeabi_memmove8;
extern int __aeabi_memset;
extern int __aeabi_memset4;
extern int __aeabi_memset8;

extern int __cxa_atexit;
extern int __cxa_guard_acquire;
extern int __cxa_guard_release;
extern int __gnu_unwind_frame;

extern int __stack_chk_fail;
extern int __stack_chk_guard;

int debugPrintf(char *text, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, text);
  vsprintf(string, text, list);
  va_end(list);

  SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, string, strlen(string));
    sceIoClose(fd);
  }

  printf("%s", string);

  return 0;
}

int load_file(char *filename, void **data) {
  SceUID fd;
  SceUID blockid;
  size_t size;

  fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
  if (fd < 0)
    return fd;

  size = sceIoLseek(fd, 0, SCE_SEEK_END);
  sceIoLseek(fd, 0, SCE_SEEK_SET);

  blockid = sceKernelAllocMemBlock("file", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (size + 0xfff) & ~0xfff, NULL);
  if (blockid < 0)
    return blockid;

  sceKernelGetMemBlockBase(blockid, data);

  sceIoRead(fd, *data, size);
  sceIoClose(fd);

  return blockid;
}

uintptr_t find_addr_by_symbol(char *symbol) {
  for (int i = 0; i < n_syms; i++) {
    char *name = dynstrtab + syms[i].st_name;
    if (strcmp(name, symbol) == 0)
      return (uintptr_t)text_base + syms[i].st_value;
  }

  debugPrintf("Could not find symbol %s\n", symbol);
  return 0;
}

void hook_thumb(uintptr_t addr, uintptr_t dst) {
  if (addr == 0)
    return;
  addr &= ~1;
  if (addr & 2) {
    uint16_t nop = 0xbf00;
    kuKernelCpuUnrestrictedMemcpy((void *)addr, &nop, sizeof(nop));
    addr += 2;
  }
  uint32_t hook[2];
  hook[0] = 0xf000f8df; // LDR PC, [PC]
  hook[1] = dst;
  kuKernelCpuUnrestrictedMemcpy((void *)addr, hook, sizeof(hook));
}

void hook_arm(uintptr_t addr, uintptr_t dst) {
  if (addr == 0)
    return;
  uint32_t hook[2];
  hook[0] = 0xe51ff004; // LDR PC, [PC, #-0x4]
  hook[1] = dst;
  kuKernelCpuUnrestrictedMemcpy((void *)addr, hook, sizeof(hook));
}

// Only used in ReadALConfig
char *getenv(const char *name) {
  return NULL;
}

void __assert2(const char *file, int line, const char *func, const char *expr) {
  debugPrintf("assertion failed:\n%s:%d (%s): %s\n", file, line, func, expr);
  assert(0);
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, fmt);
  vsprintf(string, fmt, list);
  va_end(list);

  debugPrintf("%s: %s\n", tag, string);

  return 0;
}

int ret0() {
  return 0;
}

int ret1() {
  return 1;
}

int retm1() {
  return -1;
}

int NvAPKOpen(char *path) {
  // debugPrintf("NvAPKOpen: %s\n", path);
  return 0;
}

int mkdir(const char *pathname, mode_t mode) {
  // debugPrintf("mkdir: %s\n", pathname);
  if (sceIoMkdir(pathname, mode) < 0)
    return -1;
  return 0;
}

char *GetRockstarID(void) {
  return "flow";
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

int *deviceChip;
int *deviceForm;
int *definedDevice;

int AND_SystemInitialize(void) {
  // set device information in such a way that bloom isn't enabled
  *deviceForm = 1; // phone
  *deviceChip = 19; // not a tegra? tegras are 12, 13, 14
  *definedDevice = 27; // not a tegra?
  return 0;
}

int OS_ScreenGetHeight(void) {
  return 544;
}

int OS_ScreenGetWidth(void) {
  return 960;
}

#define APK_PATH "main.obb"

char *OS_FileGetArchiveName(int mode) {
  char *out = malloc(strlen(APK_PATH) + 1);
  out[0] = '\0';
  if (mode == 1) // main
    strcpy(out, APK_PATH);
  return out;
}

void ExitAndroidGame(int code) {
  // pibTerm();
  sceKernelExitProcess(0);
}

int thread_stub(SceSize args, int *argp) {
  int (* func)(void *arg) = (void *)argp[0];
  void *arg = (void *)argp[1];
  char *out = (char *)argp[2];
  out[0x41] = 1; // running
  return func(arg);
}

void *OS_ThreadLaunch(int (* func)(), void *arg, int r2, char *name, int r4, int priority) {
  int min_priority = 191;
  int max_priority = 64;
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

  SceUID thid = sceKernelCreateThread(name, (SceKernelThreadEntry)thread_stub, vita_priority, 1 * 1024 * 1024, 0, 0, NULL);
  if (thid >= 0) {
    char *out = malloc(0x48);

    uintptr_t args[3];
    args[0] = (uintptr_t)func;
    args[1] = (uintptr_t)arg;
    args[2] = (uintptr_t)out;
    sceKernelStartThread(thid, sizeof(args), args);

    return out;
  }

  return NULL;
}

int pthread_mutex_init_fake(pthread_mutex_t **uid, const int *mutexattr) {
  pthread_mutex_t *m = calloc(1, sizeof(pthread_mutex_t));
  if (!m) return -1;

  const int recursive = (mutexattr && *mutexattr == 1);
  *m = recursive ? PTHREAD_RECURSIVE_MUTEX_INITIALIZER : PTHREAD_MUTEX_INITIALIZER;

  int ret = pthread_mutex_init(m, NULL);
  if (ret < 0) {
    free(m);
    return -1;
  }

  *uid = m;

  return 0;
}

int pthread_mutex_destroy_fake(pthread_mutex_t **uid) {
  if (uid && *uid && (uintptr_t)*uid > 0x8000) {
    pthread_mutex_destroy(*uid);
    free(*uid);
    *uid = NULL;
  }
  return 0;
}

int pthread_mutex_lock_fake(pthread_mutex_t **uid) {
  int ret = 0;
  if (!*uid) {
    ret = pthread_mutex_init_fake(uid, NULL);
  } else if ((uintptr_t)*uid == 0x4000) {
    int attr = 1; // recursive
    ret = pthread_mutex_init_fake(uid, &attr);
  }
  if (ret < 0) return ret;
  return pthread_mutex_lock(*uid);
}

int pthread_mutex_unlock_fake(pthread_mutex_t **uid) {
  int ret = 0;
  if (!*uid) {
    ret = pthread_mutex_init_fake(uid, NULL);
  } else if ((uintptr_t)*uid == 0x4000) {
    int attr = 1; // recursive
    ret = pthread_mutex_init_fake(uid, &attr);
  }
  if (ret < 0) return ret;
  return pthread_mutex_unlock(*uid);
}

int sem_init_fake(int *uid) {
  *uid = sceKernelCreateSema("sema", 0, 0, 0x7fffffff, NULL);
  if (*uid < 0)
    return -1;
  return 0;
}

int sem_post_fake(int *uid) {
  if (sceKernelSignalSema(*uid, 1) < 0)
    return -1;
  return 0;
}

int sem_wait_fake(int *uid) {
  if (sceKernelWaitSema(*uid, 1, NULL) < 0)
    return -1;
  return 0;
}

int sem_trywait_fake(int *uid) {
  SceUInt timeout = 0;
  int res = sceKernelWaitSema(*uid, 1, &timeout);
  if (res < 0)
    return -1;
  return 0;
}

int sem_destroy_fake(int *uid) {
  if (sceKernelDeleteSema(*uid) < 0)
    return -1;
  return 0;
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *condattr) {
  pthread_cond_t *c = calloc(1, sizeof(pthread_cond_t));
  if (!c) return -1;

  *c = PTHREAD_COND_INITIALIZER;

  int ret = pthread_cond_init(c, NULL);
  if (ret < 0) {
    free(c);
    return -1;
  }

  *cnd = c;

  return 0;
}

int pthread_cond_broadcast_fake(pthread_cond_t **cnd) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_broadcast(*cnd);
}

int pthread_cond_signal_fake(pthread_cond_t **cnd) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  };
  return pthread_cond_signal(*cnd);
}

int pthread_cond_destroy_fake(pthread_cond_t **cnd) {
  if (cnd && *cnd) {
    pthread_cond_destroy(*cnd);
    free(*cnd);
    *cnd = NULL;
  }
  return 0;
}

int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_wait(*cnd, *mtx);
}

int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_timedwait(*cnd, *mtx, t);
}

int pthread_once_fake(volatile int *once_control, void (*init_routine) (void)) {
  if (!once_control || !init_routine)
    return -1;
  if (__sync_lock_test_and_set(once_control, 1) == 0)
    (*init_routine)();
  return 0;
}

// pthread_t is an unsigned int, so it should be fine
// TODO: probably shouldn't assume default attributes
int pthread_create_fake(pthread_t *thread, const void *unused, void *entry, void *arg) {
  return pthread_create(thread, NULL, entry, arg);
}

SceUID pigID;

EGLDisplay display = NULL;
EGLSurface surface = NULL;
EGLContext context = NULL;

EGLint surface_width, surface_height;

void NVEventEGLSwapBuffers() {
  eglSwapBuffers(display, surface);
}

void NVEventEGLMakeCurrent() {
}

void NVEventEGLUnmakeCurrent() {
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

FILE *fopen_hook(const char *filename, const char *mode) {
  FILE *file = fopen(filename, mode);
  // if (!file)
  //   debugPrintf("fopen %s: %p\n", filename, file);
  return file;
}

#define CLOCK_MONOTONIC 0

// from re3-vita
int clock_gettime(int clk_id, struct timespec *tp) {
  if (clk_id == CLOCK_MONOTONIC) {
    SceKernelSysClock ticks;
    sceKernelGetProcessTime(&ticks);
    tp->tv_sec = ticks/(1000*1000);
    tp->tv_nsec = (ticks * 1000) % (1000*1000*1000);
    return 0;
  } else if (clk_id == CLOCK_REALTIME) {
    time_t seconds;
    SceDateTime time;
    sceRtcGetCurrentClockLocalTime(&time);
    sceRtcGetTime_t(&time, &seconds);
    tp->tv_sec = seconds;
    tp->tv_nsec = time.microsecond * 1000;
    return 0;
  }

  return -ENOSYS;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
  // no way to implement this as far as I can tell
  rem->tv_sec = 0;
  rem->tv_nsec = 0;
  const uint32_t usec = req->tv_sec * 1000000 + req->tv_nsec / 1000;
  return sceKernelDelayThreadCB(usec);
}

int ReadDataFromPrivateStorage(const char *file, void **data, int *size) {
  char fullpath[1024];
  snprintf(fullpath, sizeof(fullpath), DATA_PATH "/%s", file);

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
  snprintf(fullpath, sizeof(fullpath), DATA_PATH "/%s", file);

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

void functions_patch() {
  hook_thumb(find_addr_by_symbol("__cxa_guard_acquire"), (uintptr_t)&__cxa_guard_acquire);
  hook_thumb(find_addr_by_symbol("__cxa_guard_release"), (uintptr_t)&__cxa_guard_release);

  // used for openal
  hook_thumb(find_addr_by_symbol("InitializeCriticalSection"), (uintptr_t)ret0);

  // used to check some flags
  hook_thumb(find_addr_by_symbol("_Z20OS_ServiceAppCommandPKcS0_"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z23OS_ServiceAppCommandIntPKci"), (uintptr_t)ret0);

  hook_thumb(find_addr_by_symbol("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority"), (uintptr_t)OS_ThreadLaunch);

  // this is checked on startup
  hook_thumb(find_addr_by_symbol("_Z25OS_ServiceIsWifiAvailablev"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z28OS_ServiceIsNetworkAvailablev"), (uintptr_t)ret0);

  hook_thumb(find_addr_by_symbol("_Z18OS_ServiceOpenLinkPKc"), (uintptr_t)ret0);

  // don't have movie playback yet
  hook_thumb(find_addr_by_symbol("_Z12OS_MoviePlayPKcbbf"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z12OS_MovieStopv"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z20OS_MovieSetSkippableb"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z17OS_MovieTextScalei"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z17OS_MovieIsPlayingPi"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z20OS_MoviePlayinWindowPKciiiibbf"), (uintptr_t)ret0);

  // egl
  hook_thumb(find_addr_by_symbol("_Z14NVEventEGLInitv"), (uintptr_t)NVEventEGLInit);
  hook_thumb(find_addr_by_symbol("_Z21NVEventEGLMakeCurrentv"), (uintptr_t)NVEventEGLMakeCurrent);
  hook_thumb(find_addr_by_symbol("_Z23NVEventEGLUnmakeCurrentv"), (uintptr_t)NVEventEGLUnmakeCurrent);
  hook_thumb(find_addr_by_symbol("_Z21NVEventEGLSwapBuffersv"), (uintptr_t)NVEventEGLSwapBuffers);

  // TODO: implement touch here
  hook_thumb(find_addr_by_symbol("_Z13ProcessEventsb"), (uintptr_t)ProcessEvents);

  hook_thumb(find_addr_by_symbol("_Z24NVThreadGetCurrentJNIEnvv"), (uintptr_t)0x1337);

  // uint16_t bkpt = 0xbe00;
  // kuKernelCpuUnrestrictedMemcpy(text_base + 0x00194968, &bkpt, 2);

  hook_thumb(find_addr_by_symbol("_Z25GetAndroidCurrentLanguagev"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z9NvAPKOpenPKc"), (uintptr_t)ret0);

  hook_thumb(find_addr_by_symbol("_Z17OS_ScreenGetWidthv"), (uintptr_t)OS_ScreenGetWidth);
  hook_thumb(find_addr_by_symbol("_Z18OS_ScreenGetHeightv"), (uintptr_t)OS_ScreenGetHeight);

  hook_thumb(find_addr_by_symbol("_Z14AND_DeviceTypev"), (uintptr_t)AND_DeviceType);
  hook_thumb(find_addr_by_symbol("_Z16AND_DeviceLocalev"), (uintptr_t)AND_DeviceLocale);
  hook_thumb(find_addr_by_symbol("_Z20AND_SystemInitializev"), (uintptr_t)AND_SystemInitialize);
  hook_thumb(find_addr_by_symbol("_Z21AND_ScreenSetWakeLockb"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z22AND_FileGetArchiveName13OSFileArchive"), (uintptr_t)OS_FileGetArchiveName);


  hook_thumb(find_addr_by_symbol("_Z26ReadDataFromPrivateStoragePKcRPcRi"), (uintptr_t)ReadDataFromPrivateStorage);
  hook_thumb(find_addr_by_symbol("_Z25WriteDataToPrivateStoragePKcS0_i"), (uintptr_t)WriteDataToPrivateStorage);

  hook_thumb(find_addr_by_symbol("_Z25WarGamepad_GetGamepadTypei"), (uintptr_t)WarGamepad_GetGamepadType);
  hook_thumb(find_addr_by_symbol("_Z28WarGamepad_GetGamepadButtonsi"), (uintptr_t)WarGamepad_GetGamepadButtons);
  hook_thumb(find_addr_by_symbol("_Z25WarGamepad_GetGamepadAxisii"), (uintptr_t)WarGamepad_GetGamepadAxis);

  hook_thumb(find_addr_by_symbol("_Z12VibratePhonei"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z14Mobile_Vibratei"), (uintptr_t)ret0);

  // no touchsense
  hook_thumb(find_addr_by_symbol("_ZN10TouchSenseC2Ev"), (uintptr_t)ret0);

  // stub out framebuffer effects, they cause cardiac arrest in the renderer under certain circumstances
  hook_thumb(find_addr_by_symbol("_Z19SafeBindFrameBufferj"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z11DrawPPImageR11P_ES2Shaderjjjbjj"), (uintptr_t)ret0);
  hook_thumb(find_addr_by_symbol("_Z10ApplyBloomv"), (uintptr_t)ret0);

  hook_thumb(find_addr_by_symbol("_Z15ExitAndroidGamev"), (uintptr_t)ExitAndroidGame);
}

char *__ctype_ = (char *)&_ctype_;

// this is supposed to be an array of FILEs, which have a different size in libMaxPayne
// instead use it to determine whether it's trying to print to stdout/stderr
uint8_t fake_sF[0x300]; // stdout, stderr, stdin

static inline FILE *get_actual_stream(uint8_t *stream) {
  if ((uintptr_t)stream == 0x1337 || (stream >= fake_sF && stream < fake_sF + 0x300))
    return stdout;
  return (FILE *)stream;
}

int fprintf_hook(uint8_t *stream, const char *fmt, ...) {
  FILE *f = get_actual_stream(stream);
  if (!f) return -1;

  va_list list;
  va_start(list, fmt);
  int ret = vfprintf(f, fmt, list);
  va_end(list);

  return ret;
}

int fputc_hook(int ch, uint8_t *stream) {
  return fputc(ch, get_actual_stream(stream));
}

int fputs_hook(const char *s, uint8_t *stream) {
  return fputs(s, get_actual_stream(stream));
}

int EnterGameFromSCFunc = 0;
int SigningOutfromApp = 0;

int __stack_chk_guard_fake = 0x42424242;

void glGetIntegervHook(GLenum pname, GLint *data) {
  glGetIntegerv(pname, data);
  if (pname == GL_MAX_VERTEX_UNIFORM_VECTORS)
    *data = (63 * 3) + 32; // piglet hardcodes 128! need this for the bones
}

// Piglet does not use softfp, so we need to write some wrappers

__attribute__((naked)) void glClearColorWrapper(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  asm volatile (
    "vmov s0, r0\n"
    "vmov s1, r1\n"
    "vmov s2, r2\n"
    "vmov s3, r3\n"
    "b glClearColor\n"
  );
}

__attribute__((naked)) void glClearDepthfWrapper(GLfloat d) {
  asm volatile (
    "vmov s0, r0\n"
    "b glClearDepthf\n"
  );
}

__attribute__((naked)) void glDepthRangefWrapper(GLfloat near, GLfloat far) {
  asm volatile (
    "vmov s0, r0\n"
    "vmov s1, r1\n"
    "b glDepthRangef\n"
  );
}

__attribute__((naked)) void glUniform1fWrapper(GLint location, GLfloat v) {
  asm volatile (
    "vmov s0, r1\n"
    "b glUniform1f\n"
  );
}

__attribute__((naked)) void glUniform3fWrapper(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  asm volatile (
    "vmov s0, r1\n"
    "vmov s1, r2\n"
    "vmov s2, r3\n"
    "b glUniform3f\n"
  );
}

__attribute__((naked)) void glPolygonOffsetWrapper(GLfloat factor, GLfloat units) {
  asm volatile (
    "vmov s0, r0\n"
    "vmov s1, r1\n"
    "b glPolygonOffset\n"
  );
}

__attribute__((naked)) void glTexParameterfWrapper(GLenum target, GLenum pname, GLfloat param) {
  asm volatile (
    "vmov s0, r2\n"
    "b glTexParameterf\n"
  );
}

// Fails for:
// 35841: GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG
// 35842: GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
void glCompressedTexImage2DHook(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * data) {
  if (!level)
    glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

void glTexImage2DHook(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void * data) {
  if (!level)
    glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
}

void glGetShaderInfoLogHook(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
  static char srcbuf[0x2000];
  GLsizei len = 0;
  glGetShaderSource(shader, sizeof(srcbuf), &len, srcbuf);
  if (len > 0) debugPrintf("\nshader source:\n%s\n", srcbuf);
  glGetShaderInfoLog(shader, maxLength, length, infoLog);
  debugPrintf("shader info log:\n%s\n", infoLog);
}

void glGetProgramInfoLogHook(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
  glGetProgramInfoLog(program, maxLength, length, infoLog);
  debugPrintf("program info log:\n%s\n", infoLog);
}

void glViewportHook(GLint x, GLint y, GLsizei w, GLsizei h) {
  // HACK: the game really wants to force 16:9, so it centers a 960x540 viewport
  if (w == 960 && h == 540) {
    h = 544;
    y = 0;
  }
  glViewport(x, y, w, h);
}

FILE *stderr_fake = (FILE *)0x1337;

typedef struct {
  char *symbol;
  uintptr_t func;
  int patched;
} DynLibFunction;

DynLibFunction dynlib_functions[] = {
  { "__sF", (uintptr_t)&fake_sF },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },

  { "stderr", (uintptr_t)&stderr_fake },

  { "__aeabi_memclr", (uintptr_t)&__aeabi_memclr },
  { "__aeabi_memclr8", (uintptr_t)&__aeabi_memclr8 },
  { "__aeabi_memcpy", (uintptr_t)&__aeabi_memcpy },
  { "__aeabi_memcpy4", (uintptr_t)&__aeabi_memcpy4 },
  { "__aeabi_memmove", (uintptr_t)&__aeabi_memmove },
  { "__aeabi_memmove4", (uintptr_t)&__aeabi_memmove4 },
  { "__aeabi_memset", (uintptr_t)&__aeabi_memset },

  { "AAssetManager_open", (uintptr_t)&ret0 },
  { "AAssetManager_fromJava", (uintptr_t)&ret0 },
  { "AAsset_close", (uintptr_t)&ret0 },
  { "AAsset_getLength", (uintptr_t)&ret0 },
  { "AAsset_getRemainingLength", (uintptr_t)&ret0 },
  { "AAsset_read", (uintptr_t)&ret0 },
  { "AAsset_seek", (uintptr_t)&ret0 },

  // Not sure how important this is. Used in some init_array.
  { "pthread_key_create", (uintptr_t)&ret0 },
  { "pthread_key_delete", (uintptr_t)&ret0 },

  { "pthread_getspecific", (uintptr_t)&ret0 },
  { "pthread_setspecific", (uintptr_t)&ret0 },

  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake },
  { "pthread_cond_init", (uintptr_t)&pthread_cond_init_fake },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_fake },
  { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_fake },
  { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_fake },

  { "pthread_create", (uintptr_t)&pthread_create_fake },
  { "pthread_join", (uintptr_t)&pthread_join },
  { "pthread_self", (uintptr_t)&pthread_self },

  { "pthread_setschedparam", (uintptr_t)&ret0 },

  { "pthread_mutexattr_init", (uintptr_t)&ret0 },
  { "pthread_mutexattr_settype", (uintptr_t)&ret0 },
  { "pthread_mutexattr_destroy", (uintptr_t)&ret0 },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake },

  { "pthread_once", (uintptr_t)&pthread_once_fake },

  { "sched_get_priority_min", (uintptr_t)&retm1 },

  { "sem_destroy", (uintptr_t)&sem_destroy_fake },
  // { "sem_getvalue", (uintptr_t)&sem_getvalue },
  { "sem_init", (uintptr_t)&sem_init_fake },
  { "sem_post", (uintptr_t)&sem_post_fake },
  { "sem_trywait", (uintptr_t)&sem_trywait_fake },
  { "sem_wait", (uintptr_t)&sem_wait_fake },

  { "GetRockstarID", (uintptr_t)&GetRockstarID },

  { "__android_log_print", (uintptr_t)__android_log_print },

  { "__errno", (uintptr_t)&__errno },

  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  // freezes with real __stack_chk_guard
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },

  { "_ctype_", (uintptr_t)&__ctype_ },

   // TODO: use math neon?
  { "acos", (uintptr_t)&acos },
  { "acosf", (uintptr_t)&acosf },
  { "asinf", (uintptr_t)&asinf },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  { "exp", (uintptr_t)&exp },
  { "floor", (uintptr_t)&floor },
  { "floorf", (uintptr_t)&floorf },
  { "fmod", (uintptr_t)&fmod },
  { "fmodf", (uintptr_t)&fmodf },
  { "log", (uintptr_t)&log },
  { "log10f", (uintptr_t)&log10f },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "sin", (uintptr_t)&sin },
  { "sinf", (uintptr_t)&sinf },
  { "tan", (uintptr_t)&tan },
  { "tanf", (uintptr_t)&tanf },
  { "sqrt", (uintptr_t)&sqrt },
  { "sqrtf", (uintptr_t)&sqrtf },

  { "atoi", (uintptr_t)&atoi },
  { "atof", (uintptr_t)&atof },
  { "isspace", (uintptr_t)&isspace },
  { "tolower", (uintptr_t)&tolower },
  { "towlower", (uintptr_t)&towlower },
  { "toupper", (uintptr_t)&toupper },
  { "towupper", (uintptr_t)&towupper },

  { "calloc", (uintptr_t)&calloc },
  { "free", (uintptr_t)&free },
  { "malloc", (uintptr_t)&malloc },
  { "realloc", (uintptr_t)&realloc },

  { "clock_gettime", (uintptr_t)&clock_gettime },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  { "time", (uintptr_t)&time },
  { "asctime", (uintptr_t)&asctime },
  { "localtime", (uintptr_t)&localtime },
  { "localtime_r", (uintptr_t)&localtime_r },
  { "strftime", (uintptr_t)&strftime },

  // { "eglGetDisplay", (uintptr_t)&eglGetDisplay },
  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
  // { "eglQueryString", (uintptr_t)&eglQueryString },

  { "abort", (uintptr_t)&abort },
  { "exit", (uintptr_t)&exit },

  { "fopen", (uintptr_t)&fopen_hook },
  { "fclose", (uintptr_t)&fclose },
  { "fdopen", (uintptr_t)&fdopen },
  { "fflush", (uintptr_t)&fflush },
  { "fgetc", (uintptr_t)&fgetc },
  { "fgets", (uintptr_t)&fgets },
  { "fputs", (uintptr_t)&fputs_hook },
  { "fputc", (uintptr_t)&fputc_hook },
  { "fprintf", (uintptr_t)&fprintf_hook },
  { "fread", (uintptr_t)&fread },
  { "fseek", (uintptr_t)&fseek },
  { "ftell", (uintptr_t)&ftell },
  { "fwrite", (uintptr_t)&fwrite },
  { "fstat", (uintptr_t)&fstat },
  { "ferror", (uintptr_t)&ferror },
  { "feof", (uintptr_t)&feof },
  { "setvbuf", (uintptr_t)&setvbuf },

  { "getenv", (uintptr_t)&getenv },
  // { "gettid", (uintptr_t)&gettid },

  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
  { "glBindBuffer", (uintptr_t)&glBindBuffer },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate },
  { "glBufferData", (uintptr_t)&glBufferData },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
  { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColorWrapper },
  { "glClearDepthf", (uintptr_t)&glClearDepthfWrapper },
  { "glClearStencil", (uintptr_t)&glClearStencil },
  { "glCompileShader", (uintptr_t)&glCompileShader },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2DHook },
  { "glCreateProgram", (uintptr_t)&glCreateProgram },
  { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCullFace", (uintptr_t)&glCullFace },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
  { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
  { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask },
  { "glDepthRangef", (uintptr_t)&glDepthRangefWrapper },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
  { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFinish", (uintptr_t)&glFinish },
  { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
  { "glFrontFace", (uintptr_t)&glFrontFace },
  { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
  { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
  { "glGetError", (uintptr_t)&glGetError },
  { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLogHook },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
  { "glGetString", (uintptr_t)&glGetString },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glHint", (uintptr_t)&glHint },
  { "glLinkProgram", (uintptr_t)&glLinkProgram },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffsetWrapper },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage },
  { "glScissor", (uintptr_t)&glScissor },
  { "glShaderSource", (uintptr_t)&glShaderSource },
  { "glTexImage2D", (uintptr_t)&glTexImage2DHook },
  { "glTexParameterf", (uintptr_t)&glTexParameterfWrapper },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glUniform1f", (uintptr_t)&glUniform1fWrapper },
  { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform3f", (uintptr_t)&glUniform3fWrapper },
  { "glUniform3fv", (uintptr_t)&glUniform3fv },
  { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttrib4fv", (uintptr_t)&glVertexAttrib4fv },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glViewport", (uintptr_t)&glViewportHook },

  // this only uses setjmp in the JPEG loader but not longjmp
  // probably doesn't matter if they're compatible or not
  { "longjmp", (uintptr_t)&longjmp },
  { "setjmp", (uintptr_t)&setjmp },

  { "memcmp", (uintptr_t)&memcmp },
  { "wmemcmp", (uintptr_t)&wmemcmp },
  { "memcpy", (uintptr_t)&memcpy_neon },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "memchr", (uintptr_t)&memchr },

  { "printf", (uintptr_t)&debugPrintf },

  { "bsearch", (uintptr_t)&bsearch },
  { "qsort", (uintptr_t)&qsort },

  // { "raise", (uintptr_t)&raise },
  // { "rewind", (uintptr_t)&rewind },

  // { "scmainUpdate", (uintptr_t)&scmainUpdate },
  // { "slCreateEngine", (uintptr_t)&slCreateEngine },

  { "snprintf", (uintptr_t)&snprintf },
  { "sprintf", (uintptr_t)&sprintf },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf },

  { "sscanf", (uintptr_t)&sscanf },

  { "close", (uintptr_t)&close },
  // { "closedir", (uintptr_t)&closedir },
  { "lseek", (uintptr_t)&lseek },
  { "mkdir", (uintptr_t)&mkdir },
  { "open", (uintptr_t)&open },
  // { "opendir", (uintptr_t)&opendir },
  { "read", (uintptr_t)&read },
  // { "readdir", (uintptr_t)&readdir },
  { "stat", (uintptr_t)stat },
  { "write", (uintptr_t)&write },

  { "strcasecmp", (uintptr_t)&strcasecmp },
  { "strcat", (uintptr_t)&strcat },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcoll", (uintptr_t)&strcoll },
  { "strcpy", (uintptr_t)&strcpy },
  { "stpcpy", (uintptr_t)&stpcpy },
  { "strerror", (uintptr_t)&strerror },
  { "strlen", (uintptr_t)&strlen },
  { "strncasecmp", (uintptr_t)&strncasecmp },
  { "strncat", (uintptr_t)&strncat },
  { "strncmp", (uintptr_t)&strncmp },
  { "strncpy", (uintptr_t)&strncpy },
  { "strpbrk", (uintptr_t)&strpbrk },
  { "strrchr", (uintptr_t)&strrchr },
  { "strstr", (uintptr_t)&strstr },
  { "strtod", (uintptr_t)&strtod },
  { "strtok", (uintptr_t)&strtok },
  { "strtol", (uintptr_t)&strtol },
  { "strtoul", (uintptr_t)&strtoul },
  { "strtof", (uintptr_t)&strtof },
  { "strxfrm", (uintptr_t)&strxfrm },

  { "srand", (uintptr_t)&srand },
  { "rand", (uintptr_t)&rand },

  // { "syscall", (uintptr_t)&syscall },
  // { "sysconf", (uintptr_t)&sysconf },

  { "nanosleep", (uintptr_t)&nanosleep },
  { "usleep", (uintptr_t)&usleep },

  { "wctob", (uintptr_t)&wctob },
  { "wctype", (uintptr_t)&wctype },
  { "wcsxfrm", (uintptr_t)&wcsxfrm },
  { "iswctype", (uintptr_t)&iswctype },
  { "wcscoll", (uintptr_t)&wcscoll },
  { "wcsftime", (uintptr_t)&wcsftime },
  { "mbrtowc", (uintptr_t)&mbrtowc },
  { "wcrtomb", (uintptr_t)&wcrtomb },
  { "wcslen", (uintptr_t)&wcslen },
  { "btowc", (uintptr_t)&btowc },
};

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

int main() {
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  pibInit(PIB_SHACCCG | PIB_GET_PROC_ADDR_CORE);

  void *so_data, *prog_data;
  SceUID so_blockid, prog_blockid;

  so_blockid = load_file(LIB_PATH, &so_data);

  Elf32_Ehdr *elf_hdr = (Elf32_Ehdr *)so_data;
  Elf32_Phdr *prog_hdrs = (Elf32_Phdr *)((uintptr_t)so_data + elf_hdr->e_phoff);
  Elf32_Shdr *sec_hdrs = (Elf32_Shdr *)((uintptr_t)so_data + elf_hdr->e_shoff);

  prog_data = (void *)0x98000000;

  for (int i = 0; i < elf_hdr->e_phnum; i++) {
    if (prog_hdrs[i].p_type == PT_LOAD) {
      uint32_t prog_size = ALIGN_MEM(prog_hdrs[i].p_memsz, prog_hdrs[i].p_align);

      if ((prog_hdrs[i].p_flags & PF_X) == PF_X) {
        SceKernelAllocMemBlockKernelOpt opt;
        memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
        opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
        opt.attr = 0x1;
        opt.field_C = (SceUInt32)(prog_data);
        prog_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, prog_size, &opt);

        sceKernelGetMemBlockBase(prog_blockid, &prog_data);

        prog_hdrs[i].p_vaddr += (Elf32_Addr)prog_data;

        text_base = (void *)prog_hdrs[i].p_vaddr;
        text_size = prog_size;
      } else {
        SceKernelAllocMemBlockKernelOpt opt;
        memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
        opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
        opt.attr = 0x1;
        opt.field_C = (SceUInt32)(text_base + text_size);
        prog_blockid = kuKernelAllocMemBlock("rw_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, prog_size, &opt);

        sceKernelGetMemBlockBase(prog_blockid, &prog_data);

        prog_hdrs[i].p_vaddr += (Elf32_Addr)text_base;

        data_base = (void *)prog_hdrs[i].p_vaddr;
        data_size = prog_size;
      }

      char *zero = malloc(prog_size);
      memset(zero, 0, prog_size);
      kuKernelCpuUnrestrictedMemcpy(prog_data, zero, prog_size);
      free(zero);

      kuKernelCpuUnrestrictedMemcpy((void *)prog_hdrs[i].p_vaddr, (void *)((uintptr_t)so_data + prog_hdrs[i].p_offset), prog_hdrs[i].p_filesz);
    }
  }

  char *shstrtab = (char *)((uintptr_t)so_data + sec_hdrs[elf_hdr->e_shstrndx].sh_offset);

  int dynsyn_idx = -1;

  for (int i = 0; i < elf_hdr->e_shnum; i++) {
    char *sh_name = shstrtab + sec_hdrs[i].sh_name;

    if (strcmp(sh_name, ".dynsym") == 0) {
      dynsyn_idx = i;
      syms = (Elf32_Sym *)((uintptr_t)text_base + sec_hdrs[dynsyn_idx].sh_addr);
      n_syms = sec_hdrs[dynsyn_idx].sh_size / sizeof(Elf32_Sym);
    } else if (strcmp(sh_name, ".dynstr") == 0) {
      dynstrtab = (char *)((uintptr_t)text_base + sec_hdrs[i].sh_addr);
    } else if (strcmp(sh_name, ".rel.dyn") == 0 || strcmp(sh_name, ".rel.plt") == 0) {
      Elf32_Rel *rels = (Elf32_Rel *)((uintptr_t)text_base + sec_hdrs[i].sh_addr);
      int n_rels = sec_hdrs[i].sh_size / sizeof(Elf32_Rel);

      for (int j = 0; j < n_rels; j++) {
        uint32_t *ptr = (uint32_t *)(text_base + rels[j].r_offset);
        int sym_idx = ELF32_R_SYM(rels[j].r_info);
        Elf32_Sym *sym = &syms[sym_idx];

        switch (ELF32_R_TYPE(rels[j].r_info)) {
          case R_ARM_ABS32:
          {
            *ptr += (uintptr_t)text_base + sym->st_value;
            break;
          }

          case R_ARM_RELATIVE:
          {
            *ptr += (uintptr_t)text_base;
            break;
          }

          case R_ARM_GLOB_DAT:
          case R_ARM_JUMP_SLOT:
          {
            if (sym->st_shndx != SHN_UNDEF) {
              *ptr = (uintptr_t)text_base + sym->st_value;
              break;
            }

            // make it crash for debugging
            *ptr = rels[j].r_offset;

            char *name = dynstrtab + sym->st_name;

            for (int k = 0; k < sizeof(dynlib_functions) / sizeof(DynLibFunction); k++) {
              if (strcmp(name, dynlib_functions[k].symbol) == 0) {
                *ptr = dynlib_functions[k].func;
                dynlib_functions[k].patched = 1;
                break;
              }
            }

            if (*ptr == rels[j].r_offset)
              debugPrintf("unsatisfied: %s\n", name);

            break;
          }

          default:
            debugPrintf("Unknown relocation type: %x\n", ELF32_R_TYPE(rels[j].r_info));
            break;
        }
      }
    }
  }

  for (int k = 0; k < sizeof(dynlib_functions) / sizeof(*dynlib_functions); ++k)
    if (!dynlib_functions[k].patched)
      printf("unneeded import: %s\n", dynlib_functions[k].symbol);

  openal_patch();
  opengl_patch();
  functions_patch();

  stderr_fake = stderr;

  kuKernelFlushCaches(text_base, text_size);

  for (int i = 0; i < elf_hdr->e_shnum; i++) {
    char *sh_name = shstrtab + sec_hdrs[i].sh_name;

    if (strcmp(sh_name, ".init_array") == 0) {
      int (** init_array)() = (void *)((uintptr_t)text_base + sec_hdrs[i].sh_addr);
      int n_array = sec_hdrs[i].sh_size / 4;
      for (int j = 0; j < n_array; j++) {
        if (init_array[j] != 0)
          init_array[j]();
      }
    }
  }

  sceKernelFreeMemBlock(so_blockid);

  // won't save without it
  mkdir(DATA_PATH "/savegames", 0777);

  deviceChip = (int *)find_addr_by_symbol("deviceChip");
  deviceForm = (int *)find_addr_by_symbol("deviceForm");
  definedDevice = (int *)find_addr_by_symbol("definedDevice");

  strcpy((char *)find_addr_by_symbol("StorageRootBuffer"), DATA_PATH);
  *(uint8_t *)find_addr_by_symbol("IsAndroidPaused") = 0;

  uint32_t (* initGraphics)(void) = (void *)find_addr_by_symbol("_Z12initGraphicsv");
  initGraphics();

  uint32_t (* ShowJoystick)(int show) = (void *)find_addr_by_symbol("_Z12ShowJoystickb");
  ShowJoystick(0);

  int (* NVEventAppMain)(int argc, char *argv[]) = (void *)find_addr_by_symbol("_Z14NVEventAppMainiPPc");
  NVEventAppMain(0, NULL);

  return 0;
}
