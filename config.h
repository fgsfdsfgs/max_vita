/* config.h -- global configuration and config file handling
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define LOAD_ADDRESS 0x98000000

#define MEMORY_MB 256

#define DATA_PATH "data/maxpayne"
#define SO_NAME "libMaxPayne.so"
#define CONFIG_NAME "config.txt"
#define LOG_PATH "ux0:data/max_log.txt"

#define SCREEN_W 960
#define SCREEN_H 544

// #define DEBUG_LOG 1

typedef struct {
  int touch_x_margin;
  int use_fios2;
  int io_cache_block_num;
  int io_cache_block_size;
  int trilinear_filter;
  int msaa;
  int disable_mipmaps;
  int language;
  int crouch_toggle;
  char mod_file[0x100];
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
