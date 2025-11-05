#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
   
const char *vShaderSrc =
    "attribute vec4 pos;    \n"
    "void main() {          \n"
    "    gl_Position = pos; \n"
    "}\n";
 
const char *fShaderSrc =
    "precision mediump float;                       \n"
    "void main() {                                  \n"
    "    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);   \n"
    "}\n";
   
int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_GLContext context = {0};
   
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
   
    window = SDL_CreateWindow("main", 0, 0, 320, 240, SDL_WINDOW_OPENGL);
    context = SDL_GL_CreateContext(window);
   
    GLuint vShader = 0;
    GLuint fShader = 0;
    GLuint pObject = 0;
   
    vShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vShader, 1, &vShaderSrc, NULL);
    glCompileShader(vShader);
   
    fShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fShader, 1, &fShaderSrc, NULL);
    glCompileShader(fShader);
   
    pObject = glCreateProgram();
    glAttachShader(pObject, vShader);
    glAttachShader(pObject, fShader);
    glLinkProgram(pObject);
    glUseProgram(pObject);
       
    GLfloat v[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };
  
    glViewport(0, 0, 320, 240);
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, v);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  
    SDL_GL_SwapWindow(window);
    SDL_Delay(30000);
   
    SDL_DestroyWindow(window);
    SDL_GL_DeleteContext(context);
    SDL_Quit();
    return 0;
}
