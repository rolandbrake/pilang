#ifndef PI_AUDIO_H
#define PI_AUDIO_H

#include "../../src/pi_value.h"

Value au_load(VM *vm, int argc, Value *argv);

Value au_sampleRate(VM *vm, int argc, Value *argv);
Value au_channels(VM *vm, int argc, Value *argv);
Value au_frames(VM *vm, int argc, Value *argv);
Value au_duration(VM *vm, int argc, Value *argv);
Value au_format(VM *vm, int argc, Value *argv);

Value au_play(VM *vm, int argc, Value *argv);
Value au_volume(VM *vm, int argc, Value *argv);
Value au_pause(VM *vm, int argc, Value *argv);
Value au_resume(VM *vm, int argc, Value *argv);
Value au_stop(VM *vm, int argc, Value *argv);
Value au_isPlaying(VM *vm, int argc, Value *argv);
Value au_playMusic(VM *vm, int argc, Value *argv);
Value au_musicIsPlaying(VM *vm, int argc, Value *argv);
Value au_stopMusic(VM *vm, int argc, Value *argv);

#endif // PI_AUDIO_H
