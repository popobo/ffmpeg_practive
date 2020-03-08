#include <libavutil/log.h>
#include <libavformat/avformat.h>

int main(int argc, char *argv[])
{
    int ret = 0;
    AVFormatContext *fmtCtx = NULL;
    
    av_log_set_level(AV_LOG_INFO);
    
    ret = avformat_open_input(&fmtCtx, "./test.mp4", NULL, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Can't open file:%s\n", av_err2str(ret));
        return -1;
    }
    //第四个参数 0是输入流 1是输出流
    av_dump_format(fmtCtx, 0, "./test.mp4", 0);

    avformat_close_input(&fmtCtx);
        

    return 0;
}