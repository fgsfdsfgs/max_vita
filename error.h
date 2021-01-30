/* error.h -- error handler
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __MAX_ERROR_H__
#define __MAX_ERROR_H__

void fatal_error(const char *fmt, ...) __attribute__((noreturn));

#endif
