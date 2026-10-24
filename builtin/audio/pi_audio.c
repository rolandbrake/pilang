#include "../pi_kwargs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "pi_audio.h"
#include "../pi_builtin.h"
#include "../../src/pi_object.h"

EM_JS(int, _web_audio_load, (const char *path), {
    Module.piAudio ||= [];

    const a = new Audio(UTF8ToString(path));
    Module.piAudio.push(a);

    return Module.piAudio.length - 1;
});

EM_JS(void, _web_audio_play, (int id, int loops), {
    const a = Module.piAudio && Module.piAudio[id];
    if (!a)
        return;

    let remaining = loops;

    a.loop = loops < 0;
    a.onended = loops > 0
        ? () =>
    {
        if (remaining-- > 0)
        {
            a.currentTime = 0;
            a.play().catch(() => {});
        }
    }
        : null;

        a.currentTime = 0;
        a.play().catch(() => {});
});

EM_JS(void, _web_audio_volume, (int id, double value), {
    const a = Module.piAudio && Module.piAudio[id];

    if (a)
        a.volume = value;
});

EM_JS(void, _web_audio_pause, (int id), {
    const a = Module.piAudio && Module.piAudio[id];

    if (a)
        a.pause();
});

EM_JS(void, _web_audio_stop, (int id), {
    const a = Module.piAudio && Module.piAudio[id];

    if (a)
    {
        a.pause();
        a.currentTime = 0;
    }
});

EM_JS(int, _web_audio_playing, (int id), {
    const a = Module.piAudio && Module.piAudio[id];

    return !!(a && !a.paused && !a.ended);
});

EM_JS(void, _web_music_play, (const char *path, int loops), {
    const music = new Audio(UTF8ToString(path));

    if (Module.piMusic)
        Module.piMusic.pause();

    music.loop = loops < 0;

    Module.piMusic = music;
    Module.piMusicRequested = true;

    const play = function()
    {
        music.play().catch(function(){});
    };

    play();

    if (!Module.piAudioUnlock)
    {
        Module.piAudioUnlock = function()
        {
            if (Module.piMusicRequested && Module.piMusic)
                Module.piMusic.play().catch(function(){});
        };

        document.addEventListener(
            'pointerdown',
            Module.piAudioUnlock,
            {once : true});

        document.addEventListener(
            'keydown',
            Module.piAudioUnlock,
            {once : true});
    }
});

EM_JS(int, _web_music_playing, (), {
    const a = Module.piMusic;

    return !!(Module.piMusicRequested && a && !a.ended);
});

EM_JS(void, _web_music_stop, (), {
    if (Module.piMusic)
    {
        Module.piMusic.pause();
        Module.piMusic.currentTime = 0;
    }

    Module.piMusicRequested = false;
});

static ObjAudio *_web_audio(VM *vm, Value value)
{
    if (!IS_AUDIO(value))
        vm_error(vm, "audio expects a clip");
    return AS_AUDIO(value);
}

Value au_load(VM *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[audio.load] expects a file path string.");
    ObjAudio *a = (ObjAudio *)alloc_object(sizeof(*a), OBJ_AUDIO);
    a->browser_id = _web_audio_load(AS_CSTRING(argv[0]));
    return NEW_OBJ(add_obj(vm, (Object *)a));
}

Value au_play(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[audio.play] expects an audio clip.");
    int loops = 0;
    if (argc > 1 && !IS_NIL(argv[1]))
    {
        if (!IS_NUM(argv[1]) || AS_NUM(argv[1]) < -1 || AS_NUM(argv[1]) != (int)AS_NUM(argv[1]))
            vm_error(vm, "[audio.play] loops must be an integer greater than or equal to -1.");
        loops = (int)AS_NUM(argv[1]);
    }
    PiMap *kwargs = _kw_map(vm);
    if (kwargs)
    {
        Value value;
        if (_kw_get(kwargs, "loops", &value) && !IS_NIL(value))
        {
            if (!IS_NUM(value) || AS_NUM(value) < -1 || AS_NUM(value) != (int)AS_NUM(value))
                vm_error(vm, "[audio.play] loops must be an integer greater than or equal to -1.");
            loops = (int)AS_NUM(value);
        }
    }
    _web_audio_play(_web_audio(vm, argv[0])->browser_id, loops);
    return NEW_BOOL(TRUE);
}

Value au_volume(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[1]))
        vm_error(vm, "[audio.volume] expects a number.");
    _web_audio_volume(_web_audio(vm, argv[0])->browser_id, AS_NUM(argv[1]));
    return NEW_NIL();
}

Value au_pause(VM *vm, int argc, Value *argv)
{
    _web_audio_pause(_web_audio(vm, argv[0])->browser_id);
    return NEW_NIL();
}

Value au_resume(VM *vm, int argc, Value *argv)
{
    _web_audio_play(_web_audio(vm, argv[0])->browser_id, 0);
    return NEW_NIL();
}

Value au_stop(VM *vm, int argc, Value *argv)
{
    _web_audio_stop(_web_audio(vm, argv[0])->browser_id);
    return NEW_NIL();
}

Value au_isPlaying(VM *vm, int argc, Value *argv) { 
    return NEW_BOOL(_web_audio_playing(_web_audio(vm, argv[0])->browser_id)); 
}
Value au_sampleRate(VM *vm, int argc, Value *argv) { return NEW_NUM(0); }
Value au_channels(VM *vm, int argc, Value *argv) { return NEW_NUM(0); }
Value au_frames(VM *vm, int argc, Value *argv) { return NEW_NUM(0); }
Value au_duration(VM *vm, int argc, Value *argv) { return NEW_NUM(0); }
Value au_format(VM *vm, int argc, Value *argv) { return NEW_NIL(); }
Value au_playMusic(VM *vm, int argc, Value *argv)
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
    PiMap *kwargs = _kw_map(vm);
    if (kwargs)
    {
        Value value;
        if (_kw_get(kwargs, "loops", &value) && !IS_NIL(value))
        {
            if (!IS_NUM(value) || AS_NUM(value) < -1 || AS_NUM(value) != (int)AS_NUM(value))
                vm_error(vm, "[audio.play_music] loops must be an integer greater than or equal to -1.");
            loops = (int)AS_NUM(value);
        }
    }
    _web_music_play(AS_CSTRING(argv[0]), loops);
    return NEW_BOOL(TRUE);
}
Value au_musicIsPlaying(VM *vm, int argc, Value *argv) { return NEW_BOOL(_web_music_playing()); }
Value au_stopMusic(VM *vm, int argc, Value *argv)
{
    _web_music_stop();
    return NEW_BOOL(TRUE);
}
static BuiltinFunc audio_funcs[] = {{"load", au_load}, {"sample_rate", au_sampleRate}, {"channels", au_channels}, {"frames", au_frames}, {"duration", au_duration}, {"format", au_format}, {"play", au_play}, {"volume", au_volume}, {"pause", au_pause}, {"resume", au_resume}, {"stop", au_stop}, {"is_playing", au_isPlaying}, {"play_music", au_playMusic}, {"music_isPlaying", au_musicIsPlaying}, {"stop_music", au_stopMusic}};
static BuiltinConst audio_consts[] = {};
DEFINE_BUILTIN_MODULE(module_audio, "audio", audio_funcs, audio_consts);
#else
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "pi_audio.h"
#include "../pi_builtin.h"
#include "../../src/common.h"
#include "../../src/pi_object.h"
#include "../../src/pi_value.h"

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

static void _ensure_mixer(VM *vm)
{
    static bool opened = FALSE;
    if (opened)
        return;

    Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
    if (Mix_OpenAudio(48000, AUDIO_F32SYS, 2, 1024) < 0)
        vm_errorf(vm, "[audio] failed to open audio output: %s", Mix_GetError());
    opened = TRUE;
}

static int _active_channel(ObjAudio *audio)
{
    if (audio->channel < 0 || Mix_GetChunk(audio->channel) != audio->chunk ||
        (!Mix_Playing(audio->channel) && !Mix_Paused(audio->channel)))
    {
        audio->channel = -1;
        return -1;
    }
    return audio->channel;
}

static ObjAudio *_require_audio(VM *vm, int argc, Value *argv, const char *name)
{
    if (argc < 1 || !IS_AUDIO(argv[0]))
        vm_errorf(vm, "[audio.%s] expects an audio clip.", name);
    return AS_AUDIO(argv[0]);
}

Value au_load(VM *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[audio.load] expects a file path string.");

    const char *path = AS_CSTRING(argv[0]);
    char *resolved_path = resolve_path(vm, path);
    if (!resolved_path)
        vm_error(vm, "[audio.load] out of memory.");

    _ensure_mixer(vm);
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

Value au_sampleRate(VM *vm, int argc, Value *argv)
{
    return NEW_NUM(_require_audio(vm, argc, argv, "sample_rate")->spec.freq);
}

Value au_channels(VM *vm, int argc, Value *argv)
{
    return NEW_NUM(_require_audio(vm, argc, argv, "channels")->spec.channels);
}

Value au_frames(VM *vm, int argc, Value *argv)
{
    return NEW_NUM(_require_audio(vm, argc, argv, "frames")->frames);
}

Value au_duration(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "duration");
    return NEW_NUM((double)audio->frames / audio->spec.freq);
}

Value au_format(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "format");
    const char *format = SDL_AUDIO_ISFLOAT(audio->spec.format) ? "f32" : "pcm";
    return NEW_OBJ(add_obj(vm, new_pistring(strdup(format))));
}

Value au_play(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "play");
    int loops = 0;
    if (argc > 1 && !IS_NIL(argv[1]))
    {
        if (!IS_NUM(argv[1]) || AS_NUM(argv[1]) < -1 || AS_NUM(argv[1]) != (int)AS_NUM(argv[1]))
            vm_error(vm, "[audio.play] loops must be an integer greater than or equal to -1.");
        loops = (int)AS_NUM(argv[1]);
    }
    PiMap *kwargs = _kw_map(vm);
    if (kwargs)
    {
        Value value;
        if (_kw_get(kwargs, "loops", &value) && !IS_NIL(value))
        {
            if (!IS_NUM(value) || AS_NUM(value) < -1 || AS_NUM(value) != (int)AS_NUM(value))
                vm_error(vm, "[audio.play] loops must be an integer greater than or equal to -1.");
            loops = (int)AS_NUM(value);
        }
    }

    int current = _active_channel(audio);
    if (current >= 0)
        Mix_HaltChannel(current);

    int channel = Mix_PlayChannel(-1, audio->chunk, loops);
    if (channel < 0)
        vm_errorf(vm, "[audio.play] failed to play: %s", Mix_GetError());
    audio->channel = channel;
    return NEW_NUM(channel);
}

Value au_volume(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "volume");
    if (argc < 2 || !IS_NUM(argv[1]) || AS_NUM(argv[1]) < 0 || AS_NUM(argv[1]) > 1)
        vm_error(vm, "[audio.volume] expects a clip and a volume from 0 to 1.");

    int volume = (int)(AS_NUM(argv[1]) * MIX_MAX_VOLUME);
    Mix_VolumeChunk(audio->chunk, volume);
    return NEW_NUM((double)volume / MIX_MAX_VOLUME);
}

Value au_pause(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "pause");
    int channel = _active_channel(audio);
    if (channel < 0)
        return NEW_BOOL(FALSE);
    Mix_Pause(channel);
    return NEW_BOOL(TRUE);
}

Value au_resume(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "resume");
    int channel = _active_channel(audio);
    if (channel < 0)
        return NEW_BOOL(FALSE);
    Mix_Resume(channel);
    return NEW_BOOL(TRUE);
}

Value au_stop(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "stop");
    int channel = _active_channel(audio);
    if (channel < 0)
        return NEW_BOOL(FALSE);
    Mix_HaltChannel(channel);
    audio->channel = -1;
    return NEW_BOOL(TRUE);
}

Value au_isPlaying(VM *vm, int argc, Value *argv)
{
    ObjAudio *audio = _require_audio(vm, argc, argv, "is_playing");
    return NEW_BOOL(_active_channel(audio) >= 0);
}

Value au_playMusic(VM *vm, int argc, Value *argv)
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

    _ensure_mixer(vm);
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
    return NEW_BOOL(TRUE);
}

Value au_musicIsPlaying(VM *vm, int argc, Value *argv)
{
    return NEW_BOOL(Mix_PlayingMusic() || Mix_PausedMusic());
}

Value au_stopMusic(VM *vm, int argc, Value *argv)
{
    Mix_HaltMusic();
    return NEW_BOOL(TRUE);
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
#endif
