/* imports.c -- .so import resolution
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __IMPORTS_H__
#define __IMPORTS_H__

#include <stdio.h>
#include <stdlib.h>
#include "so_util.h"

extern FILE *stderr_fake;
extern DynLibFunction dynlib_functions[];
extern size_t dynlib_numfunctions;

void update_imports(void);

#endif
