#ifndef __HOOKS_H__
#define __HOOKS_H__

void patch_opengl(void);
void patch_openal(void);
void patch_game(void);
void patch_io(void);

int fios_init(void);
void fios_terminate(void);

#endif
