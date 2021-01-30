/* util.h -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

extern char fs_root[];

void *memcpy_neon(void *destination, const void *source, size_t num);

int debugPrintf(char *text, ...);

int ret0(void);
int ret1(void);
int retm1(void);

#endif
