#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL2_gfxPrimitives.h>
  
int main(int argc, char **argv)
{
    const int w = 320;
    const int h = 240;
    const int bpp = 16;
    char buf[255] = {0};
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
 
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
 
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
 
    SDL_Event event;
    while (1) {
        if (SDL_PollEvent(&event)) {
            if (event.type == SDL_MOUSEMOTION) {
                int x = 0, y = 0;
 
                SDL_GetMouseState(&x, &y);
                sprintf(buf, "mouse pos: %d %d", x, y);
                SDL_RenderClear(renderer);
                stringRGBA(renderer, 100, 100, buf, 0xff, 0xff, 0xff, 0xff);
                SDL_RenderPresent(renderer);
            }
            if ((event.type == SDL_MOUSEBUTTONUP) || (event.type == SDL_MOUSEBUTTONDOWN)) {
                break;
            }
        }
        SDL_Delay(15);
    }
 
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
