#include <SDL2/SDL.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int quit = 0;
    SDL_Event event;
    SDL_Window *window = NULL;
    SDL_Renderer *render = NULL;
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("title", 
                    0, 
                    0,
                    200,
                    200,
                    SDL_WINDOW_SHOWN);
    if (!window)
    {
        SDL_Log("Failed to create window!\n");
        goto _EXIT;
    }

    render = SDL_CreateRenderer(window, -1, 0);
    if(!render)
    {
        SDL_Log("Failed to create render");
        goto __DWINDOW;
    }
    SDL_SetRenderDrawColor(render, 255, 0, 0, 255);
    SDL_RenderClear(render);
    SDL_RenderPresent(render);
    quit = 1;
    do
    {

        SDL_WaitEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            quit = 0;
            break;
        
        default:
            SDL_Log("Event type is %d\n", event.type);
            break;
        }
    } while (quit);

__DWINDOW:
    SDL_DestroyWindow(window);
_EXIT:
    SDL_Quit();
    return 0;
}