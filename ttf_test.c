#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("Calling TTF_Init()...\n");
    // if (TTF_Init() == -1)
    // {
    //     printf("TTF_Init failed: %s\n", TTF_GetError());
    //     SDL_Quit();
    //     return 1;
    // }
    printf("TTF_Init succeeded!\n");

    // TTF_Quit();
    SDL_Quit();
    return 0;
}