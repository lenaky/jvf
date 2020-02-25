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
}

void TestRTSPClient()
{
    // Register everything
    avformat_network_init();

    AVFormatContext* fmt_ctx = NULL;
    AVPacket pkt1, * pkt = &pkt1;
    AVCodecContext* codec_ctx = NULL;
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

    // Get the codec
    AVCodec* codec = NULL;
    codec = avcodec_find_decoder( AV_CODEC_ID_H264 );
    if( !codec )
    {
        exit( 1 );
    }

    unsigned int limit_packet = 30; // 30 pictures

    av_init_packet( pkt );
    while( av_read_frame( fmt_ctx, pkt ) >= 0 && --limit_packet > 0)
    { 
        if( pkt->stream_index == video_stream_index )
        {    //packet is video
            LOG_I( "Received Packet!" );
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