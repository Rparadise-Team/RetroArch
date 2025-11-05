#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
 
int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_GLContext context = {0};
    const GLubyte *version = NULL;
 
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
 
    window = SDL_CreateWindow("main", 0, 0, 320, 240, SDL_WINDOW_OPENGL);
    context = SDL_GL_CreateContext(window);
    version = glGetString(GL_VERSION);
    printf("Version:%s\n", version);
    SDL_GL_DeleteContext(context);
    return 0;
}
