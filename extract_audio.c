#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <stdio.h>

#define ADTX_HEADER_LEN 7

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

int extract_audio(char *src, char *dst)
{
    int ret = 0;
    int len  = 0;
    int audioIndex = 0;
    FILE *fd = NULL;
    AVFormatContext *fmtCtx = NULL;
    AVPacket pkt;
    unsigned char adtsHeaderBuf[ADTX_HEADER_LEN] = {0};

    av_log_set_level(AV_LOG_INFO);
    
    //read two params from console

    if (!src || !dst)
    {
        av_log(NULL, AV_LOG_ERROR, "Src or dst is NULL\n");
    }
    

    ret = avformat_open_input(&fmtCtx, src, NULL, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", av_err2str(ret));
        return -1;
    }

    fd = fopen(dst, "wb");
    if (!fd)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", dst);
        avformat_close_input(&fmtCtx);
        return -1;
    }
    
    //第四个参数 0是输入流 1是输出流
    av_dump_format(fmtCtx, 0, src, 0);

    //2.get stream
    ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't find best stream:%s\n", av_err2str(ret));
        avformat_close_input(&fmtCtx);
        return -1;
    }
    audioIndex = ret;

    av_init_packet(&pkt);
    while(av_read_frame(fmtCtx, &pkt) >= 0)
    {
        //3.write audio data to aac file
        if (pkt.stream_index == audioIndex)
        {
            adts_header(adtsHeaderBuf, pkt.size);
            fwrite(adtsHeaderBuf, 1, ADTX_HEADER_LEN, fd);
            len = fwrite(pkt.data, 1, pkt.size, fd);
            if (len != pkt.size)
            {
                av_log(NULL, AV_LOG_WARNING, "Warning, length is equal to the size of packet!\n");
            }
        }
        av_packet_unref(&pkt);
    }

    for (int i = 0; i < ADTX_HEADER_LEN; i++)
    {
        printf("%d\n", adtsHeaderBuf[i]);
    }
    
    avformat_close_input(&fmtCtx);
    if (fd)
    {
        fclose(fd);
    }
       

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    if (argc < 3)
    {
        av_log(NULL, AV_LOG_ERROR, "The count of params should be more three!\n");
        ret = -1;
        return ret;
    }
    ret = extract_audio(argv[1], argv[2]);
    return ret;
}