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

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

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
    AVFrame* av_frm_rgb = av_frame_alloc();
    if( !av_frm_rgb )
    {
        LOG_E( "could not allocate video frame" );
        return;
    }
    if( 0 > avcodec_open2( v_dec_ctx, codec, NULL ) )
    {
        LOG_E( "could not open codec" );
        return;
    }

    unsigned int limit_packet = 30; // 30 pictures

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
                break;
            }

            while( 0 <= ret )
            {
                ret = avcodec_receive_frame( v_dec_ctx, av_frm );
                if( ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF )
                {
                    LOG_E( "error code={}", ret );
                    return;
                }
                else if( ret < 0 )
                {
                    LOG_E( "decode failed!" );
                    return;
                }

                LOG_I( "decode success!" );
            }
        }
        av_packet_unref( pkt );
        av_init_packet( pkt );
    }

    av_read_pause( fmt_ctx );
}

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    LOG_I( "start ffmpeg test.." );
    TestRTSPClient();

    return 0;
}