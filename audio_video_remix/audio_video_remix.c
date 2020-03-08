#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ADTX_HEADER_LEN 7

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

void adts_header(unsigned char *szAdtsHeader, int dataLen)
{
    //These parameters should be changed according to the condition
    int audio_object_type = 2;
    int sampling_frequency_index = 3;
    int channel_config = 2;

    int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;         //syncword:0xfff                          高8bits
    szAdtsHeader[1] = 0xf0;         //syncword:0xfff                          低4bits
    szAdtsHeader[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    szAdtsHeader[1] |= (0 << 1);    //Layer:0                                 2bits 
    szAdtsHeader[1] |= 1;           //protection absent:1                     1bit

    szAdtsHeader[2] = (audio_object_type - 1)<<6;            //profile:audio_object_type - 1                      2bits
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)<<2; //sampling frequency index:sampling_frequency_index  4bits 
    szAdtsHeader[2] |= (0 << 1);                             //private bit:0                                      1bit
    szAdtsHeader[2] |= (channel_config & 0x04)>>2;           //channel configuration:channel_config               高1bit

    szAdtsHeader[3] = (channel_config & 0x03)<<6;     //channel configuration:channel_config      低2bits
    szAdtsHeader[3] |= (0 << 5);                      //original：0                               1bit
    szAdtsHeader[3] |= (0 << 4);                      //home：0                                   1bit
    szAdtsHeader[3] |= (0 << 3);                      //copyright id bit：0                       1bit  
    szAdtsHeader[3] |= (0 << 2);                      //copyright id start：0                     1bit
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits

    szAdtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
    szAdtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);       //frame length:value    低3bits
    szAdtsHeader[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    szAdtsHeader[6] = 0xfc;
}

//Input format:
//media_file_video_path, media_file_video_start_time, media_file_video_end_time, media_file_audio_path, media_file_audio_start_time, media_file_audio_end_time, output_file_path
int main(int argc, char *argv[])
{   
    int ret = 0;

    char *mediaFileAudioPath = NULL;
    char *mediaFileVideoPath = NULL;
    char *outputFilePath = NULL;
    FILE *outputFd = NULL;
    AVFormatContext *audioFmtCtx = NULL;
    AVFormatContext *videoFmtCtx = NULL;
    AVFormatContext *outputFmtCtx = NULL;
    AVPacket audioPkt;
    memset(&audioPkt, 0, sizeof(audioPkt));
    AVPacket videoPkt;
    memset(&videoPkt, 0, sizeof(videoPkt));
    uint8_t adtsHeader[ADTX_HEADER_LEN] = {0};
    uint32_t audioIndex  = 0;
    uint32_t videoIndex = 0;
    AVOutputFormat *outputFmt = NULL;
    AVStream *audioStream = NULL;
    AVStream *videoStream = NULL;
    AVStream *outputStream = NULL;
    uint32_t audioStartTime = 0;
    uint32_t audioEndTime = 0;
    uint32_t videoStartTime = 0;
    uint32_t videoEndTime = 0;    
    int64_t audioDtsStart = 0;
    int64_t audioPtsStart = 0;
    int64_t videoDtsStart = 0;
    int64_t videoPtsStart = 0;
    AVStream *audioTempInStram = NULL;
    AVStream *videoTempInStram = NULL;
    AVStream *tempOutStram = NULL;
    
    av_log_set_level(AV_LOG_INFO);

    if (argc < 8)
    {
        av_log(NULL, AV_LOG_ERROR, "The count of params should be like:media_file_video_path, media_file_video_start_time, media_file_video_end_time, media_file_audio_path, media_file_audio_start_time, media_file_audio_end_time, output_file_path\n");
        ret = -1;
        goto END;
    }

    //获取音频流
    mediaFileAudioPath = argv[4];

    if (!mediaFileAudioPath)
    {
        av_log(NULL, AV_LOG_ERROR, "MediaFileAudioPath %s is NULL\n", mediaFileAudioPath);
        ret = -1;
        goto END;
    }
    
    ret = avformat_open_input(&audioFmtCtx, mediaFileAudioPath, NULL, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", av_err2str(ret));
        goto END;
    }
    
    
    
    av_dump_format(audioFmtCtx, 0, mediaFileAudioPath, 0);
    
    ret = av_find_best_stream(audioFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't find best stream:%s\n", av_err2str(ret));
        avformat_close_input(&audioFmtCtx);
        goto END;
    }
    audioIndex = ret;
    audioStream = audioFmtCtx->streams[audioIndex];
    av_log(NULL, AV_LOG_INFO, "%d\n", audioFmtCtx->streams[audioIndex]->index);
    
    //获取视频流
    mediaFileVideoPath = argv[1];

    if (!mediaFileVideoPath)
    {
        av_log(NULL, AV_LOG_ERROR, "MediaFileVideoPath %s is NULL\n", mediaFileVideoPath);
        ret = -1;
        goto END;
    }
    
    ret = avformat_open_input(&videoFmtCtx, mediaFileVideoPath, NULL, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", av_err2str(ret));
        goto END;
    }
    
    av_dump_format(videoFmtCtx, 0, mediaFileVideoPath, 0);
    
    ret = av_find_best_stream(videoFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't find best stream:%s\n", av_err2str(ret));
        avformat_close_input(&videoFmtCtx);
        goto END;
    }
    videoIndex = ret;
    videoStream = videoFmtCtx->streams[videoIndex];
    av_log(NULL, AV_LOG_INFO, "%d\n", videoFmtCtx->streams[videoIndex]->index);
    
    //申请输出文件的格式上下文
    avformat_alloc_output_context2(&outputFmtCtx, NULL, NULL, outputFilePath);
    if (!outputFmtCtx)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    outputFmt = outputFmtCtx->oformat;

    //拷贝音频流信息
    outputStream = avformat_new_stream(outputFmtCtx, audioStream->codec->codec);
    if (!outputStream)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to allcating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    
    ret = avcodec_copy_context(outputStream->codec, audioStream->codec);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failde to copy codec context from input stream to output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    outputStream->codec->codec_tag = 0;
    if (outputFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        outputStream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    //拷贝视频流信息
    outputStream = avformat_new_stream(outputFmtCtx, videoStream->codec->codec);
    if (!outputStream)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to allcating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    
    ret = avcodec_copy_context(outputStream->codec, videoStream->codec);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failde to copy codec context from input stream to output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    outputStream->codec->codec_tag = 0;
    if (outputFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        outputStream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    av_dump_format(outputFmtCtx, 0, outputFilePath, 1);
    if (!(outputFmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&outputFmtCtx->pb, outputFilePath, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", outputFilePath);
            goto END;
        }
    }
    
    ret = avformat_write_header(outputFmtCtx, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", outputFilePath);
        goto END;
    }

    //音频源移动到对应位置
    audioStartTime = atoi(argv[5]);
    ret = av_seek_frame(audioFmtCtx, audioIndex, audioStartTime * AV_TIME_BASE, AVSEEK_FLAG_ANY);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Error seek audio\n");
        goto END;
    }
    audioEndTime = atoi(argv[6]);

    //视频源移动到对应位置
    videoStartTime = atoi(argv[2]);
    ret = av_seek_frame(videoFmtCtx, videoIndex, videoStartTime * AV_TIME_BASE, AVSEEK_FLAG_ANY);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Error seek video\n");
        goto END;
    }
    videoEndTime = atoi(argv[3]);

    while (1)
    {
        ret = av_read_frame(audioFmtCtx, &audioPkt);
        ret = av_read_frame(videoFmtCtx, &videoPkt);

        if (ret < 0)
        {
            break;
        }

        if (audioPkt.stream_index == audioIndex)
        {
            audioTempInStram = audioFmtCtx->streams[audioIndex];
            tempOutStram = outputFmtCtx->streams[audioIndex];
            
            log_packet(audioFmtCtx, &audioPkt, "in");
            if (av_q2d(audioTempInStram->time_base) * audioPkt.pts > audioEndTime)
            {
                av_packet_unref(&audioPkt);
            }
            else
            {
                if (audioDtsStart == 0)
                {
                    audioDtsStart = audioPkt.dts;
                    printf("audioDtsStart = %ld\n", audioDtsStart);
                }
                if (audioPtsStart == 0)
                {
                    audioPtsStart = audioPkt.pts;
                    printf("audioPtsStart = %ld\n", audioPtsStart);
                }
                
                /* copy packet */
                audioPkt.dts = av_rescale_q_rnd(audioPkt.dts - audioDtsStart, audioTempInStram->time_base, tempOutStram->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                audioPkt.pts = av_rescale_q_rnd(audioPkt.pts - audioPtsStart, audioTempInStram->time_base, tempOutStram->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);


                if (audioPkt.dts < 0)
                {
                    audioPkt.dts = 0;
                }
                if (audioPkt.pts < 0)
                {
                    audioPkt.pts = 0;
                }
                audioPkt.duration = (int)av_rescale_q((int64_t)audioPkt.duration, audioTempInStram->time_base, tempOutStram->time_base);
                audioPkt.pos = -1;
                log_packet(outputFmtCtx, &audioPkt, "out");
                printf("\n");

                ret = av_interleaved_write_frame(outputFmtCtx, &audioPkt);
                if (ret < 0)
                {
                    av_log(NULL, AV_LOG_ERROR, "Error muxing audio packet\n");
                }
                av_packet_unref(&audioPkt);
            }
        }
        
        if (videoPkt.stream_index == videoIndex)
        {
            videoTempInStram = videoFmtCtx->streams[videoIndex];
            tempOutStram = outputFmtCtx->streams[videoIndex];
            
            log_packet(videoFmtCtx, &videoPkt, "in");
            if (av_q2d(videoTempInStram->time_base) * videoPkt.pts > videoEndTime)
            {
                av_packet_unref(&videoPkt);
            }
            else
            {
                if (videoDtsStart == 0)
                {
                    videoDtsStart = videoPkt.dts;
                    printf("videoDtsStart = %ld\n", videoDtsStart);
                }
                if (videoPtsStart == 0)
                {
                    videoPtsStart = videoPkt.pts;
                    printf("videoPtsStart = %ld\n", videoPtsStart);
                }
                
                /* copy packet */
                videoPkt.dts = av_rescale_q_rnd(videoPkt.dts - videoDtsStart, videoTempInStram->time_base, tempOutStram->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                videoPkt.pts = av_rescale_q_rnd(videoPkt.pts - videoPtsStart, videoTempInStram->time_base, tempOutStram->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);


                if (videoPkt.dts < 0)
                {
                    videoPkt.dts = 0;
                }
                if (videoPkt.pts < 0)
                {
                    videoPkt.pts = 0;
                }
                videoPkt.duration = (int)av_rescale_q((int64_t)videoPkt.duration, videoTempInStram->time_base, tempOutStram->time_base);
                videoPkt.pos = -1;
                log_packet(outputFmtCtx, &videoPkt, "out");
                printf("\n");

                ret = av_interleaved_write_frame(outputFmtCtx, &videoPkt);
                if (ret < 0)
                {
                    av_log(NULL, AV_LOG_ERROR, "Error muxing video packet\n");
                }
                av_packet_unref(&videoPkt);
            }    

            av_write_trailer(outputFmtCtx);
        }

        
    }
    
    

END:
    if (audioFmtCtx != NULL)
    {
        avformat_close_input(&audioFmtCtx);
    }
    if (videoFmtCtx != NULL)
    {
        avformat_close_input(&videoFmtCtx);
    }
    
    
    if (outputFd != NULL)
    {
        fclose(outputFd);
    }
    

    return ret;
}