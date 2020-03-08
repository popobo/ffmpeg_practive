#include <stdio.h>
#include <libavutil/log.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

#define ERROR_STR_SIZE 1024

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


#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif

//增加start code
static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size)
{
    uint32_t offset         = out->size;
    uint8_t nal_header_size = offset ? 3 : 4;
    int err;

    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
    if (err < 0)
        return err;

    if (sps_pps)
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset) {
        AV_WB32(out->data + sps_pps_size, 1);
    } else {
        (out->data + offset + sps_pps_size)[0] =
        (out->data + offset + sps_pps_size)[1] = 0;
        (out->data + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}

int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding)
{
    uint16_t unit_size;
    uint64_t total_size                 = 0;
    uint8_t *out                        = NULL, unit_nb, sps_done = 0,
             sps_seen                   = 0, pps_seen = 0, sps_offset = 0, pps_offset = 0;
    const uint8_t *extradata            = codec_extradata + 4;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size, 用于指示表示编码数据长度所需字节数

    sps_offset = pps_offset = -1;

    /* retrieve sps and pps unit(s) */
    unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
    if (!unit_nb) {
        goto pps;
    }else {
        sps_offset = 0;
        sps_seen = 1;
    }

    while (unit_nb--) {
        int err;

        unit_size   = AV_RB16(extradata);
        total_size += unit_size + 4;
        if (total_size > INT_MAX - padding) {
            av_log(NULL, AV_LOG_ERROR,
                   "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size) {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                   "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;
pps:
        if (!unit_nb && !sps_done++) {
            unit_nb = *extradata++; /* number of pps unit(s) */
            if (unit_nb) {
                pps_offset = total_size;
                pps_seen = 1;
            }
        }
    }

    if (out)
        memset(out + total_size, 0, padding);

    if (!sps_seen)
        av_log(NULL, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(NULL, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    out_extradata->data      = out;
    out_extradata->size      = total_size;

    return length_size;
}

int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd)
{

    AVPacket *out = NULL;
    AVPacket spspps_pkt;

    int len;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size    = 0;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int            buf_size;
    int ret = 0, i;

    out = av_packet_alloc();

    buf      = in->data;
    buf_size = in->size;
    buf_end  = in->data + in->size;

    do {
        ret= AVERROR(EINVAL);
        if (buf + 4 /*s->length_size*/ > buf_end)
            goto fail;

        for (nal_size = 0, i = 0; i<4/*s->length_size*/; i++)
            nal_size = (nal_size << 8) | buf[i];

        buf += 4; /*s->length_size;*/
        unit_type = *buf & 0x1f;

        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;

        /*
        if (unit_type == 7)
            s->idr_sps_seen = s->new_idr = 1;
        else if (unit_type == 8) {
            s->idr_pps_seen = s->new_idr = 1;
            */
            /* if SPS has not been seen yet, prepend the AVCC one to PPS */
            /*
            if (!s->idr_sps_seen) {
                if (s->sps_offset == -1)
                    av_log(ctx, AV_LOG_WARNING, "SPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                else {
                    if ((ret = alloc_and_copy(out,
                                         ctx->par_out->extradata + s->sps_offset,
                                         s->pps_offset != -1 ? s->pps_offset : ctx->par_out->extradata_size - s->sps_offset,
                                         buf, nal_size)) < 0)
                        goto fail;
                    s->idr_sps_seen = 1;
                    goto next_nal;
                }
            }
        }
        */

        /* if this is a new IDR picture following an IDR picture, reset the idr flag.
         * Just check first_mb_in_slice to be 0 as this is the simplest solution.
         * This could be checking idr_pic_id instead, but would complexify the parsing. */
        /*
        if (!s->new_idr && unit_type == 5 && (buf[1] & 0x80))
            s->new_idr = 1;

        */
        /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
        if (/*s->new_idr && */unit_type == 5 /*&& !s->idr_sps_seen && !s->idr_pps_seen*/) {

            // h264_extradata_to_annexb( fmt_ctx->streams[in->stream_index]->codec->extradata,
            //                           fmt_ctx->streams[in->stream_index]->codec->extradata_size,
            //                           &spspps_pkt,
            //                           AV_INPUT_BUFFER_PADDING_SIZE);
            h264_extradata_to_annexb( fmt_ctx->streams[in->stream_index]->codecpar->extradata,
                                      fmt_ctx->streams[in->stream_index]->codecpar->extradata_size,
                                      &spspps_pkt,
                                      AV_INPUT_BUFFER_PADDING_SIZE);

            if ((ret=alloc_and_copy(out,
                               spspps_pkt.data, spspps_pkt.size,
                               buf, nal_size)) < 0)
                goto fail;
            /*s->new_idr = 0;*/
        /* if only SPS has been seen, also insert PPS */
        }
        /*else if (s->new_idr && unit_type == 5 && s->idr_sps_seen && !s->idr_pps_seen) {
            if (s->pps_offset == -1) {
                av_log(ctx, AV_LOG_WARNING, "PPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                    goto fail;
            } else if ((ret = alloc_and_copy(out,
                                        ctx->par_out->extradata + s->pps_offset, ctx->par_out->extradata_size - s->pps_offset,
                                        buf, nal_size)) < 0)
                goto fail;
        }*/ else {
            if ((ret=alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                goto fail;
            /*
            if (!s->new_idr && unit_type == 1) {
                s->new_idr = 1;
                s->idr_sps_seen = 0;
                s->idr_pps_seen = 0;
            }
            */
        }


        len = fwrite( out->data, 1, out->size, dst_fd);
        if(len != out->size){
            av_log(NULL, AV_LOG_DEBUG, "warning, length of writed data isn't equal pkt.size(%d, %d)\n",
                    len,
                    out->size);
        }
        fflush(dst_fd);

next_nal:
        buf        += nal_size;
        cumul_size += nal_size + 4;//s->length_size;
    } while (cumul_size < buf_size);

    /*
    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    */
fail:
    av_packet_free(&out);

    return ret;
}

int extract_video(char *src_filename, char *dst_filename)
{
    int err_code;
    char errors[1024];

    FILE *dst_fd = NULL;

    int video_stream_index = -1;

    //AVFormatContext *ofmt_ctx = NULL;
    //AVOutputFormat *output_fmt = NULL;
    //AVStream *out_stream = NULL;

    AVFormatContext *fmt_ctx = NULL;
    AVPacket pkt;

    //AVFrame *frame = NULL;

    av_log_set_level(AV_LOG_DEBUG);

    if(src_filename == NULL || dst_filename == NULL){
        av_log(NULL, AV_LOG_ERROR, "src or dts file is null, plz check them!\n");
        return -1;
    }

    dst_fd = fopen(dst_filename, "wb");
    if (!dst_fd) {
        av_log(NULL, AV_LOG_DEBUG, "Could not open destination file %s\n", dst_filename);
        return -1;
    }

    /*open input media file, and allocate format context*/
    if((err_code = avformat_open_input(&fmt_ctx, src_filename, NULL, NULL)) < 0){
        av_strerror(err_code, errors, 1024);
        av_log(NULL, AV_LOG_DEBUG, "Could not open source file: %s, %d(%s)\n",
               src_filename,
               err_code,
               errors);
        return -1;
    }

    /*dump input information*/
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    /*initialize packet*/
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /*find best video stream*/
    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(video_stream_index < 0){
        av_log(NULL, AV_LOG_DEBUG, "Could not find %s stream in input file %s\n",
               av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
               src_filename);
        return AVERROR(EINVAL);
    }

    /*
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_DEBUG, "Error occurred when opening output file");
        exit(1);
    }
    */

    /*read frames from media file*/
    while(av_read_frame(fmt_ctx, &pkt) >=0 ){
        if(pkt.stream_index == video_stream_index){
            /*
            pkt.stream_index = 0;
            av_write_frame(ofmt_ctx, &pkt);
            av_free_packet(&pkt);
            */

            h264_mp4toannexb(fmt_ctx, &pkt, dst_fd);

        }

        //release pkt->data
        av_packet_unref(&pkt);
    }

    //av_write_trailer(ofmt_ctx);

    /*close input media file*/
    avformat_close_input(&fmt_ctx);
    if(dst_fd) {
        fclose(dst_fd);
    }

    //avio_close(ofmt_ctx->pb);

    return 0;
}

//merge video stream and audio stream 
int video_audio_merge(char *video_filename, char *audio_filename, char *output_filename)
{
    int ret = -1;

    int error_code = 0;
    char error_string[ERROR_STR_SIZE] = {0};

    AVFormatContext *video_format_context = NULL;
    AVFormatContext *audio_format_context = NULL;

    AVFormatContext *output_format_context = NULL;
    AVOutputFormat *output_format = NULL;

    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;

    AVStream *output_stream_1 = NULL;
    AVStream *output_stream_2 = NULL;

    int64_t current_pts_1 = 0, current_pts_2 = 0;

    int bool_use_video_ts = 1;
    uint32_t packets = 0;
    AVPacket pkt;

    int stream1 = 0, stream2 = 0;

    av_log_set_level(AV_LOG_DEBUG);

    // register avformat, codec
    // av_register_all();

    //open first file
    if((error_code = avformat_open_input(&video_format_context, video_filename, 0, 0)) < 0 ){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
               "Could not open src file, %s, %d(%s)\n",
               video_filename, error_code, error_string);
        goto __FAIL;
    }

    if((error_code = avformat_find_stream_info(video_format_context, 0)) <0){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
               "Failed to retrieve input stream info, %s, %d(%s) \n",
               video_filename, error_code, error_string);
        goto __FAIL;
    }

    av_dump_format(video_format_context, 0, video_filename, 0);

    //open second file
    if((error_code = avformat_open_input(&audio_format_context, audio_filename, 0, 0)) < 0 ){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
                "Could not open the second src file, %s, %d(%s)\n",
                audio_filename, error_code, error_string);
        goto __FAIL;
    }

    if((error_code = avformat_find_stream_info(audio_format_context, 0)) <0){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
                "Failed to retrieve input stream info, %s, %d(%s) \n",
                audio_filename, error_code, error_string);
        goto __FAIL;
    }

    av_dump_format(audio_format_context, 0, audio_filename, 0);

    //create out context
    if((error_code = avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_filename)) < 0 ){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
                "Failed to create an context of outfile , %d(%s) \n",
                error_code, error_string);
    }

    output_format = output_format_context->oformat;

    //create out stream according to input stream
    if(video_format_context->nb_streams == 1){
        video_stream = video_format_context->streams[0];
        stream1 = 1;

        AVCodecParameters *in_codecpar = video_stream->codecpar;

        if(in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
           in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
           in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE){
            av_log(NULL, AV_LOG_ERROR, "The Codec type is invalid!\n");
            goto __FAIL;
        }

        output_stream_1 = avformat_new_stream(output_format_context, NULL);
        if(!output_stream_1){
            av_log(NULL, AV_LOG_ERROR, "Failed to alloc out stream!\n");
            goto __FAIL;
        }

        if((error_code = avcodec_parameters_copy(output_stream_1->codecpar, in_codecpar)) < 0 ){
            av_strerror(error_code, error_string, ERROR_STR_SIZE);
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy codec parameter, %d(%s)\n",
                   error_code, error_string);
        }

        output_stream_1->codecpar->codec_tag = 0;

        /*
        if (output_format->flags & AVFMT_GLOBALHEADER)
            output_stream_1->codecpar->flags |= CODEC_FLAG_GLOBAL_HEADER;
            */
    }

    if(audio_format_context->nb_streams == 1){
        audio_stream = audio_format_context->streams[0];
        stream2 = 1;

        AVCodecParameters *in_codecpar = audio_stream->codecpar;

        if(in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
           in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
           in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE){
            av_log(NULL, AV_LOG_ERROR, "The Codec type is invalid!\n");
            goto __FAIL;
        }

        output_stream_2 = avformat_new_stream(output_format_context, NULL);
        if(!output_stream_2){
            av_log(NULL, AV_LOG_ERROR, "Failed to alloc out stream!\n");
            goto __FAIL;
        }

        if((error_code = avcodec_parameters_copy(output_stream_2->codecpar, in_codecpar)) < 0 ){
            av_strerror(error_code, error_string, ERROR_STR_SIZE);
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy codec parameter, %d(%s)\n",
                   error_code, error_string);
            goto __FAIL;
        }

        output_stream_2->codecpar->codec_tag = 0;
        /*
        if (output_format->flags & AVFMT_GLOBALHEADER)
            output_stream_2->codecpar->flags |= CODEC_FLAG_GLOBAL_HEADER;
            */
    }

    av_dump_format(output_format_context, 0, output_filename, 1);

    //open out file
    if(!(output_format->flags & AVFMT_NOFILE)){
        if((error_code = avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE))<0){
            av_strerror(error_code, error_string, ERROR_STR_SIZE);
            av_log(NULL, AV_LOG_ERROR,
                   "Could not open output file, %s, %d(%s)\n",
                   output_filename, error_code, error_string);
            goto __FAIL;
        }
    }

    //write media header
    if((error_code = avformat_write_header(output_format_context, NULL)) < 0){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
               "Error occurred when writing media header!\n");
        goto __FAIL;
    }

    av_init_packet(&pkt);

    while ( stream1 || stream2 ) {
        /* select the stream to encode */
        if (stream1 &&
            ( !stream2 || av_compare_ts(current_pts_1, video_stream->time_base,
                                            current_pts_2, audio_stream->time_base) <= 0)) {
            ret = av_read_frame(video_format_context, &pkt);
            if(ret < 0 ){
                stream1 = 0;
                continue;
            }

            //pkt.pts = packets++;
            //video_stream->time_base = (AVRational){video_stream->r_frame_rate.den, video_stream->r_frame_rate.num};

            //是否使用视频的时间基
            if(!bool_use_video_ts &&
                    (video_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)){
                pkt.pts = ++packets;
                video_stream->time_base = (AVRational){video_stream->r_frame_rate.den, video_stream->r_frame_rate.num};
                //pkt.pts = av_rescale_q(pkt.pts, fps, output_stream_1->time_base);
                //pkt.dts = av_rescale_q(pkt.dts, fps, output_stream_1->time_base);

                pkt.pts = av_rescale_q_rnd(pkt.pts, video_stream->time_base, output_stream_1->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
                //pkt.dts = av_rescale_q_rnd(pkt.dts, video_stream->time_base, output_stream_1->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
                //pkt.duration = av_rescale_q(pkt.duration, fps, output_stream_1->time_base);
                pkt.dts = pkt.pts;
                av_log(NULL, AV_LOG_DEBUG, "xxxxxxxxx%d, dts=%ld, pts=%ld\n", packets, pkt.dts, pkt.pts);
            }

            //FIX：No PTS (Example: Raw H.264)
            //Simple Write PTS
            if(pkt.pts==AV_NOPTS_VALUE){
                //Write PTS
                AVRational time_base1 = video_stream->time_base;
                //Duration between 2 frames (us)
                av_log(NULL, AV_LOG_DEBUG, "AV_TIME_BASE=%d,av_q2d=%f(num=%d, den=%d)\n",
                                        AV_TIME_BASE,
                                        av_q2d(video_stream->r_frame_rate),
                                        video_stream->r_frame_rate.num,
                                        video_stream->r_frame_rate.den);

                int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(video_stream->r_frame_rate);
                //Parameters
                pkt.pts=(double)(packets*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                pkt.dts=pkt.pts;
                current_pts_1 = pkt.pts;
                pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                packets++;
            }

            //Convert PTS/DTS
            pkt.pts = av_rescale_q_rnd(pkt.pts, video_stream->time_base, output_stream_1->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts = pkt.pts;
            //pkt.pts = av_rescale_q_rnd(pkt.pts, video_stream->time_base, output_stream_1->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            //pkt.dts = av_rescale_q_rnd(pkt.dts, video_stream->time_base, output_stream_1->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));

            pkt.duration = av_rescale_q(pkt.duration, video_stream->time_base, output_stream_1->time_base);
            pkt.pos = -1;
            pkt.stream_index=0;
            av_log(NULL, AV_LOG_DEBUG, "xxxxxxxxx%d, dts=%ld, pts=%ld\n", packets, pkt.dts, pkt.pts);

            stream1 = !av_interleaved_write_frame(output_format_context, &pkt);
            //stream1 = !av_write_frame(output_format_context, &pkt);
        } else if(stream2){
            ret = av_read_frame(audio_format_context, &pkt);
            if(ret < 0 ){
                stream2 = 0;
                continue;
            }

            if(!bool_use_video_ts &&
                    (audio_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)){
                pkt.pts = packets++;
                pkt.dts = pkt.pts;
            }


            current_pts_2 = pkt.pts;
            //Convert PTS/DTS
            pkt.pts = av_rescale_q_rnd(pkt.pts, audio_stream->time_base, output_stream_2->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts= pkt.pts;
            //pkt.dts = av_rescale_q_rnd(pkt.dts, audio_stream->time_base, output_stream_2->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));

            pkt.duration = av_rescale_q(pkt.duration, audio_stream->time_base, output_stream_2->time_base);
            pkt.pos = -1;
            pkt.stream_index=1;

            av_log(NULL, AV_LOG_DEBUG, "Write stream2 Packet. size:%5d\tpts:%ld\tdts:%ld\n", pkt.size, pkt.pts, pkt.dts);


            stream2 = !av_interleaved_write_frame(output_format_context, &pkt);
        }

        av_packet_unref(&pkt);
    }

    //write media tailer
    if((error_code = av_write_trailer(output_format_context)) < 0){
        av_strerror(error_code, error_string, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR,
               "Error occurred when writing media tailer!\n");
        goto __FAIL;
    }

    ret = 0;

__FAIL:

    if(video_format_context){
        avformat_close_input(&video_format_context);
    }

    if(audio_format_context){
        avformat_close_input(&audio_format_context);
    }

    if(output_format_context){
        if(!(output_format->flags & AVFMT_NOFILE)){
            avio_closep(&output_format_context->pb);
        }
        avformat_free_context(output_format_context);
    }


    return ret;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    if(argc < 4){
        av_log(NULL, AV_LOG_ERROR, "Usage: \n " \
                            "Command video_filename audio_filename output_filename \n");
        ret = -1;
        return ret;
    }
    
    ret = video_audio_merge(argv[1], argv[2], argv[3]);
    return ret;
}
