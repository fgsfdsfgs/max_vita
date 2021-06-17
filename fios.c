/* fios.c -- use FIOS2 for optimized I/O
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fios.h"

#define MAX_PATH_LENGTH 256
#define RAMCACHEBLOCKSIZE (128 * 1024)
#define RAMCACHEBLOCKNUM 512

static int64_t g_OpStorage[SCE_FIOS_OP_STORAGE_SIZE(64, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];
static int64_t g_ChunkStorage[SCE_FIOS_CHUNK_STORAGE_SIZE(1024) / sizeof(int64_t) + 1];
static int64_t g_FHStorage[SCE_FIOS_FH_STORAGE_SIZE(512, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];
static int64_t g_DHStorage[SCE_FIOS_DH_STORAGE_SIZE(32, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];

static SceFiosRamCacheContext g_RamCacheContext = SCE_FIOS_RAM_CACHE_CONTEXT_INITIALIZER;
static char *g_RamCacheWorkBuffer;

int fios_init(void) {
  int res;

  SceFiosParams params = SCE_FIOS_PARAMS_INITIALIZER;
  params.opStorage.pPtr = g_OpStorage;
  params.opStorage.length = sizeof(g_OpStorage);
  params.chunkStorage.pPtr = g_ChunkStorage;
  params.chunkStorage.length = sizeof(g_ChunkStorage);
  params.fhStorage.pPtr = g_FHStorage;
  params.fhStorage.length = sizeof(g_FHStorage);
  params.dhStorage.pPtr = g_DHStorage;
  params.dhStorage.length = sizeof(g_DHStorage);
  params.pathMax = MAX_PATH_LENGTH;

  res = sceFiosInitialize(&params);
  if (res < 0)
    return res;

  g_RamCacheWorkBuffer = memalign(8, config.io_cache_block_num * config.io_cache_block_size);
  if (!g_RamCacheWorkBuffer)
    return -1;

  g_RamCacheContext.pPath = DATA_PATH;
  g_RamCacheContext.pWorkBuffer = g_RamCacheWorkBuffer;
  g_RamCacheContext.workBufferSize = config.io_cache_block_num * config.io_cache_block_size;
  g_RamCacheContext.blockSize = config.io_cache_block_size;
  res = sceFiosIOFilterAdd(0, sceFiosIOFilterCache, &g_RamCacheContext);
  if (res < 0)
    return res;

  return 0;
}

void fios_terminate(void) {
  if (g_RamCacheWorkBuffer) {
    sceFiosTerminate();
    free(g_RamCacheWorkBuffer);
  }
}
