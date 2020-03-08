#include <stdio.h>
#include <SDL2/SDL.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000 / 3 * 2

//compatibility with newer API
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(55, 28, 1)

#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame

#endif

struct SwrContext *audio_convert_ctx = NULL;

typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
}PacketQueue;

PacketQueue audioq;

int quit = 0;

void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *queue, AVPacket *pkt)
{
    AVPacketList *pkt_list_node;
    if (av_dup_packet(pkt) < 0) //引用计数加1
    {
        return -1;
    }
    pkt_list_node = av_malloc(sizeof(AVPacketList));
    if (!pkt_list_node)
    {
        return -1;
    }
    pkt_list_node->pkt = *pkt;
    pkt_list_node->next = NULL;

    SDL_LockMutex(queue->mutex);

    if (!queue->last_pkt)
    {
        queue->first_pkt = pkt_list_node;
    }
    else
    {
        queue->last_pkt->next = pkt_list_node;
    }
    
    queue->last_pkt = pkt_list_node;
    queue->nb_packets++;
    queue->size += pkt_list_node->pkt.size;
    SDL_CondSignal(queue->cond);

    SDL_UnlockMutex(queue->mutex);
    
    return 0;
}

int packet_queue_get(PacketQueue *queue, AVPacket *pkt, int block)
{
    AVPacketList *pkt_list_node;
    int ret = 0;

    SDL_LockMutex(queue->mutex);

    for(;;)
    {
        if (quit)
        {
            fprintf(stderr, "packet_queue_get quit\n");
            ret = -1;
            break;
        }
        
        pkt_list_node = queue->first_pkt;
        if (pkt_list_node)
        {
            queue->first_pkt = pkt_list_node->next;
            if (!queue->first_pkt)
            {
                queue->last_pkt = NULL;
            }
            queue->nb_packets--;
            queue->size -= pkt_list_node->pkt.size;
            *pkt = pkt_list_node->pkt;
            av_free(pkt_list_node);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }

    SDL_UnlockMutex(queue->mutex);
    return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;
    
    int len, data_size = 0;

    for(;;)
    {
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;
            //解码
            len = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
            if (len < 0)
            {
                //if error skip frame
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len;
            audio_pkt_size -= len;
            data_size = 0;
            if (got_frame)
            {
                data_size = 2 * 2 * frame.nb_samples;
                assert(data_size <= buf_size);
                //转化音频格式
                swr_convert(audio_convert_ctx, 
                            &audio_buf,
                            MAX_AUDIO_FRAME_SIZE * 3 / 2,
                            (const uint8_t **)frame.data,
                            frame.nb_samples);
            }
            if (data_size <= 0)
            {   
                //no data yet, got more frames
                continue;
            }
            //we have data, return it and come back for more later
            return data_size;
        }
        
        if(pkt.data)
        {
            av_free_packet(&pkt);
        }

        if (quit)
        {
            return -1;
        }
        
        if (packet_queue_get(&audioq, &pkt, 1) < 0)
        {
            fprintf(stderr, "queue put error");
            return -1;
        }
        
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}

//音频处理核心函数 stream声卡缓存
void audio_callback(void *userdata, Uint8 *stream, int len)
{
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len_temp, audio_size;
    
    static uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE * 3 / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    //声卡缓存区未满
    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {
            //we have already sent all our data, get more
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            if (audio_size < 0)
            {
                //if error out silence
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size); //设置静默音
            }
            else
            {
                audio_buf_size = audio_size;   
            }
            audio_buf_index = 0;
        }
        len_temp = audio_buf_size - audio_buf_index;
        if (len_temp > len)
        {
            len_temp = len;
        }
        // fprintf(stderr, "index = %d, len_temp = %d, len = %d\n",
        //         audio_buf_index,
        //         len_temp,
        //         len);
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len_temp);
        len -= len_temp;
        stream += len_temp;
        audio_buf_index += len_temp;
    }
}

int main(int argc, char *argv[])
{
    int ret = -1;
    AVFormatContext *pFormatCtx = NULL;//for opening multi-media file
    int i = 0, videoStream = 0, audioStream = 0;

    AVCodecContext *pCodecCtxOrig = NULL;//codec context
    AVCodecContext *pCodecCtx = NULL;

    struct SwsContext *sws_ctx = NULL;//视频裁剪上下文
    
    AVCodec *pCodec = NULL;
    AVFrame *pFrame = NULL;
    AVPacket packet;

    int frameFinished = 0;
    float aspect_ratio = 0.0;

    //for audio decode
    AVCodecContext *aCodecCtxOrig = NULL;
    AVCodecContext *aCodecCtx = NULL;
    AVCodec *aCodec = NULL;
    struct SwrContext *swr_ctx = NULL;
 
    int64_t in_channel_layout;
    int64_t out_channel_layout;


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

    //for audio
    SDL_AudioSpec wanted_spec, spec;

    SDL_Event event;

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
    av_register_all();

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
    audioStream = -1;

    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (AVMEDIA_TYPE_VIDEO == pFormatCtx->streams[i]->codec->codec_type && videoStream < 0)
        {
            videoStream = i;
        }
        if (AVMEDIA_TYPE_AUDIO == pFormatCtx->streams[i]->codec->codec_type && audioStream < 0)
        {
            audioStream = i;
        }
    }
    
    if (-1 == videoStream)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could't find a video stream!\n");
        ret = -1;
        goto __END;
    }

    if (-1 == audioStream)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could't find a audio stream!\n");
        ret = -1;
        goto __END;
    }

    //copy context
    aCodecCtxOrig = pFormatCtx->streams[audioStream]->codec;
    aCodec = avcodec_find_decoder(aCodecCtxOrig->codec_id);
    if (NULL == aCodec)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could't find a audio codec!\n");
        ret = -1;
        goto __END;
    }

    //copy context
    aCodecCtx = avcodec_alloc_context3(aCodec);
    ret = avcodec_copy_context(aCodecCtx, aCodecCtxOrig);
    if (ret != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't copy codec context\n");
        goto __END;
    }


    //set audio setting from codec info
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    //打开音频设备
    ret = SDL_OpenAudio(&wanted_spec, &spec);
    if (ret < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open audio device - %s!\n", SDL_GetError());
        goto __END;
    }

    //打开编码器
    avcodec_open2(aCodecCtx, aCodec, NULL);
    
    //初始化音频队列
    packet_queue_init(&audioq);

    in_channel_layout = av_get_default_channel_layout(aCodecCtx->channels);
    out_channel_layout = in_channel_layout;//AV_CH_LAYOUT_STEREO
    SDL_Log("in_channel_layout = %ld, out_channel_layout = %ld\n", in_channel_layout, out_channel_layout);
    
    //多媒体文件参数多种多样 对于声卡设备固定为1种格式
    audio_convert_ctx = swr_alloc(); //音频重采样的上下文
    if (audio_convert_ctx)
    {
        swr_alloc_set_opts(audio_convert_ctx,
                           out_channel_layout,
                           AV_SAMPLE_FMT_S16,
                           aCodecCtx->sample_rate,
                           in_channel_layout, //通道数
                           aCodecCtx->sample_fmt,
                           aCodecCtx->sample_rate,
                           0,
                           NULL);
    }
    swr_init(audio_convert_ctx);

    SDL_PauseAudio(0);

    //get a pointer to the codec context for the video stream
    pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;
    //find the decoder for the video stream
    pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
    if (NULL == pCodec)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could't find a video codec!\n");
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
                
                //解码后的yuv数据放到pict里 然后pict的数据放到纹理里
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
        else if(packet.stream_index == audioStream)
        {
            //for audio 
            packet_queue_put(&audioq, &packet);
        }
        else
        {
            //free the packet that was allocated by av_read_frame
            av_free_packet(&packet);
        }
        
        SDL_PollEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            quit = 1;
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
    
    if (aCodecCtxOrig)
    {
        avcodec_close(aCodecCtx);
    }
    if (aCodecCtx)
    {
        avcodec_close(aCodecCtx);
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