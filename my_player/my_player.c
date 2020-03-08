#include <stdio.h>
#include <SDL2/SDL.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

//compatibility with newer API
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(55, 28, 1)

#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame

#endif

int main(int argc, char *argv[])
{
    int ret = -1;
    AVFormatContext *pFormatCtx = NULL;//for opening multi-media file
    int i = 0, videoStream = 0;

    AVCodecContext *pCodecCtxOrig = NULL;//codec context
    AVCodecContext *pCodecCtx = NULL;

    struct SwsContext *sws_ctx = NULL;//视频裁剪上下文
    
    AVCodec *pCodec = NULL;
    AVFrame *pFrame = NULL;
    AVPacket packet;

    int frameFinished = 0;
    float aspect_ratio = 0.0;

    AVPicture *pict = NULL;//存放yuv
    
    SDL_Rect rect;
    Uint32 pixformat = 0;
    
    //for render
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    //set default size of window
    int w_width = 640;
    int w_height = 480;

    if (argc < 2)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Usage: command <file>\n");
        ret = -1;
        goto __END;
    }
    
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize SDL - %s\n", SDL_GetError());
        goto __END;
    }
    
    //Register all formats and codec
    // av_register_all();

    //open video file
    ret = avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);
    if (ret != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open video file!\n");
        goto __END;
    }
    
    //retrieve stream information 看上下文中有没有流信息
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to find stream information!\n");
        goto __END;
    }
    
    //dump information about file noto standard output
    av_dump_format(pFormatCtx, 0, argv[1], 0);
    
    //find the first video stream 
    videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (AVMEDIA_TYPE_VIDEO == pFormatCtx->streams[i]->codec->codec_type)
        {
            videoStream = i;
            break;
        }
    }
    
    if (-1 == videoStream)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could't find a video stream!\n");
        ret = -1;
        goto __END;
    }
    
    //get a pointer to the codec context for the video stream
    pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

    //find the decoder for the video stream
    pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
    if (NULL == pCodec)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could't find a video stream!\n");
        ret = -1;
        goto __END;
    }
    
    //copy context 为了不破坏原有信息
    pCodecCtx = avcodec_alloc_context3(pCodec);
    ret = avcodec_copy_context(pCodecCtx, pCodecCtxOrig);
    if (ret != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't copy codec context\n");
        goto __END;
    }
    
    //open codec
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open decoder\n");
        goto __END;
    }
    
    //allocate video frame 解码后的数据帧
    pFrame = av_frame_alloc();
    
    w_width = pCodecCtx->width;
    w_height = pCodecCtx->height;

    win = SDL_CreateWindow("my_player",
                           SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED,
                           w_width, w_height,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window by SDL\n");
        ret = -1;
        goto __END;
    }
    
    renderer = SDL_CreateRenderer(win, -1, 0);
    if (!renderer)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create renderer by SDL\n");
        ret = -1;
        goto __END;
    }

    //纹理存放的是yuv数据
    pixformat = SDL_PIXELFORMAT_IYUV;
    texture = SDL_CreateTexture(renderer, 
                                pixformat, 
                                SDL_TEXTUREACCESS_STREAMING,
                                w_width,
                                w_height);
    
    //initialize sws context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width,//原始宽高
                             pCodecCtx->height,
                             pCodecCtx->pix_fmt,
                             pCodecCtx->width,//目的宽高
                             pCodecCtx->height,
                             AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);

    pict = (AVPicture*)malloc(sizeof(AVPicture));
    avpicture_alloc(pict,
                    AV_PIX_FMT_YUV420P,
                    pCodecCtx->width,
                    pCodecCtx->height);

    //read frames and save first five frames to disk
    while(av_read_frame(pFormatCtx, &packet) >= 0)
    {   
        SDL_Delay(40);
        //解码加渲染
        //is this a packet from the video stream 
        if (packet.stream_index == videoStream)
        {
            //decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            
            //do ww get a video frame
            if (frameFinished)
            {
                //convert the image into yuv format that SDL uses
                sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, 
                          pFrame->linesize, 0, pCodecCtx->height,
                          pict->data, pict->linesize);
                
                //数据放到pict里 然后pict的数据放到纹理里
                SDL_UpdateYUVTexture(texture, NULL,
                                     pict->data[0], pict->linesize[0], //y
                                     pict->data[1], pict->linesize[1], //u
                                     pict->data[2], pict->linesize[2]); //v
                //set size of window
                rect.x = 0;
                rect.y = 0;
                rect.w = pCodecCtx->width;
                rect.h = pCodecCtx->height;

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, &rect);
                SDL_RenderPresent(renderer);
            }
        }
        //free the packet that was allocated by av_read_frame
        av_free_packet(&packet);

        SDL_Event event;
        SDL_PollEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            goto __END;
            break;
        
        default:
            break;
        }
    }
__END:
    //free the yuv frame
    if (pFrame)
    {
        av_frame_free(&pFrame);
    }
    
    //close the codec
    if (pCodecCtx)
    {
        avcodec_close(pCodecCtx);
    }
    
    if (pCodecCtxOrig)
    {
        avcodec_close(pCodecCtxOrig);
    }

    //close the video file
    if (pFormatCtx)
    {
        avformat_close_input(&pFormatCtx);
    }

    if (pict)
    {
        avpicture_free(pict);
    }
    
    if (win)
    {
        SDL_DestroyWindow(win);
    }
    
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
    }
    
    if (texture)
    {
        SDL_DestroyTexture(texture);
    }
    
    SDL_Quit();

    return ret;
}