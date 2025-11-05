#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_image.h>
 
#include "hex_png.h"
   
int main(int argc, char **argv)
{
    const int w = 320;
    const int h = 240;
    const int bpp = 16;
    SDL_Window *window = NULL;
    SDL_Surface *screen = NULL;
    SDL_Texture *texture = NULL;
    SDL_Renderer *renderer = NULL;
 
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    screen = SDL_CreateRGBSurface(0, w, h, bpp, 0, 0, 0, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
 
    SDL_RWops *rw = SDL_RWFromMem(hex_png, sizeof(hex_png));
    SDL_Surface *png = IMG_Load_RW(rw, 1);
    SDL_BlitSurface(png, NULL, screen, NULL);
    SDL_FreeSurface(png);
 
    SDL_UpdateTexture(texture, NULL, screen->pixels, screen->pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
 
    SDL_Delay(30000);
 
    SDL_FreeSurface(screen);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
