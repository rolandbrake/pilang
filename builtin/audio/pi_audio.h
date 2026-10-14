#ifndef PI_AUDIO_H
#define PI_AUDIO_H

#include "../../pi_value.h"

Value au_load(vm_t *vm, int argc, Value *argv);

Value au_sampleRate(vm_t *vm, int argc, Value *argv);
Value au_channels(vm_t *vm, int argc, Value *argv);
Value au_frames(vm_t *vm, int argc, Value *argv);
Value au_duration(vm_t *vm, int argc, Value *argv);
Value au_format(vm_t *vm, int argc, Value *argv);

Value au_play(vm_t *vm, int argc, Value *argv);
Value au_pause(vm_t *vm, int argc, Value *argv);
Value au_resume(vm_t *vm, int argc, Value *argv);
Value au_stop(vm_t *vm, int argc, Value *argv);
Value au_isPlaying(vm_t *vm, int argc, Value *argv);
Value au_playMusic(vm_t *vm, int argc, Value *argv);
Value au_musicIsPlaying(vm_t *vm, int argc, Value *argv);
Value au_stopMusic(vm_t *vm, int argc, Value *argv);

#endif // PI_AUDIO_H
