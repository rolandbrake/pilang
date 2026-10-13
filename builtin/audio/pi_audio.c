#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "pi_audio.h"
#include "../pi_builtin.h"
#include "../../common.h"
#include "../../pi_object.h"
#include "../../pi_value.h"

ObjAudio *new_audio(SDL_AudioSpec spec, float *samples, Uint32 frames)
{
    ObjAudio *audio = (ObjAudio *)alloc_object(sizeof(ObjAudio), OBJ_AUDIO);
    audio->spec = spec;
    audio->samples = samples;
    audio->frames = frames;
    return audio;
}

Value au_load(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[audio.load] expects a file path string.");

    const char *path = AS_CSTRING(argv[0]);
    char *resolved_path = resolve_path(vm, path);
    if (!resolved_path)
        vm_error(vm, "[audio.load] out of memory.");

    SDL_AudioSpec source_spec;
    Uint8 *source_buffer = NULL;
    Uint32 source_length = 0;
    if (!SDL_LoadWAV(resolved_path, &source_spec, &source_buffer, &source_length))
    {
        free(resolved_path);
        vm_errorf(vm, "[audio.load] failed to load WAV '%s': %s", path, SDL_GetError());
    }
    free(resolved_path);

    if (source_spec.freq <= 0 || source_spec.channels == 0)
    {
        SDL_FreeWAV(source_buffer);
        vm_error(vm, "[audio.load] WAV has an invalid sample rate or channel count.");
    }

    SDL_AudioCVT converter;
    if (SDL_BuildAudioCVT(&converter, source_spec.format, source_spec.channels,
                          source_spec.freq, AUDIO_F32SYS, source_spec.channels,
                          source_spec.freq) < 0)
    {
        SDL_FreeWAV(source_buffer);
        vm_errorf(vm, "[audio.load] failed to convert WAV '%s': %s", path, SDL_GetError());
    }

    size_t converted_capacity = (size_t)source_length * (size_t)converter.len_mult;
    Uint8 *converted_buffer = converted_capacity ? malloc(converted_capacity) : NULL;
    if (converted_capacity && !converted_buffer)
    {
        SDL_FreeWAV(source_buffer);
        vm_error(vm, "[audio.load] out of memory.");
    }

    if (source_length)
        memcpy(converted_buffer, source_buffer, source_length);
    converter.buf = converted_buffer;
    converter.len = (int)source_length;
    if (converter.needed && SDL_ConvertAudio(&converter) < 0)
    {
        free(converted_buffer);
        SDL_FreeWAV(source_buffer);
        vm_errorf(vm, "[audio.load] failed to convert WAV '%s': %s", path, SDL_GetError());
    }
    SDL_FreeWAV(source_buffer);

    size_t converted_length = converter.needed ? (size_t)converter.len_cvt : source_length;
    size_t frame_size = (size_t)source_spec.channels * sizeof(float);
    if (converted_length % frame_size != 0 || converted_length / frame_size > UINT32_MAX)
    {
        free(converted_buffer);
        vm_error(vm, "[audio.load] converted WAV has an invalid frame length.");
    }

    source_spec.format = AUDIO_F32SYS;
    source_spec.size = (Uint32)converted_length;
    Uint32 frames = (Uint32)(converted_length / frame_size);
    return NEW_OBJ(add_obj(vm, (Object *)new_audio(source_spec,
                                                   (float *)converted_buffer, frames)));
}

Value au_sampleRate(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_error(vm, "[audio.sample_rate] expects an audio clip.");
    return NEW_NUM(AS_AUDIO(argv[0])->spec.freq);
}

Value au_channels(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_error(vm, "[audio.channels] expects an audio clip.");
    return NEW_NUM(AS_AUDIO(argv[0])->spec.channels);
}

Value au_frames(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_error(vm, "[audio.frames] expects an audio clip.");
    return NEW_NUM(AS_AUDIO(argv[0])->frames);
}

Value au_duration(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_error(vm, "[audio.duration] expects an audio clip.");
    ObjAudio *audio = AS_AUDIO(argv[0]);
    return NEW_NUM((double)audio->frames / audio->spec.freq);
}

Value au_format(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_error(vm, "[audio.format] expects an audio clip.");
    return NEW_OBJ(add_obj(vm, new_pistring(strdup("f32"))));
}

static BuiltinFunc audio_funcs[] = {
    {"load", au_load},
    {"sample_rate", au_sampleRate},
    {"channels", au_channels},
    {"frames", au_frames},
    {"duration", au_duration},
    {"format", au_format},
};

static BuiltinConst audio_consts[] = {};

DEFINE_BUILTIN_MODULE(module_audio, "audio", audio_funcs, audio_consts);
