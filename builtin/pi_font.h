#ifndef PI_FONT_H
#define PI_FONT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TTF_Font *pi_openDefaultFont(int size)
{
    TTF_Font *font = TTF_OpenFont("resources/JetBrainsMono-Light.ttf", size);
    if (font)
        return font;

    char *base_path = SDL_GetBasePath();
    if (base_path)
    {
        const char *relative_paths[] = {
            "../resources/JetBrainsMono-Light.ttf",
            "resources/JetBrainsMono-Light.ttf"};
        for (size_t i = 0; i < sizeof(relative_paths) / sizeof(relative_paths[0]); i++)
        {
            size_t length = strlen(base_path) + strlen(relative_paths[i]) + 1;
            char *path = (char *)malloc(length);
            if (!path)
                continue;

            snprintf(path, length, "%s%s", base_path, relative_paths[i]);
            font = TTF_OpenFont(path, size);
            free(path);
            if (font)
                break;
        }
        SDL_free(base_path);
    }

    if (font)
        return font;

    const char *fallbacks[] = {
        "release/VeraMono.ttf",
        "VeraMono.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc"};
    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++)
    {
        font = TTF_OpenFont(fallbacks[i], size);
        if (font)
            return font;
    }

    return NULL;
}

#endif
