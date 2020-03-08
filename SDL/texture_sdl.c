#include <SDL2/SDL.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int quit = 0;
    SDL_Event event;
    SDL_Window *window = NULL;
    SDL_Renderer *render = NULL;
    SDL_Texture *texture = NULL; 
    SDL_Rect rect;

    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("title", 
                    0, 
                    0,
                    400,
                    400,
                    SDL_WINDOW_SHOWN);
    if (!window)
    {
        SDL_Log("Failed to create window!\n");
        goto __EXIT;
    }

    render = SDL_CreateRenderer(window, -1, 0);
    if(!render)
    {
        SDL_Log("Failed to create render");
        goto __DWINDOW;
    }
    // SDL_SetRenderDrawColor(render, 255, 0, 0, 255);
    // SDL_RenderClear(render);
    // SDL_RenderPresent(render);

    texture = SDL_CreateTexture(render, 
                      SDL_PIXELFORMAT_ABGR8888, 
                      SDL_TEXTUREACCESS_TARGET,
                      400,
                      400);
    if (!texture)
    {
        SDL_Log("Failed to create texture!\n");
        goto __RENDER;
    }

    quit = 1;
    do
    {
        rect.w = 30;
        rect.h = 30;
        rect.x = rand() % 400;
        rect.y = rand() % 400;
        //改变渲染器的目标
        SDL_SetRenderTarget(render, texture);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
        //对整体用SDL_RenderClear
        SDL_RenderClear(render);

        SDL_RenderDrawRect(render, &rect);
        SDL_SetRenderDrawColor(render, 255, 0, 0, 0);
        SDL_RenderFillRect(render, &rect);

        SDL_SetRenderTarget(render, NULL);
        SDL_RenderCopy(render, texture, NULL, NULL);

        SDL_RenderPresent(render);

        SDL_WaitEventTimeout(&event, 500);
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

    SDL_DestroyTexture(texture);

__RENDER:
    SDL_DestroyRenderer(render);
__DWINDOW:
    SDL_DestroyWindow(window);
__EXIT:
    SDL_Quit();
    return 0;
}