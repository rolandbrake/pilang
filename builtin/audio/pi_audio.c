#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "pi_audio.h"
#include "../pi_builtin.h"
#include "../../common.h"
#include "../../pi_object.h"
#include "../../pi_value.h"

static Mix_Music *current_music = NULL;

ObjAudio *new_audio(SDL_AudioSpec spec, float *samples, Uint32 frames,
                    Mix_Chunk *chunk)
{
    ObjAudio *audio = (ObjAudio *)alloc_object(sizeof(ObjAudio), OBJ_AUDIO);
    audio->spec = spec;
    audio->samples = samples;
    audio->frames = frames;
    audio->chunk = chunk;
    audio->channel = -1;
    return audio;
}

static void ensure_mixer(vm_t *vm)
{
    static bool opened = false;
    if (opened)
        return;

    Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
    if (Mix_OpenAudio(48000, AUDIO_F32SYS, 2, 1024) < 0)
        vm_errorf(vm, "[audio] failed to open audio output: %s", Mix_GetError());
    opened = true;
}

static int active_channel(ObjAudio *audio)
{
    if (audio->channel < 0 || Mix_GetChunk(audio->channel) != audio->chunk ||
        (!Mix_Playing(audio->channel) && !Mix_Paused(audio->channel)))
    {
        audio->channel = -1;
        return -1;
    }
    return audio->channel;
}

static ObjAudio *require_audio(vm_t *vm, int argc, Value *argv, const char *name)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_errorf(vm, "[audio.%s] expects an audio clip.", name);
    return AS_AUDIO(argv[0]);
}

Value au_load(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[audio.load] expects a file path string.");

    const char *path = AS_CSTRING(argv[0]);
    char *resolved_path = resolve_path(vm, path);
    if (!resolved_path)
        vm_error(vm, "[audio.load] out of memory.");

    ensure_mixer(vm);
    Mix_Chunk *chunk = Mix_LoadWAV(resolved_path);
    if (!chunk)
    {
        free(resolved_path);
        vm_errorf(vm, "[audio.load] failed to load '%s': %s", path, Mix_GetError());
    }

    SDL_AudioSpec source_spec;
    Uint8 *source_buffer = NULL;
    Uint32 source_length = 0;
    if (!SDL_LoadWAV(resolved_path, &source_spec, &source_buffer, &source_length))
    {
        free(resolved_path);
        int frequency, channels;
        Uint16 format;
        if (!Mix_QuerySpec(&frequency, &format, &channels))
        {
            Mix_FreeChunk(chunk);
            vm_error(vm, "[audio.load] audio output is unavailable.");
        }

        int bytes_per_sample = SDL_AUDIO_BITSIZE(format) / 8;
        if (frequency <= 0 || channels <= 0 || bytes_per_sample <= 0 ||
            chunk->alen % (channels * bytes_per_sample) != 0)
        {
            Mix_FreeChunk(chunk);
            vm_errorf(vm, "[audio.load] failed to inspect '%s'.", path);
        }

        SDL_AudioSpec playback_spec = {0};
        playback_spec.freq = frequency;
        playback_spec.format = format;
        playback_spec.channels = (Uint8)channels;
        playback_spec.size = chunk->alen;
        Uint32 frames = chunk->alen / (channels * bytes_per_sample);
        return NEW_OBJ(add_obj(vm, (Object *)new_audio(playback_spec, NULL, frames, chunk)));
    }
    free(resolved_path);

    if (source_spec.freq <= 0 || source_spec.channels == 0)
    {
        SDL_FreeWAV(source_buffer);
        Mix_FreeChunk(chunk);
        vm_error(vm, "[audio.load] WAV has an invalid sample rate or channel count.");
    }

    SDL_AudioCVT converter;
    if (SDL_BuildAudioCVT(&converter, source_spec.format, source_spec.channels,
                          source_spec.freq, AUDIO_F32SYS, source_spec.channels,
                          source_spec.freq) < 0)
    {
        SDL_FreeWAV(source_buffer);
        Mix_FreeChunk(chunk);
        vm_errorf(vm, "[audio.load] failed to convert WAV '%s': %s", path, SDL_GetError());
    }

    size_t converted_capacity = (size_t)source_length * (size_t)converter.len_mult;
    Uint8 *converted_buffer = converted_capacity ? malloc(converted_capacity) : NULL;
    if (converted_capacity && !converted_buffer)
    {
        SDL_FreeWAV(source_buffer);
        Mix_FreeChunk(chunk);
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
        Mix_FreeChunk(chunk);
        vm_errorf(vm, "[audio.load] failed to convert WAV '%s': %s", path, SDL_GetError());
    }
    SDL_FreeWAV(source_buffer);

    size_t converted_length = converter.needed ? (size_t)converter.len_cvt : source_length;
    size_t frame_size = (size_t)source_spec.channels * sizeof(float);
    if (converted_length % frame_size != 0 || converted_length / frame_size > UINT32_MAX)
    {
        free(converted_buffer);
        Mix_FreeChunk(chunk);
        vm_error(vm, "[audio.load] converted WAV has an invalid frame length.");
    }

    source_spec.format = AUDIO_F32SYS;
    source_spec.size = (Uint32)converted_length;
    Uint32 frames = (Uint32)(converted_length / frame_size);
    return NEW_OBJ(add_obj(vm, (Object *)new_audio(source_spec,
                                                   (float *)converted_buffer, frames, chunk)));
}

Value au_sampleRate(vm_t *vm, int argc, Value *argv)
{
    return NEW_NUM(require_audio(vm, argc, argv, "sample_rate")->spec.freq);
}

Value au_channels(vm_t *vm, int argc, Value *argv)
{
    return NEW_NUM(require_audio(vm, argc, argv, "channels")->spec.channels);
}

Value au_frames(vm_t *vm, int argc, Value *argv)
{
    return NEW_NUM(require_audio(vm, argc, argv, "frames")->frames);
}

Value au_duration(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "duration");
    return NEW_NUM((double)audio->frames / audio->spec.freq);
}

Value au_format(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "format");
    const char *format = SDL_AUDIO_ISFLOAT(audio->spec.format) ? "f32" : "pcm";
    return NEW_OBJ(add_obj(vm, new_pistring(strdup(format))));
}

Value au_play(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "play");
    int loops = 0;
    if (argc > 1)
    {
        if (!IS_NUM(argv[1]) || AS_NUM(argv[1]) < -1 || AS_NUM(argv[1]) != (int)AS_NUM(argv[1]))
            vm_error(vm, "[audio.play] loops must be an integer greater than or equal to -1.");
        loops = (int)AS_NUM(argv[1]);
    }

    int current = active_channel(audio);
    if (current >= 0)
        Mix_HaltChannel(current);

    int channel = Mix_PlayChannel(-1, audio->chunk, loops);
    if (channel < 0)
        vm_errorf(vm, "[audio.play] failed to play: %s", Mix_GetError());
    audio->channel = channel;
    return NEW_NUM(channel);
}

Value au_volume(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "volume");
    if (argc < 2 || !IS_NUM(argv[1]) || AS_NUM(argv[1]) < 0 || AS_NUM(argv[1]) > 1)
        vm_error(vm, "[audio.volume] expects a clip and a volume from 0 to 1.");

    int volume = (int)(AS_NUM(argv[1]) * MIX_MAX_VOLUME);
    Mix_VolumeChunk(audio->chunk, volume);
    return NEW_NUM((double)volume / MIX_MAX_VOLUME);
}

Value au_pause(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "pause");
    int channel = active_channel(audio);
    if (channel < 0)
        return NEW_BOOL(false);
    Mix_Pause(channel);
    return NEW_BOOL(true);
}

Value au_resume(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "resume");
    int channel = active_channel(audio);
    if (channel < 0)
        return NEW_BOOL(false);
    Mix_Resume(channel);
    return NEW_BOOL(true);
}

Value au_stop(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "stop");
    int channel = active_channel(audio);
    if (channel < 0)
        return NEW_BOOL(false);
    Mix_HaltChannel(channel);
    audio->channel = -1;
    return NEW_BOOL(true);
}

Value au_isPlaying(vm_t *vm, int argc, Value *argv)
{
    ObjAudio *audio = require_audio(vm, argc, argv, "is_playing");
    return NEW_BOOL(active_channel(audio) >= 0);
}

Value au_playMusic(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[audio.play_music] expects a file path string.");

    int loops = 0;
    if (argc > 1)
    {
        if (!IS_NUM(argv[1]) || AS_NUM(argv[1]) < -1 || AS_NUM(argv[1]) != (int)AS_NUM(argv[1]))
            vm_error(vm, "[audio.play_music] loops must be an integer greater than or equal to -1.");
        loops = (int)AS_NUM(argv[1]);
    }

    ensure_mixer(vm);
    const char *path = AS_CSTRING(argv[0]);
    char *resolved_path = resolve_path(vm, path);
    if (!resolved_path)
        vm_error(vm, "[audio.play_music] out of memory.");

    Mix_Music *music = Mix_LoadMUS(resolved_path);
    free(resolved_path);
    if (!music)
        vm_errorf(vm, "[audio.play_music] failed to load '%s': %s", path, Mix_GetError());

    Mix_HaltMusic();
    if (current_music)
        Mix_FreeMusic(current_music);
    current_music = music;

    if (Mix_PlayMusic(current_music, loops) < 0)
        vm_errorf(vm, "[audio.play_music] failed to play '%s': %s", path, Mix_GetError());
    return NEW_BOOL(true);
}

Value au_musicIsPlaying(vm_t *vm, int argc, Value *argv)
{
    return NEW_BOOL(Mix_PlayingMusic() || Mix_PausedMusic());
}

Value au_stopMusic(vm_t *vm, int argc, Value *argv)
{
    Mix_HaltMusic();
    return NEW_BOOL(true);
}

static BuiltinFunc audio_funcs[] = {
    {"load", au_load},
    {"sample_rate", au_sampleRate},
    {"channels", au_channels},
    {"frames", au_frames},
    {"duration", au_duration},
    {"format", au_format},
    {"play", au_play},
    {"volume", au_volume},
    {"pause", au_pause},
    {"resume", au_resume},
    {"stop", au_stop},
    {"is_playing", au_isPlaying},
    {"play_music", au_playMusic},
    {"music_isPlaying", au_musicIsPlaying},
    {"stop_music", au_stopMusic},
};

static BuiltinConst audio_consts[] = {};

DEFINE_BUILTIN_MODULE(module_audio, "audio", audio_funcs, audio_consts);
