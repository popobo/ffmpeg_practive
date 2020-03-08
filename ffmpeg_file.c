#include <libavformat/avformat.h>


int main()
{
    int ret = 0;

    ret = avpriv_io_move("./test.txt", "./test1.txt");
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Fail to rename file.");
        return -1;
    }
    av_log(NULL, AV_LOG_INFO, "Success");

    //url串
    // ret = avpriv_io_delete("./test.txt");
    // if (ret < 0)
    // {
    //     av_log(NULL, AV_LOG_ERROR, "Fail to delete file.");
    //     return -1;
    // }
    // av_log(NULL, AV_LOG_INFO, "Success");
    return 0;
}