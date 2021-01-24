#ifndef __UTIL_H__
#define __UTIL_H__

void *memcpy_neon(void *destination, const void *source, size_t num);

int debugPrintf(char *text, ...);

int ret0(void);
int ret1(void);
int retm1(void);

#endif
