#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "pi_audio.h"
#include "../pi_builtin.h"
#include "../../common.h"
#include "../../pi_object.h"
#include "../../pi_value.h"

ObjAudio *new_audio(SDL_AudioSpec spec, Uint8 *buffer, Uint32 length)
{
    ObjAudio *audio = (ObjAudio *)alloc_object(sizeof(ObjAudio), OBJ_AUDIO);
    audio->spec = spec;
    audio->buffer = buffer;
    audio->length = length;
    return audio;
}

static bool is_absolute_path(const char *path)
{
    if (!path || !path[0])
        return false;

#ifdef _WIN32
    return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
           (path[0] == '\\' && path[1] == '\\');
#else
    return path[0] == '/';
#endif
}

static char *resolve_audio_path(vm_t *vm, const char *path)
{
    if (is_absolute_path(path) || !vm || !vm->current_path)
        return strdup(path);

    size_t length = strlen(vm->current_path) + 1 + strlen(path) + 1;
    char *resolved = malloc(length);
    if (resolved)
        snprintf(resolved, length, "%s/%s", vm->current_path, path);
    return resolved;
}

Value au_load(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_STRING(argv[0]))
        vm_error(vm, "[audio.load] expects a file path string.");

    const char *path = AS_CSTRING(argv[0]);
    char *resolved_path = resolve_audio_path(vm, path);
    if (!resolved_path)
        vm_error(vm, "[audio.load] out of memory.");

    SDL_AudioSpec spec;
    Uint8 *buffer = NULL;
    Uint32 length = 0;
    if (!SDL_LoadWAV(resolved_path, &spec, &buffer, &length))
    {
        free(resolved_path);
        vm_errorf(vm, "[audio.load] failed to load WAV '%s': %s", path, SDL_GetError());
    }
    free(resolved_path);

    return NEW_OBJ(add_obj(vm, (Object *)new_audio(spec, buffer, length)));
}

static BuiltinFunc audio_funcs[] = {
    {"load", au_load},
};

static BuiltinConst audio_consts[] = {};

DEFINE_BUILTIN_MODULE(module_audio, "audio", audio_funcs, audio_consts);
