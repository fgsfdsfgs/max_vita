/* config.c -- simple configuration parser
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"

Config config;

int read_config(const char *file) {
  char name[64];
  int value;
  FILE *f;

  memset(&config, 0, sizeof(Config));
  config.touch_x_margin = 100;
  config.use_fios2 = 1;
  config.io_cache_block_num = 64;
  config.io_cache_block_size = 65536;
  config.msaa = 2;

  f = fopen(file, "r");
  if (f == NULL)
    return -1;

  while ((fscanf(f, "%63s %d", name, &value)) != EOF) {
    #define CONFIG_VAR(var) if (strcmp(name, #var) == 0) config.var = value;
    CONFIG_VAR(touch_x_margin);
    CONFIG_VAR(use_fios2);
    CONFIG_VAR(io_cache_block_num);
    CONFIG_VAR(io_cache_block_size);
    CONFIG_VAR(msaa);
    #undef CONFIG_VAR
  }

  fclose(f);

  return 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  #define CONFIG_VAR(var) fprintf(f, "%s %d\n", #var, config.var)
  CONFIG_VAR(touch_x_margin);
  CONFIG_VAR(use_fios2);
  CONFIG_VAR(io_cache_block_num);
  CONFIG_VAR(io_cache_block_size);
  CONFIG_VAR(msaa);
  #undef CONFIG_VAR

  fclose(f);

  return 0;
}
