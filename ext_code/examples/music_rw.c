#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_mixer.h>
  
#include "hex_wav.h"
 
int main(int argc, char **argv)
{
    const int w = 320;
    const int h = 240;
    const int bpp = 16;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
 
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    window = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
 
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096);
    SDL_RWops *rw = SDL_RWFromMem(hex_wav, sizeof(hex_wav));
    Mix_Music *music = Mix_LoadMUS_RW(rw, 1);
    Mix_PlayMusic(music, 1);
 
    SDL_Delay(3000);
 
    Mix_HaltMusic();
    Mix_FreeMusic(music);
    Mix_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
