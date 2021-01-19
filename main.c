/*
  TODO:
  - Fix street lamps
  - Fix hud elements (touch buttons)
    - Some compressed textures are not supported
  - Implement touch
  - Use math neon
  - Use 4th core
  - Optimize bones matrix
*/

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

#define MEMORY_MB 256

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

int debugPrintf(char *text, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, text);
  vsprintf(string, text, list);
  va_end(list);

  SceUID fd = sceIoOpen("ux0:data/max_log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
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

void __aeabi_atexit() {
  return;
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

char *GetRockstarID() {
  return "flow";
}

int OS_SystemChip() {
  return 0;
}

int AND_DeviceType() {
  // 0x1: phone
  // 0x2: tegra
  // low memory is < 256
  return (MEMORY_MB << 6) | (3 << 2) | 0x1;
}

int AND_DeviceLocale() {
  return 0; // english
}

int OS_ScreenGetHeight() {
  return 544;
}

int OS_ScreenGetWidth() {
  return 960;
}

int ProcessEvents() {
  return 0; // 1 is exit!
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

int pthread_cond_timeout_np_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, uint32_t msec) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }

  struct timeval now;
  struct timespec abst = { 0 };
  gettimeofday(&now, NULL);
  abst.tv_sec = now.tv_sec + (msec / 1000);
  abst.tv_nsec = (now.tv_usec + (msec % 1000) * 1000) * 1000;

  return pthread_cond_timedwait(*cnd, *mtx, &abst);
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

int thread_stub(SceSize args, int *argp) {
  int (* func)(void *arg) = (void *)argp[0];
  void *arg = (void *)argp[1];
  char *out = (char *)argp[2];
  out[0x41] = 1; // running
  return func(arg);
}

// OS_ThreadLaunch CdStream with priority 6b
// OS_ThreadLaunch Es2Thread with priority 40
// OS_ThreadLaunch MainThread with priority 5a
// OS_ThreadLaunch BankLoader with priority bf
// OS_ThreadLaunch StreamThread with priority 6b
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

#define APK_PATH "main.obb"

char *OS_FileGetArchiveName(int mode) {
  char *out = malloc(strlen(APK_PATH) + 1);
  out[0] = '\0';
  if (mode == 1) // main
    strcpy(out, APK_PATH);
  return out;
}

FILE *fopen_hook(const char *filename, const char *mode) {
  FILE *file = fopen(filename, mode);
  if (!file)
    debugPrintf("fopen failed for %s\n", filename);
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

void
matmul4_neon(float m0[16], float m1[16], float d[16])
{
  asm volatile (
    "vld1.32  {d0, d1},   [%1]!\n\t" //q0 = m1
    "vld1.32  {d2, d3},   [%1]!\n\t" //q1 = m1+4
    "vld1.32  {d4, d5},   [%1]!\n\t" //q2 = m1+8
    "vld1.32  {d6, d7},   [%1]\n\t"  //q3 = m1+12
    "vld1.32  {d16, d17}, [%0]!\n\t" //q8 = m0
    "vld1.32  {d18, d19}, [%0]!\n\t" //q9 = m0+4
    "vld1.32  {d20, d21}, [%0]!\n\t" //q10 = m0+8
    "vld1.32  {d22, d23}, [%0]\n\t"  //q11 = m0+12

    "vmul.f32 q12, q8,  d0[0]\n\t"   //q12 = q8 * d0[0]
    "vmul.f32 q13, q8,  d2[0]\n\t"   //q13 = q8 * d2[0]
    "vmul.f32 q14, q8,  d4[0]\n\t"   //q14 = q8 * d4[0]
    "vmul.f32 q15, q8,  d6[0]\n\t"   //q15 = q8 * d6[0]
    "vmla.f32 q12, q9,  d0[1]\n\t"   //q12 = q9 * d0[1]
    "vmla.f32 q13, q9,  d2[1]\n\t"   //q13 = q9 * d2[1]
    "vmla.f32 q14, q9,  d4[1]\n\t"   //q14 = q9 * d4[1]
    "vmla.f32 q15, q9,  d6[1]\n\t"   //q15 = q9 * d6[1]
    "vmla.f32 q12, q10, d1[0]\n\t"   //q12 = q10 * d0[0]
    "vmla.f32 q13, q10, d3[0]\n\t"   //q13 = q10 * d2[0]
    "vmla.f32 q14, q10, d5[0]\n\t"   //q14 = q10 * d4[0]
    "vmla.f32 q15, q10, d7[0]\n\t"   //q15 = q10 * d6[0]
    "vmla.f32 q12, q11, d1[1]\n\t"   //q12 = q11 * d0[1]
    "vmla.f32 q13, q11, d3[1]\n\t"   //q13 = q11 * d2[1]
    "vmla.f32 q14, q11, d5[1]\n\t"   //q14 = q11 * d4[1]
    "vmla.f32 q15, q11, d7[1]\n\t"   //q15 = q11 * d6[1]

    "vst1.32  {d24, d25}, [%2]!\n\t"  //d = q12
    "vst1.32  {d26, d27}, [%2]!\n\t"  //d+4 = q13
    "vst1.32  {d28, d29}, [%2]!\n\t"  //d+8 = q14
    "vst1.32  {d30, d31}, [%2]\n\t"   //d+12 = q15

    : "+r"(m0), "+r"(m1), "+r"(d) :
    : "q0", "q1", "q2", "q3", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15",
    "memory"
  );
}

void *(* GetCurrentProjectionMatrix)();

void SetMatrixConstant(void *ES2Shader, int MatrixConstantID, float *matrix) {
#ifdef MVP_OPTIMIZATION
  if (MatrixConstantID == 0) { // Projection matrix
    void *MvpMatrix = ES2Shader + 0x4C * 0;
    float *MvpMatrixData = MvpMatrix + 0x2AC;
    float *MvMatrixData = (ES2Shader + 0x4C * 1) + 0x2AC;
    matmul4_neon(matrix, MvMatrixData, MvpMatrixData);
    *(uint8_t *)(MvpMatrix + 0x2EC) = 1;
    *(uint8_t *)(MvpMatrix + 0x2A8) = 1;
    return;
  } else if (MatrixConstantID == 1) { // Model view matrix
    float *ProjMatrix = (float *)GetCurrentProjectionMatrix();
    // There will be no fresher ProjMatrix, so we should update MvpMatrix as well.
    if (((uint8_t *)ProjMatrix)[64] == 0) {
      void *MvpMatrix = ES2Shader + 0x4C * 0;
      float *MvpMatrixData = MvpMatrix + 0x2AC;
      matmul4_neon(ProjMatrix, matrix, MvpMatrixData);
      *(uint8_t *)(MvpMatrix + 0x2EC) = 1;
      *(uint8_t *)(MvpMatrix + 0x2A8) = 1;
    }
  }
#endif

  void *UniformMatrix = ES2Shader + 0x4C * MatrixConstantID;
  float *UniformMatrixData = UniformMatrix + 0x2AC;

  // That check is so useless IMO. If you need to go through both matrices anways, why just don't copy.
  // if (memcmp(UniformMatrixData, matrix, 16 * 4) != 0) {
    memcpy_neon(UniformMatrixData, matrix, 16 * 4);
    *(uint8_t *)(UniformMatrix + 0x2EC) = 1;
    *(uint8_t *)(UniformMatrix + 0x2A8) = 1;
  // }
}

void functions_patch() {
  // egl
  hook_arm(find_addr_by_symbol("_Z14NVEventEGLInitv"), (uintptr_t)NVEventEGLInit);
  hook_arm(find_addr_by_symbol("_Z21NVEventEGLMakeCurrentv"), (uintptr_t)NVEventEGLMakeCurrent);
  hook_arm(find_addr_by_symbol("_Z23NVEventEGLUnmakeCurrentv"), (uintptr_t)NVEventEGLUnmakeCurrent);
  hook_arm(find_addr_by_symbol("_Z21NVEventEGLSwapBuffersv"), (uintptr_t)NVEventEGLSwapBuffers);

  // TODO: implement touch here
  hook_arm(find_addr_by_symbol("_Z13ProcessEventsb"), (uintptr_t)ProcessEvents);

  // hook_thumb(find_addr_by_symbol("_Z24NVThreadGetCurrentJNIEnvv"), (uintptr_t)0x1337);

  // do not check result of CFileMgr::OpenFile in CWaterLevel::WaterLevelInitialise
  uint32_t nop = 0xbf00bf00;
  kuKernelCpuUnrestrictedMemcpy(text_base + 0x004D7A2A, &nop, 2);

  // uint16_t bkpt = 0xbe00;
  // kuKernelCpuUnrestrictedMemcpy(text_base + 0x00194968, &bkpt, 2);
}

extern int _Znwj;
extern int _ZdlPv;
extern int _Znaj;
extern int _ZdaPv;
extern int __dso_handle;

extern int __cxa_atexit;
extern int __gnu_unwind_frame;

// extern int __aeabi_atexit;

extern int __aeabi_dcmplt;
extern int __aeabi_dmul;
extern int __aeabi_dsub;
extern int __aeabi_idiv;
extern int __aeabi_idivmod;
extern int __aeabi_l2d;
extern int __aeabi_l2f;
extern int __aeabi_ldivmod;
extern int __aeabi_ui2d;
extern int __aeabi_uidiv;
extern int __aeabi_uidivmod;
extern int __aeabi_ul2d;
extern int __aeabi_ul2f;
extern int __aeabi_uldivmod;
extern int __aeabi_d2f;
extern int __aeabi_d2uiz;
extern int __aeabi_dadd;
extern int __aeabi_dcmpeq;
extern int __aeabi_dcmpgt;
extern int __aeabi_ddiv;
extern int __aeabi_f2d;
extern int __aeabi_fcmpeq;
extern int __aeabi_fcmpgt;
extern int __aeabi_fcmpun;

// extern int __assert2;
// extern int __errno;

extern int __isfinitef;

extern int __stack_chk_fail;
extern int __stack_chk_guard;

extern int _Unwind_Complete;
extern int _Unwind_DeleteException;
extern int _Unwind_GetDataRelBase;
extern int _Unwind_GetLanguageSpecificData;
extern int _Unwind_GetRegionStart;
extern int _Unwind_GetTextRelBase;
extern int _Unwind_RaiseException;
extern int _Unwind_Resume;
extern int _Unwind_Resume_or_Rethrow;
extern int _Unwind_VRS_Get;
extern int _Unwind_VRS_Set;

static const short _C_toupper_[] = {
  -1,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
  0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
  0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
  0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
  0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
  0x60, 'A',  'B',  'C',  'D',  'E',  'F',  'G',
  'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
  'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
  'X',  'Y',  'Z',  0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
  0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
  0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static const short _C_tolower_[] = {
  -1,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
  0x40, 'a',  'b',  'c',  'd',  'e',  'f',  'g',
  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
  'x',  'y',  'z',  0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
  0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
  0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
  0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
  0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

const short *_toupper_tab_ = _C_toupper_;
const short *_tolower_tab_ = _C_tolower_;

// this is supposed to be an array of FILEs, which have a different size in libMaxPayne
// instead use it to determine whether it's trying to print to stdout/stderr
#define MP_FILE_STRUCT_SIZE 0x54
uint8_t fake_sF[MP_FILE_STRUCT_SIZE * 3]; // stdout, stderr, stdin

static inline FILE *get_actual_stream(uint8_t *stream) {
  if (stream >= fake_sF && stream < fake_sF + MP_FILE_STRUCT_SIZE * 3)
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

typedef struct {
  char *symbol;
  uintptr_t func;
  int patched;
} DynLibFunction;

DynLibFunction dynlib_functions[] = {
  { "_Znwj", (uintptr_t)&_Znwj },
  { "_ZdlPv", (uintptr_t)&_ZdlPv },
  { "_Znaj", (uintptr_t)&_Znaj },
  { "_ZdaPv", (uintptr_t)&_ZdaPv },
  { "__dso_handle", (uintptr_t)&__dso_handle },
  { "__sF", (uintptr_t)&fake_sF },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },

  { "_tolower_tab_", (uintptr_t)&_tolower_tab_ },
  { "_toupper_tab_", (uintptr_t)&_toupper_tab_ },

  { "EnterGameFromSCFunc", (uintptr_t)&EnterGameFromSCFunc },
  { "SigningOutfromApp", (uintptr_t)&SigningOutfromApp },
  { "_Z15EnterSocialCLubv", (uintptr_t)ret0 },
  { "_Z12IsSCSignedInv", (uintptr_t)ret0 },

  // Not sure how important this is. Used in some init_array.
  { "pthread_key_create", (uintptr_t)&ret0 },
  { "pthread_key_delete", (uintptr_t)&ret0 },

  { "pthread_getspecific", (uintptr_t)&ret0 },
  { "pthread_setspecific", (uintptr_t)&ret0 },

  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake },
  { "pthread_cond_init", (uintptr_t)&pthread_cond_init_fake },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_fake },
  { "pthread_cond_timeout_np", (uintptr_t)&pthread_cond_timeout_np_fake },
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
  // { "sem_trywait", (uintptr_t)&sem_trywait },
  { "sem_wait", (uintptr_t)&sem_wait_fake },

  { "_Jv_RegisterClasses", (uintptr_t)0 },
  { "_ITM_deregisterTMCloneTable", (uintptr_t)0 },
  { "_ITM_registerTMCloneTable", (uintptr_t)0 },

  { "__deregister_frame_info", (uintptr_t)0 },
  { "__register_frame_info", (uintptr_t)0 },

  { "GetRockstarID", (uintptr_t)&GetRockstarID },

  { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },

  { "__android_log_print", (uintptr_t)__android_log_print },

  { "__assert2", (uintptr_t)&__assert2 },
  { "__errno", (uintptr_t)&__errno },
  // { "__isfinitef", (uintptr_t)&__isfinitef },

  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  // freezes with real __stack_chk_guard
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },

  { "__aeabi_dcmplt", (uintptr_t)&__aeabi_dcmplt },
  { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
  { "__aeabi_dsub", (uintptr_t)&__aeabi_dsub },
  { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
  { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
  { "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
  { "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
  { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
  { "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
  { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
  { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
  { "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d },
  { "__aeabi_ul2f", (uintptr_t)&__aeabi_ul2f },
  { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
  { "__aeabi_d2f", (uintptr_t)&__aeabi_d2f },
  { "__aeabi_d2uiz", (uintptr_t)&__aeabi_d2uiz },
  { "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
  { "__aeabi_dcmpeq", (uintptr_t)&__aeabi_dcmpeq },
  { "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
  { "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
  { "__aeabi_f2d", (uintptr_t)&__aeabi_f2d },
  { "__aeabi_fcmpeq", (uintptr_t)&__aeabi_fcmpeq },
  { "__aeabi_fcmpgt", (uintptr_t)&__aeabi_fcmpgt },
  { "__aeabi_fcmpun", (uintptr_t)&__aeabi_fcmpun },

  { "_Unwind_Complete", (uintptr_t)&_Unwind_Complete },
  { "_Unwind_DeleteException", (uintptr_t)&_Unwind_DeleteException },
  { "_Unwind_GetDataRelBase", (uintptr_t)&_Unwind_GetDataRelBase },
  { "_Unwind_GetLanguageSpecificData", (uintptr_t)&_Unwind_GetLanguageSpecificData },
  { "_Unwind_GetRegionStart", (uintptr_t)&_Unwind_GetRegionStart },
  { "_Unwind_GetTextRelBase", (uintptr_t)&_Unwind_GetTextRelBase },
  { "_Unwind_RaiseException", (uintptr_t)&_Unwind_RaiseException },
  { "_Unwind_Resume", (uintptr_t)&_Unwind_Resume },
  { "_Unwind_Resume_or_Rethrow", (uintptr_t)&_Unwind_Resume_or_Rethrow },
  { "_Unwind_VRS_Get", (uintptr_t)&_Unwind_VRS_Get },
  { "_Unwind_VRS_Set", (uintptr_t)&_Unwind_VRS_Set },

  { "_ctype_", (uintptr_t)&_ctype_ },

   // TODO: use math neon?
  { "asin", (uintptr_t)&asin },
  { "acos", (uintptr_t)&acos },
  { "atan", (uintptr_t)&atan },
  { "acosf", (uintptr_t)&acosf },
  { "asinf", (uintptr_t)&asinf },
  { "atan2", (uintptr_t)&atan2 },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "ceilf", (uintptr_t)&ceilf },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  { "cosh", (uintptr_t)&cosh },
  { "exp", (uintptr_t)&exp },
  { "floor", (uintptr_t)&floor },
  { "floorf", (uintptr_t)&floorf },
  { "fmod", (uintptr_t)&fmod },
  { "fmodf", (uintptr_t)&fmodf },
  { "log", (uintptr_t)&log },
  { "log10f", (uintptr_t)&log10f },
  { "log10", (uintptr_t)&log10 },
  { "modff", (uintptr_t)&modff },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "sin", (uintptr_t)&sin },
  { "sinf", (uintptr_t)&sinf },
  { "sinh", (uintptr_t)&sinh },
  { "tan", (uintptr_t)&tan },
  { "tanf", (uintptr_t)&tanf },
  { "tanh", (uintptr_t)&tanh },
  { "sqrt", (uintptr_t)&sqrt },
  { "modf", (uintptr_t)&modf },

  { "atoi", (uintptr_t)&atoi },
  { "isspace", (uintptr_t)&isspace },
  { "isalnum", (uintptr_t)&isalnum },

  { "calloc", (uintptr_t)&calloc },
  { "free", (uintptr_t)&free },
  { "malloc", (uintptr_t)&malloc },
  { "realloc", (uintptr_t)&realloc },

  { "clock_gettime", (uintptr_t)&clock_gettime },
  { "ctime", (uintptr_t)&ctime },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  { "gmtime", (uintptr_t)&gmtime },
  { "time", (uintptr_t)&time },
  { "asctime", (uintptr_t)&asctime },
  { "localtime", (uintptr_t)&localtime },
  { "strftime", (uintptr_t)&strftime },

  // { "eglGetDisplay", (uintptr_t)&eglGetDisplay },
  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
  // { "eglQueryString", (uintptr_t)&eglQueryString },

  { "abort", (uintptr_t)&abort },
  { "exit", (uintptr_t)&exit },

  { "fopen", (uintptr_t)&fopen },
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
  { "glDepthRangef", (uintptr_t)&glDepthRangef },
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
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
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
  { "glTexParameterf", (uintptr_t)&glTexParameterf },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glUniform1f", (uintptr_t)&glUniform1f },
  { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform3f", (uintptr_t)&glUniform3f },
  { "glUniform3fv", (uintptr_t)&glUniform3fv },
  { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttrib4fv", (uintptr_t)&glVertexAttrib4fv },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glViewport", (uintptr_t)&glViewport },

  // TODO: check if they are compatible
  // { "longjmp", (uintptr_t)&longjmp },
  // { "setjmp", (uintptr_t)&setjmp },

  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy_neon },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "memchr", (uintptr_t)&memchr },

  { "printf", (uintptr_t)&debugPrintf },
  { "puts", (uintptr_t)&puts },

  { "bsearch", (uintptr_t)&bsearch },
  { "qsort", (uintptr_t)&qsort },

  // { "raise", (uintptr_t)&raise },
  // { "rewind", (uintptr_t)&rewind },

  // { "scmainUpdate", (uintptr_t)&scmainUpdate },
  // { "slCreateEngine", (uintptr_t)&slCreateEngine },

  { "lrand48", (uintptr_t)&lrand48 },
  { "srand48", (uintptr_t)&srand48 },

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
  { "strxfrm", (uintptr_t)&strxfrm },

  // { "syscall", (uintptr_t)&syscall },
  // { "sysconf", (uintptr_t)&sysconf },

  { "nanosleep", (uintptr_t)&nanosleep },
  { "usleep", (uintptr_t)&usleep },
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

  so_blockid = load_file("ux0:data/maxpayne/libMaxPayne.so", &so_data);

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
            *ptr = (uintptr_t)text_base + sym->st_value;
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

  openal_patch();
  opengl_patch();
  functions_patch();

  kuFlushIcache(text_base, text_size);

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

  *(uint8_t *)find_addr_by_symbol("IsAndroidPaused") = 0;
  *(uint32_t *)find_addr_by_symbol("cIsAndroidPaused") = 0;
  *(uint8_t *)find_addr_by_symbol("IsInitGraphics") = 1;
  *(uint32_t *)find_addr_by_symbol("androidScreenWidth") = 960;
  *(uint32_t *)find_addr_by_symbol("androidScreenHeight") = 544;

  int (* NVEventAppMain)(int argc, char *argv[]) = (void *)find_addr_by_symbol("_Z14NVEventAppMainiPPc");
  NVEventAppMain(0, NULL);

  return 0;
}
