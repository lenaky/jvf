#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

#include "util/spdlog_wrap.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

}

void TestRTSPClient()
{
    // Register everything
    avformat_network_init();

    AVFormatContext* fmt_ctx = NULL;
    AVPacket pkt1, * pkt = &pkt1;
    unsigned int video_stream_index = -1;

    fmt_ctx = avformat_alloc_context();
    if( !fmt_ctx )
    {
        LOG_E( "could not alloc context" );
        return;
    }

    //open RTSP
    if( 0 > avformat_open_input( &fmt_ctx, "rtsp://192.168.0.16:5554/camera", NULL, NULL ) )
    {
        LOG_E( "open input failed" );
        return;
    }

    if( avformat_find_stream_info( fmt_ctx, NULL ) < 0 )
    {
        LOG_E( "could not find codec parameters" );
        return;
    }

    //search video stream
    for( unsigned int i = 0; i < fmt_ctx->nb_streams; i++ )
    {
        if ( fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }

    if( -1 == video_stream_index )
    {
        LOG_E( "video_stream_index not found" );
        return;
    }

    //start reading packets from stream and write them to file
    av_read_play( fmt_ctx );    //play RTSP

    /* preparing decode recved packet */
    const AVCodec* codec = avcodec_find_decoder( AV_CODEC_ID_H264 );
    if( !codec )
    {
        LOG_E( "codec not found" );
        return;
    }

    AVCodecContext* v_dec_ctx = avcodec_alloc_context3( codec );
    if( !v_dec_ctx )
    {
        LOG_E( "could not allocate video codec context" );
        return;
    }

    AVFrame* av_frm = av_frame_alloc();
    if( !av_frm )
    {
        LOG_E( "could not allocate video frame" );
        return;
    }

    if( 0 > avcodec_open2( v_dec_ctx, codec, NULL ) )
    {
        LOG_E( "could not open codec" );
        return;
    }

    unsigned int limit_packet = 3000; // 30 pictures

    av_init_packet( pkt );
    while( av_read_frame( fmt_ctx, pkt ) >= 0 && --limit_packet > 0)
    { 
        if( pkt->stream_index == video_stream_index )
        {    //packet is video
            LOG_I( "Received Packet!" );
            auto ret = avcodec_send_packet( v_dec_ctx, pkt );
            if( 0 > ret )
            {
                LOG_E( "send packet failed. ret={}", ret );
                continue;
            }
            LOG_I( "send packet success" );
            while( 0 <= ret )
            {
                ret = avcodec_receive_frame( v_dec_ctx, av_frm );
                if( ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF )
                {
                    continue;
                }
                else if( ret < 0 )
                {
                    LOG_E( "decode failed!" );
                    return;
                }

                if( v_dec_ctx->width > 0 && v_dec_ctx->height > 0 )
                {
                    auto pix_fmt = AV_PIX_FMT_RGB24;
                    static struct SwsContext* sws_ctx = sws_getContext( v_dec_ctx->width, 
                                                                        v_dec_ctx->height, 
                                                                        v_dec_ctx->pix_fmt,
                                                                        v_dec_ctx->width,
                                                                        v_dec_ctx->height, 
                                                                        pix_fmt,
                                                                        SWS_BILINEAR, NULL, NULL, NULL );
                    if( sws_ctx != NULL )
                    {
                        auto pic_size = av_image_get_buffer_size( pix_fmt,
                                                                  v_dec_ctx->width,
                                                                  v_dec_ctx->height,
                                                                  16 );
                        /* original data copy from av_frame.
                        auto pic_size = av_image_get_buffer_size( v_dec_ctx->pix_fmt,
                                                                  v_dec_ctx->width,
                                                                  v_dec_ctx->height,
                                                                  16 );
                        uint8_t* buffer = ( uint8_t* )av_malloc( pic_size );
                        auto number_of_written_bytes = av_image_copy_to_buffer( buffer, pic_size,
                                                                                av_frm->data, av_frm->linesize,
                                                                                v_dec_ctx->pix_fmt, v_dec_ctx->width, v_dec_ctx->height, 1 );
                        std::ofstream ofs( "./test.yuv", std::ios::binary );
                        if( ofs )
                        {
                            ofs.write( ( const char* )buffer, number_of_written_bytes );
                            ofs.close();
                        }
                        av_free( buffer );
                        */

                        // convert img from yuv420p which is default decoded image to rgb24.
                        uint8_t *dst_data[ 4 ];
                        int dst_linesize[ 4 ];
                        int alloc_size = 0;
                        if( ( alloc_size = av_image_alloc( dst_data, dst_linesize,
                            v_dec_ctx->width, v_dec_ctx->height, pix_fmt, 1 ) ) < 0 )
                        {
                            LOG_E( "could not alloc image buffer" );
                            exit( 0 );
                        }

                        sws_scale( sws_ctx,
                            ( const uint8_t* const* )av_frm->data,
                                   av_frm->linesize,
                                   0,
                                   v_dec_ctx->height,
                                   dst_data,
                                   dst_linesize );

                        std::ofstream ofs( "./test.raw", std::ios::binary );
                        if( ofs )
                        {
                            ofs.write( (const char*)dst_data[ 0 ], alloc_size );
                            ofs.close();
                        }

                        av_freep( &dst_data[0] );
                        sws_freeContext( sws_ctx );
                    }
                }

                LOG_I( "decode success!" );
            }
        }
        av_packet_unref( pkt );
        av_init_packet( pkt );
    }

    if( fmt_ctx )
        avformat_close_input( &fmt_ctx );
    avcodec_free_context( &v_dec_ctx );
    av_frame_free( &av_frm );
    av_read_pause( fmt_ctx );
    av_packet_free( &pkt );
}

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    LOG_I( "start ffmpeg test.." );
    TestRTSPClient();

    return 0;
}