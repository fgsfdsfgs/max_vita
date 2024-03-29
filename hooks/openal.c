/* openal.c -- OpenAL hooks and patches
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include "../util.h"
#include "../so_util.h"

// the game doesn't properly unqueue buffers to be available for deletion,
// so we do that for it

static ALuint last_stopped_src = 0;

// remember the last stopped source cause the game does
// alStopSource(); alDeleteBuffers();
void alSourceStopHook(ALuint src) {
  last_stopped_src = src;
  alSourceStop(src);
}

void alDeleteBuffersHook(ALsizei n, ALuint *bufs) {
  if (last_stopped_src) {
    // there was an alStopSource() call right before this is happening,
    // properly handle the source before deleting its buffers
    ALint type = 0;
    alGetSourcei(last_stopped_src, AL_SOURCE_TYPE, &type);
    // if the source is streaming, unqueue the buffers from it,
    // otherwise just set its buffer to NULL
    if (type == AL_STREAMING)
      alSourceUnqueueBuffers(last_stopped_src, n, bufs);
    else
      alSourcei(last_stopped_src, AL_BUFFER, 0);
    last_stopped_src = 0;
  }
  alDeleteBuffers(n, bufs);
}

ALCcontext *alcCreateContextHook(ALCdevice *dev, const ALCint *unused) {
  // override 22050hz with 44100hz in case someone wants high quality sounds
  const ALCint attr[] = { ALC_FREQUENCY, 44100, 0 };
  return alcCreateContext(dev, attr);
}

void patch_openal(void) {
  // used for openal
  hook_thumb(so_find_addr("InitializeCriticalSection"), (uintptr_t)ret0);
  // openal API
  hook_thumb(so_find_addr("alAuxiliaryEffectSlotf"), (uintptr_t)alAuxiliaryEffectSlotf);
  hook_thumb(so_find_addr("alAuxiliaryEffectSlotfv"), (uintptr_t)alAuxiliaryEffectSlotfv);
  hook_thumb(so_find_addr("alAuxiliaryEffectSloti"), (uintptr_t)alAuxiliaryEffectSloti);
  hook_thumb(so_find_addr("alAuxiliaryEffectSlotiv"), (uintptr_t)alAuxiliaryEffectSlotiv);
  hook_thumb(so_find_addr("alBuffer3f"), (uintptr_t)alBuffer3f);
  hook_thumb(so_find_addr("alBuffer3i"), (uintptr_t)alBuffer3i);
  hook_thumb(so_find_addr("alBufferData"), (uintptr_t)alBufferData);
  hook_thumb(so_find_addr("alBufferf"), (uintptr_t)alBufferf);
  hook_thumb(so_find_addr("alBufferfv"), (uintptr_t)alBufferfv);
  hook_thumb(so_find_addr("alBufferi"), (uintptr_t)alBufferi);
  hook_thumb(so_find_addr("alBufferiv"), (uintptr_t)alBufferiv);
  hook_thumb(so_find_addr("alDeleteAuxiliaryEffectSlots"), (uintptr_t)alDeleteAuxiliaryEffectSlots);
  hook_thumb(so_find_addr("alDeleteBuffers"), (uintptr_t)alDeleteBuffersHook);
  hook_thumb(so_find_addr("alDeleteEffects"), (uintptr_t)alDeleteEffects);
  hook_thumb(so_find_addr("alDeleteFilters"), (uintptr_t)alDeleteFilters);
  hook_thumb(so_find_addr("alDeleteSources"), (uintptr_t)alDeleteSources);
  hook_thumb(so_find_addr("alDisable"), (uintptr_t)alDisable);
  hook_thumb(so_find_addr("alDistanceModel"), (uintptr_t)alDistanceModel);
  hook_thumb(so_find_addr("alDopplerFactor"), (uintptr_t)alDopplerFactor);
  hook_thumb(so_find_addr("alDopplerVelocity"), (uintptr_t)alDopplerVelocity);
  hook_thumb(so_find_addr("alEffectf"), (uintptr_t)alEffectf);
  hook_thumb(so_find_addr("alEffectfv"), (uintptr_t)alEffectfv);
  hook_thumb(so_find_addr("alEffecti"), (uintptr_t)alEffecti);
  hook_thumb(so_find_addr("alEffectiv"), (uintptr_t)alEffectiv);
  hook_thumb(so_find_addr("alEnable"), (uintptr_t)alEnable);
  hook_thumb(so_find_addr("alFilterf"), (uintptr_t)alFilterf);
  hook_thumb(so_find_addr("alFilterfv"), (uintptr_t)alFilterfv);
  hook_thumb(so_find_addr("alFilteri"), (uintptr_t)alFilteri);
  hook_thumb(so_find_addr("alFilteriv"), (uintptr_t)alFilteriv);
  hook_thumb(so_find_addr("alGenAuxiliaryEffectSlots"), (uintptr_t)alGenAuxiliaryEffectSlots);
  hook_thumb(so_find_addr("alGenBuffers"), (uintptr_t)alGenBuffers);
  hook_thumb(so_find_addr("alGenEffects"), (uintptr_t)alGenEffects);
  hook_thumb(so_find_addr("alGenFilters"), (uintptr_t)alGenFilters);
  hook_thumb(so_find_addr("alGenSources"), (uintptr_t)alGenSources);
  hook_thumb(so_find_addr("alGetAuxiliaryEffectSlotf"), (uintptr_t)alGetAuxiliaryEffectSlotf);
  hook_thumb(so_find_addr("alGetAuxiliaryEffectSlotfv"), (uintptr_t)alGetAuxiliaryEffectSlotfv);
  hook_thumb(so_find_addr("alGetAuxiliaryEffectSloti"), (uintptr_t)alGetAuxiliaryEffectSloti);
  hook_thumb(so_find_addr("alGetAuxiliaryEffectSlotiv"), (uintptr_t)alGetAuxiliaryEffectSlotiv);
  hook_thumb(so_find_addr("alGetBoolean"), (uintptr_t)alGetBoolean);
  hook_thumb(so_find_addr("alGetBooleanv"), (uintptr_t)alGetBooleanv);
  hook_thumb(so_find_addr("alGetBuffer3f"), (uintptr_t)alGetBuffer3f);
  hook_thumb(so_find_addr("alGetBuffer3i"), (uintptr_t)alGetBuffer3i);
  hook_thumb(so_find_addr("alGetBufferf"), (uintptr_t)alGetBufferf);
  hook_thumb(so_find_addr("alGetBufferfv"), (uintptr_t)alGetBufferfv);
  hook_thumb(so_find_addr("alGetBufferi"), (uintptr_t)alGetBufferi);
  hook_thumb(so_find_addr("alGetBufferiv"), (uintptr_t)alGetBufferiv);
  hook_thumb(so_find_addr("alGetDouble"), (uintptr_t)alGetDouble);
  hook_thumb(so_find_addr("alGetDoublev"), (uintptr_t)alGetDoublev);
  hook_thumb(so_find_addr("alGetEffectf"), (uintptr_t)alGetEffectf);
  hook_thumb(so_find_addr("alGetEffectfv"), (uintptr_t)alGetEffectfv);
  hook_thumb(so_find_addr("alGetEffecti"), (uintptr_t)alGetEffecti);
  hook_thumb(so_find_addr("alGetEffectiv"), (uintptr_t)alGetEffectiv);
  hook_thumb(so_find_addr("alGetEnumValue"), (uintptr_t)alGetEnumValue);
  hook_thumb(so_find_addr("alGetError"), (uintptr_t)alGetError);
  hook_thumb(so_find_addr("alGetFilterf"), (uintptr_t)alGetFilterf);
  hook_thumb(so_find_addr("alGetFilterfv"), (uintptr_t)alGetFilterfv);
  hook_thumb(so_find_addr("alGetFilteri"), (uintptr_t)alGetFilteri);
  hook_thumb(so_find_addr("alGetFilteriv"), (uintptr_t)alGetFilteriv);
  hook_thumb(so_find_addr("alGetFloat"), (uintptr_t)alGetFloat);
  hook_thumb(so_find_addr("alGetFloatv"), (uintptr_t)alGetFloatv);
  hook_thumb(so_find_addr("alGetInteger"), (uintptr_t)alGetInteger);
  hook_thumb(so_find_addr("alGetIntegerv"), (uintptr_t)alGetIntegerv);
  hook_thumb(so_find_addr("alGetListener3f"), (uintptr_t)alGetListener3f);
  hook_thumb(so_find_addr("alGetListener3i"), (uintptr_t)alGetListener3i);
  hook_thumb(so_find_addr("alGetListenerf"), (uintptr_t)alGetListenerf);
  hook_thumb(so_find_addr("alGetListenerfv"), (uintptr_t)alGetListenerfv);
  hook_thumb(so_find_addr("alGetListeneri"), (uintptr_t)alGetListeneri);
  hook_thumb(so_find_addr("alGetListeneriv"), (uintptr_t)alGetListeneriv);
  hook_thumb(so_find_addr("alGetProcAddress"), (uintptr_t)alGetProcAddress);
  hook_thumb(so_find_addr("alGetSource3f"), (uintptr_t)alGetSource3f);
  hook_thumb(so_find_addr("alGetSource3i"), (uintptr_t)alGetSource3i);
  hook_thumb(so_find_addr("alGetSourcef"), (uintptr_t)alGetSourcef);
  hook_thumb(so_find_addr("alGetSourcefv"), (uintptr_t)alGetSourcefv);
  hook_thumb(so_find_addr("alGetSourcei"), (uintptr_t)alGetSourcei);
  hook_thumb(so_find_addr("alGetSourceiv"), (uintptr_t)alGetSourceiv);
  hook_thumb(so_find_addr("alGetString"), (uintptr_t)alGetString);
  hook_thumb(so_find_addr("alIsAuxiliaryEffectSlot"), (uintptr_t)alIsAuxiliaryEffectSlot);
  hook_thumb(so_find_addr("alIsBuffer"), (uintptr_t)alIsBuffer);
  hook_thumb(so_find_addr("alIsEffect"), (uintptr_t)alIsEffect);
  hook_thumb(so_find_addr("alIsEnabled"), (uintptr_t)alIsEnabled);
  hook_thumb(so_find_addr("alIsExtensionPresent"), (uintptr_t)alIsExtensionPresent);
  hook_thumb(so_find_addr("alIsFilter"), (uintptr_t)alIsFilter);
  hook_thumb(so_find_addr("alIsSource"), (uintptr_t)alIsSource);
  hook_thumb(so_find_addr("alListener3f"), (uintptr_t)alListener3f);
  hook_thumb(so_find_addr("alListener3i"), (uintptr_t)alListener3i);
  hook_thumb(so_find_addr("alListenerf"), (uintptr_t)alListenerf);
  hook_thumb(so_find_addr("alListenerfv"), (uintptr_t)alListenerfv);
  hook_thumb(so_find_addr("alListeneri"), (uintptr_t)alListeneri);
  hook_thumb(so_find_addr("alListeneriv"), (uintptr_t)alListeneriv);
  hook_thumb(so_find_addr("alSource3f"), (uintptr_t)alSource3f);
  hook_thumb(so_find_addr("alSource3i"), (uintptr_t)alSource3i);
  hook_thumb(so_find_addr("alSourcePause"), (uintptr_t)alSourcePause);
  hook_thumb(so_find_addr("alSourcePausev"), (uintptr_t)alSourcePausev);
  hook_thumb(so_find_addr("alSourcePlay"), (uintptr_t)alSourcePlay);
  hook_thumb(so_find_addr("alSourcePlayv"), (uintptr_t)alSourcePlayv);
  hook_thumb(so_find_addr("alSourceQueueBuffers"), (uintptr_t)alSourceQueueBuffers);
  hook_thumb(so_find_addr("alSourceRewind"), (uintptr_t)alSourceRewind);
  hook_thumb(so_find_addr("alSourceRewindv"), (uintptr_t)alSourceRewindv);
  hook_thumb(so_find_addr("alSourceStop"), (uintptr_t)alSourceStopHook);
  hook_thumb(so_find_addr("alSourceStopv"), (uintptr_t)alSourceStopv);
  hook_thumb(so_find_addr("alSourceUnqueueBuffers"), (uintptr_t)alSourceUnqueueBuffers);
  hook_thumb(so_find_addr("alSourcef"), (uintptr_t)alSourcef);
  hook_thumb(so_find_addr("alSourcefv"), (uintptr_t)alSourcefv);
  hook_thumb(so_find_addr("alSourcei"), (uintptr_t)alSourcei);
  hook_thumb(so_find_addr("alSourceiv"), (uintptr_t)alSourceiv);
  hook_thumb(so_find_addr("alSpeedOfSound"), (uintptr_t)alSpeedOfSound);
  hook_thumb(so_find_addr("alcCaptureCloseDevice"), (uintptr_t)alcCaptureCloseDevice);
  hook_thumb(so_find_addr("alcCaptureOpenDevice"), (uintptr_t)alcCaptureOpenDevice);
  hook_thumb(so_find_addr("alcCaptureSamples"), (uintptr_t)alcCaptureSamples);
  hook_thumb(so_find_addr("alcCaptureStart"), (uintptr_t)alcCaptureStart);
  hook_thumb(so_find_addr("alcCaptureStop"), (uintptr_t)alcCaptureStop);
  hook_thumb(so_find_addr("alcCloseDevice"), (uintptr_t)alcCloseDevice);
  hook_thumb(so_find_addr("alcCreateContext"), (uintptr_t)alcCreateContextHook);
  hook_thumb(so_find_addr("alcDestroyContext"), (uintptr_t)alcDestroyContext);
  hook_thumb(so_find_addr("alcGetContextsDevice"), (uintptr_t)alcGetContextsDevice);
  hook_thumb(so_find_addr("alcGetCurrentContext"), (uintptr_t)alcGetCurrentContext);
  hook_thumb(so_find_addr("alcGetEnumValue"), (uintptr_t)alcGetEnumValue);
  hook_thumb(so_find_addr("alcGetError"), (uintptr_t)alcGetError);
  hook_thumb(so_find_addr("alcGetIntegerv"), (uintptr_t)alcGetIntegerv);
  hook_thumb(so_find_addr("alcGetProcAddress"), (uintptr_t)alcGetProcAddress);
  hook_thumb(so_find_addr("alcGetString"), (uintptr_t)alcGetString);
  hook_thumb(so_find_addr("alcGetThreadContext"), (uintptr_t)alcGetThreadContext);
  hook_thumb(so_find_addr("alcIsExtensionPresent"), (uintptr_t)alcIsExtensionPresent);
  hook_thumb(so_find_addr("alcMakeContextCurrent"), (uintptr_t)alcMakeContextCurrent);
  hook_thumb(so_find_addr("alcOpenDevice"), (uintptr_t)alcOpenDevice);
  hook_thumb(so_find_addr("alcProcessContext"), (uintptr_t)alcProcessContext);
  hook_thumb(so_find_addr("alcSetThreadContext"), (uintptr_t)alcSetThreadContext);
  hook_thumb(so_find_addr("alcSuspendContext"), (uintptr_t)alcSuspendContext);
}
