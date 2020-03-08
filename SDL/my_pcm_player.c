#include <stdio.h>
#include <SDL2/SDL.h>

#define BLOCK_SIZE 4096000

static size_t buf_len = 0;
static Uint8 *audio_buf = NULL;
static Uint8 *audio_pos = NULL;

//stream声卡缓冲区地址 len声卡缓冲区地址
void read_audio_data(void *udata, Uint8 *stream, int len)
{
    if (0 == buf_len)
    {
        SDL_Log("0 == buf_len!\n");
        return;
    }

    SDL_memset(stream, 0, len);
    
    len = (len < buf_len) ? len : buf_len;
    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    
    audio_pos += len;
    buf_len -= len;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    char *path = "./test.pcm";
    FILE *audio_fd = NULL;
    SDL_AudioSpec spec;

    
    ret = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER); //对音频初始化
    if (ret < 0)
    {
        SDL_Log("Failed to initial!\n");
        goto __END;
    }
    audio_fd = fopen(path, "r");
    if (!audio_fd)
    {
        SDL_Log("Failed to open file!\n");
        ret = -1;
        goto __END;
    }

    audio_buf = (Uint8*)malloc(BLOCK_SIZE);
    if (!audio_buf)
    {
        SDL_Log("Failed to alloc memory!\n");
        ret = -1;
        goto __END;
    }
    
    spec.freq = 44100;
    spec.channels = 2;
    spec.format = AUDIO_S16SYS;
    spec.silence = 0;
    spec.samples = 1024;
    spec.callback = read_audio_data;//声卡需要数据时会调用这个回调函数
    spec.userdata = NULL;//回调函数参数

    ret = SDL_OpenAudio(&spec, NULL);
    if (ret < 0)
    {
        SDL_Log("Failed to open audio device!\n");
        goto __END;
    }

    SDL_PauseAudio(0);//0是播放 1是停止
    SDL_Log("SDL_PauseAudio(0);\n");
    do
    {
        buf_len = fread(audio_buf, 1, BLOCK_SIZE, audio_fd);
        audio_pos = audio_buf;
        SDL_Log("read data!\n");
        while (audio_pos < (audio_buf + buf_len))
        {
            SDL_Delay(1);
        }
    } while (buf_len != 0);
    
     
    SDL_CloseAudio();

    ret = 0;

__END:
    if (audio_fd)
    {
        fclose(audio_fd);
    }

    if (audio_buf)
    {
        free(audio_buf);
    }
    
    
    SDL_Quit();

    return ret;
}