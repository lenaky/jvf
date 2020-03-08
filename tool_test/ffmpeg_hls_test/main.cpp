#include <string>
#include <fstream>
#include <streambuf>

#include "util/spdlog_wrap.h"
#include "video_config.pb.h"

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else
#pragma comment(lib, "libprotobuf.lib")
#endif

#include "pbjson.hpp"

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

namespace FFMPEG
{
    enum FFMPEG_Error_Type : unsigned long long
    {
        ErrorNone = 0,
        ErrorAllocContextFailed = 0x10000000,
        ErrorOpenFailed         = 0x20000000,
        ErrorFindStreamFailed   = 0x30000000,
        ErrorNoVideoStream      = 0x40000000,
    };

    using FError = FFMPEG::FFMPEG_Error_Type;

    class FormatContext
    {
    public:
        FormatContext( jvf::video_config const& conf ) : _config( conf ) {}

        FError OpenStream()
        {
            LOG_D( "load : {}", _config.DebugString() );

            _fmt_ctx = avformat_alloc_context();
            if( !_fmt_ctx )
            {
                LOG_E( "could not alloc context" );
                return ErrorAllocContextFailed;
            }


            //open RTSP
            if( 0 > avformat_open_input( &_fmt_ctx, _config.mutable_src()->src_name().c_str(), NULL, NULL ) )
            {
                LOG_E( "open input failed" );
                return ErrorOpenFailed;
            }

            if( avformat_find_stream_info( _fmt_ctx, NULL ) < 0 )
            {
                LOG_E( "could not find codec parameters" );
                return ErrorFindStreamFailed;
            }

            //search video stream
            for( unsigned int i = 0; i < _fmt_ctx->nb_streams; i++ )
            {
                if( _fmt_ctx->streams[ i ]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO )
                    _video_stream_index = i;
                if( _fmt_ctx->streams[ i ]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO )
                    _audio_stream_index = i;
            }

            if( -1 == _video_stream_index )
            {
                LOG_E( "video_stream_index not found" );
                return ErrorNoVideoStream;
            }

            return ErrorNone;
        }


    private:
        AVFormatContext* _fmt_ctx = nullptr;
        jvf::video_config _config;
        unsigned int _video_stream_index = -1;
        unsigned int _audio_stream_index = -1;
    };
};

void json_loader( std::string const& filename, std::string& json_conf )
{
    std::ifstream t( filename );
    if( t )
    {
        t.seekg( 0, std::ios::end );
        json_conf.reserve( t.tellg() );
        t.seekg( 0, std::ios::beg );

        json_conf.assign( ( std::istreambuf_iterator<char>( t ) ),
                          std::istreambuf_iterator<char>() );
        t.close();
    }
}

int main( void )
{
    using namespace std::literals;

    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    jvf::video_config conf;
    std::string json_conf;
    json_loader( "./config.json"s, json_conf );

    std::string error;
    if( -1 == pbjson::json2pb( json_conf, &conf, error ) )
    {
        LOG_E( "load failed. {}", error );
        return 0;
    }

    LOG_I( "load success. {}", conf.DebugString() );
    
    FFMPEG::FormatContext ctx( conf );
    auto i = ctx.OpenStream();
    LOG_D( "Open Stream. {}", i );



    return 0;
}