#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL2_gfxPrimitives.h>
   
int main(int argc, char **argv)
{
    int num = 0;
    const int w = 320;
    const int h = 240;
    const int bpp = 16;
    char buf[255] = {0};
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
  
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    window = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
  
    SDL_Event event = {0};
    SDL_Joystick* joy = NULL;
 
    num = SDL_NumJoysticks();
    joy = SDL_JoystickOpen(0);
    printf("Joystick (%d, %p)\n", num, joy);
 
    while (1) {
        if (SDL_PollEvent(&event)) {
            if (event.type == SDL_JOYAXISMOTION) {
                if(event.jaxis.axis == 0) {
                    printf("X Axis %d\n", event.jaxis.value);
                }
                if(event.jaxis.axis == 1) {
                    printf("Y Axis %d\n", event.jaxis.value);
                }
            }
            if (event.type == SDL_JOYBUTTONUP) {
                printf("UP %d\n", event.jbutton.button);
            }
            if (event.type == SDL_JOYBUTTONDOWN) {
                printf("DOWN %d\n", event.jbutton.button);
            }
        }
        SDL_Delay(15);
    }
 
    SDL_JoystickClose(joy);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
