#include <string>
#include <fstream>
#include <streambuf>

#include "util/spdlog_wrap.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

}

int g_vpts = 1;

namespace FFMPEG
{
    enum FFMPEG_Error_Type : unsigned long long
    {
        ErrorNone = 0,
        ErrorAllocContextFailed = 0x10000000,
        ErrorOpenFailed = 0x20000000,
        ErrorFindStreamFailed = 0x30000000,
        ErrorNoVideoStream = 0x40000000,
        ErrorCodecOpenFailed = 0x50000000,
        ErrorCanNotFindCodec = 0x60000000,
        ErrorAllocContext = 0x70000000,
        ErrorParamToContext = 0x80000000,
        ErrorDecodeFailed = 0x90000000,
        ErrorAllocFrame = 0xA0000000,
        ErrorSendPacket = 0xB0000000,
    };

    using FError = FFMPEG::FFMPEG_Error_Type;

    class MuxContext
    {
    public:
        struct OutputStream
        {
            AVStream* st;
            AVCodecContext* enc;

            AVFrame* frame;
            AVFrame* tmp_frame;

            int64_t next_pts;

            struct SwsContext* sws_ctx;
            struct SwrContext* swr_ctx;
        };

    public:
        MuxContext() { av_log_set_level( AV_LOG_DEBUG ); }
        FError OpenStream( AVCodecContext* v_dec_ctx, AVCodecContext* a_dec_ctx )
        {
            avformat_alloc_output_context2( &_fmt_ctx, NULL, "hls", "./playlist_%v.m3u8" );
            if( !_fmt_ctx )
            {
                LOG_E( "Could not create output context" );
                return ErrorAllocContextFailed;
            }

            AVOutputFormat* output_fmt = _fmt_ctx->oformat;

            AVCodec* v1 = nullptr, * a1 = nullptr;

            add_stream( &_outs_v1, _fmt_ctx, &v1, output_fmt->video_codec, v_dec_ctx );
            add_stream( &_outs_a1, _fmt_ctx, &a1, output_fmt->audio_codec, a_dec_ctx );

            //av_opt_set( _fmt_ctx->priv_data, "master_pl_name", "master.m3u8", 0 );
            //av_opt_set( _fmt_ctx->priv_data, "var_stream_map", "v:0,a:0 v:1,a:1", 0 );
            av_opt_set_int( _fmt_ctx->priv_data, "hls_list_size", 0, 0 );
            av_opt_set_int( _fmt_ctx->priv_data, "hls_time", 5, 0 );

            AVDictionary* dict = NULL;
            av_dict_set( &dict, "b", "600K", 0 );
            
            auto ret = avcodec_open2( _outs_v1.enc, v1, &dict );

            ret = avcodec_open2( _outs_a1.enc, a1, NULL );

            ret = avcodec_parameters_from_context( _outs_v1.st->codecpar, _outs_v1.enc );

            ret = avcodec_parameters_from_context( _outs_a1.st->codecpar, _outs_a1.enc );

            _outs_v1.frame = alloc_picture( AV_PIX_FMT_YUV420P, _outs_v1.enc->width, _outs_v1.enc->height );
            _outs_a1.frame = alloc_sample();

            avformat_write_header( _fmt_ctx, NULL );

            av_init_packet( _ppacket );

            return ErrorNone;
        }

        AVFrame* alloc_picture( enum AVPixelFormat pix_fmt, int width, int height )
        {
            AVFrame* picture;
            int ret;

            picture = av_frame_alloc();
            if( !picture )
                return NULL;

            picture->format = pix_fmt;
            picture->width = width;
            picture->height = height;

            ret = av_frame_get_buffer( picture, 0 );
            if( ret < 0 )
            {
                LOG_E("Could not allocate frame data.");
            }

            return picture;
        }

        AVFrame* alloc_sample()
        {
            AVFrame* frame = av_frame_alloc();
            if( !frame )
            {
                fprintf( stderr, "Could not allocate audio frame\n" );
                exit( 1 );
            }

            frame->nb_samples = _outs_a1.enc->frame_size;
            frame->format = _outs_a1.enc->sample_fmt;
            frame->channel_layout = _outs_a1.enc->channel_layout;

            /* allocate the data buffers */
            auto ret = av_frame_get_buffer( frame, 0 );
            if( ret < 0 )
            {
                fprintf( stderr, "Could not allocate audio data buffers\n" );
                exit( 1 );
            }
            return frame;
        }

        int WriteFrame( AVMediaType type, AVFrame* frame )
        {
            if( type == AVMEDIA_TYPE_VIDEO )
            {
                _outs_v1.frame->data[ 0 ] = frame->data[ 0 ];
                _outs_v1.frame->data[ 1 ] = frame->data[ 1 ];
                _outs_v1.frame->data[ 2 ] = frame->data[ 2 ];
                _outs_v1.frame->linesize[ 0 ] = frame->linesize[ 0 ];
                _outs_v1.frame->linesize[ 1 ] = frame->linesize[ 1 ];
                _outs_v1.frame->linesize[ 2 ] = frame->linesize[ 2 ];
                //av_frame_copy( _outs_v1.frame, frame );
                _outs_v1.frame->pts = av_rescale_q( frame->pts,
                                                   { 1, 600 },
                                                    _outs_v1.st->time_base );

                if( 0 > av_frame_make_writable( _outs_v1.frame ) )
                {
                    int i = 0;
                }

                auto val = avcodec_send_frame( _outs_v1.enc, _outs_v1.frame );
                if( 0 == val)
                {
                    int ret = 0;
                    while( 0 <= ret )
                    {
                        ret = avcodec_receive_packet( _outs_v1.enc, _ppacket );
                        if( ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF )
                        {
                            continue;
                        }
                        if( ret < 0 )
                        {
                            break;
                        }

                        //av_packet_rescale_ts( _ppacket, {1, 10000}, _outs_v1.st->time_base );
                        _ppacket->stream_index = _outs_v1.st->index;
                        _ppacket->duration = frame->pkt_duration;

                        if( 0 == av_interleaved_write_frame( _fmt_ctx, _ppacket ) )
                        {
                            int ii = 0;
                        }
                        else
                        {
                            LOG_E( "av_interleaved_write_frame failed" );
                        }
                        av_packet_unref( _ppacket );
                    }
                }
            }
            else
            {
                _outs_a1.frame->data[ 0 ] = frame->data[ 0 ];
                _outs_a1.frame->data[ 1 ] = frame->data[ 1 ];
                _outs_a1.frame->data[ 2 ] = frame->data[ 2 ];
                _outs_a1.frame->linesize[ 0 ] = frame->linesize[ 0 ];
                _outs_a1.frame->linesize[ 1 ] = frame->linesize[ 1 ];
                _outs_a1.frame->linesize[ 2 ] = frame->linesize[ 2 ];
                _outs_a1.frame->pts = av_rescale_q( frame->pts,
                                                    { 1, 44100 },
                                                    _outs_v1.st->time_base );

                auto ret = av_frame_make_writable( _outs_a1.frame );
                ret = avcodec_send_frame( _outs_a1.enc, _outs_a1.frame );
                if( ret < 0 )
                {
                    LOG_E("Error sending the frame to the encoder" );
                    return 0;
                }

                while( ret >= 0 )
                {
                    ret = avcodec_receive_packet( _outs_a1.enc, _ppacket );
                    if( ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF )
                        continue;
                    else if( ret < 0 )
                    {
                        LOG_E( "Error encoding audio frame" );
                        break;
                    }

                    //av_packet_rescale_ts( _ppacket, { 1, 10000 }, _outs_a1.st->time_base );
                    _ppacket->stream_index = _outs_a1.st->index;

                    LOG_E( "==============> A" );
                    if( 0 == av_interleaved_write_frame( _fmt_ctx, _ppacket ) )
                    {
                        int ii = 0;
                    }
                    else
                    {
                        LOG_E( "av_interleaved_write_frame failed" );
                    }
                    av_packet_unref( _ppacket );
                }
            }

            return 0;
        }

        void add_stream( OutputStream* ost, AVFormatContext* oc,
                         AVCodec** codec, enum AVCodecID codec_id, AVCodecContext* src_codec_ctx )
        {
            AVCodecContext* c;
            int i;

            /* find the encoder */
            *codec = avcodec_find_encoder( codec_id );
            if( !( *codec ) )
            {
                LOG_E("Could not find encoder for {}", avcodec_get_name( codec_id ) );
                exit( 1 );
            }

            ost->st = avformat_new_stream( oc, NULL );
            if( !ost->st )
            {
                LOG_E( "Could not allocate stream" );
                exit( 1 );
            }
            ost->st->id = oc->nb_streams - 1;
            c = avcodec_alloc_context3( *codec );
            if( !c )
            {
                LOG_E( "Could not alloc an encoding context" );
                exit( 1 );
            }

            ost->enc = c;

            switch( ( *codec )->type )
            {
            case AVMEDIA_TYPE_AUDIO:
                c->sample_fmt = src_codec_ctx->sample_fmt;
                c->bit_rate = 64000;
                c->sample_rate = src_codec_ctx->sample_rate; // use src
                c->channels = av_get_channel_layout_nb_channels( src_codec_ctx->channel_layout ); // use src
                c->channel_layout = src_codec_ctx->channel_layout; // use src
                c->time_base = { 1, c->sample_rate };
                break;

            case AVMEDIA_TYPE_VIDEO:
                c->codec_id = codec_id;
                //c->bit_rate = 600000; // 600K
                c->width = src_codec_ctx->width; // use src
                c->height = src_codec_ctx->height; // use src
                c->time_base = av_inv_q( src_codec_ctx->framerate );
                c->sample_aspect_ratio = src_codec_ctx->sample_aspect_ratio;
                c->qmax = 51;
                c->qmin = 10;
                c->gop_size = 30;
                c->pix_fmt = AV_PIX_FMT_YUV420P;

                av_opt_set( c->priv_data, "profile", "baseline", 0 );
                break;
            }

            /* Some formats want stream headers to be separate. */
            if( oc->oformat->flags & AVFMT_GLOBALHEADER )
                c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }


    private:
        AVFormatContext* _fmt_ctx = nullptr;

        OutputStream _outs_v1{ 0 };
        OutputStream _outs_a1{ 0 };
        OutputStream _outs_v2{ 0 };
        OutputStream _outs_a2{ 0 };

        AVPacket _packet, * _ppacket = &_packet;

        std::ifstream _ifs;
    };

    template<typename Conf>
    class DecodeContext
    {
    public:
        using decode_callback = std::function<void( AVMediaType, AVFrame* )>;

    public:
        DecodeContext( Conf const& conf ) : _config( conf )
        {
            av_log_set_level( AV_LOG_DEBUG );
        }
        virtual ~DecodeContext()
        {
            if( nullptr != _v_decode_codec_ctx )
                avcodec_free_context( &_v_decode_codec_ctx );
            if( nullptr != _a_decode_codec_ctx )
                avcodec_free_context( &_a_decode_codec_ctx );
            if( nullptr != _fmt_ctx )
                avformat_close_input( &_fmt_ctx );
            if( nullptr != _av_frm )
                av_frame_free( &_av_frm );

            if( _osf ) _osf.close();
        }

        FError OpenStream()
        {
            _fmt_ctx = avformat_alloc_context();
            if( !_fmt_ctx )
            {
                LOG_E( "could not alloc context" );
                return ErrorAllocContextFailed;
            }

            if( 0 > avformat_open_input( &_fmt_ctx, _config.c_str(), NULL, NULL ) )
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

            _av_frm = av_frame_alloc();
            if( !_av_frm )
            {
                LOG_E( "could not allocate frame" );
                return ErrorAllocFrame;
            }

            for( int i = 0; i < _fmt_ctx->nb_streams; i++ )
            {
                AVStream* stream = _fmt_ctx->streams[ i ];
                AVCodec* dec = avcodec_find_decoder( stream->codecpar->codec_id );
                if( !dec )
                {
                    LOG_E( "Failed to find decoder for stream {}", i );
                    return ErrorCanNotFindCodec;
                }
                AVCodecContext* codec_ctx = avcodec_alloc_context3( dec );
                if( !codec_ctx )
                {
                    LOG_E( "Failed to allocate the decoder context for stream {}", i );
                    return ErrorAllocContext;
                }
                auto ret = avcodec_parameters_to_context( codec_ctx, stream->codecpar );
                if( ret < 0 )
                {
                    LOG_E( "Failed to copy decoder parameters to input decoder context. {}", i );
                    return ErrorParamToContext;
                }

                if( codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                    || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO )
                {
                    if( codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO )
                        codec_ctx->framerate = av_guess_frame_rate( _fmt_ctx, stream, NULL );
                    ret = avcodec_open2( codec_ctx, dec, NULL );
                    if( ret < 0 )
                    {
                        LOG_E( "Failed to open decoder for stream {}", i );
                        return ErrorCodecOpenFailed;
                    }
                }
                if( codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO )
                    _v_decode_codec_ctx = codec_ctx;
                else _a_decode_codec_ctx = codec_ctx;
            }

            av_init_packet( _ppacket );

            return ErrorNone;
        }

        FError DecodeStream()
        {
            if( 0 <= av_read_frame( _fmt_ctx, _ppacket ) )
            {
                if( _ppacket->stream_index == _video_stream_index )
                {
                    auto ret = avcodec_send_packet( _v_decode_codec_ctx, _ppacket );
                    if( 0 > ret )
                    {
                        LOG_E( "send packet failed. ret={}", ret );
                        av_packet_unref( _ppacket );
                        return ErrorSendPacket;
                    }
                    LOG_I( "video send packet success" );
                    while( 0 <= ret )
                    {
                        ret = avcodec_receive_frame( _v_decode_codec_ctx, _av_frm );
                        if( ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF )
                        {
                            continue;
                        }
                        else if( ret < 0 )
                        {
                            LOG_E( "decode failed!" );
                            break;
                        }
                        _decode_function( AVMEDIA_TYPE_VIDEO, _av_frm );
                    }
                    LOG_I( "video decode done" );
                    av_packet_unref( _ppacket );
                    return ErrorNone;
                }
                else
                {
                    auto ret = avcodec_send_packet( _a_decode_codec_ctx, _ppacket );
                    if( 0 > ret )
                    {
                        LOG_E( "send packet failed. ret={}", ret );
                        av_packet_unref( _ppacket );
                        return ErrorSendPacket;
                    }
                    LOG_I( "audio send packet success" );
                    while( 0 <= ret )
                    {
                        ret = avcodec_receive_frame( _a_decode_codec_ctx, _av_frm );
                        if( ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF )
                        {
                            continue;
                        }
                        else if( ret < 0 )
                        {
                            LOG_E( "decode failed!" );
                            break;
                        }
                        _decode_function( AVMEDIA_TYPE_AUDIO, _av_frm );
                    }
                    LOG_I( "audio decode done" );
                    av_packet_unref( _ppacket );
                    return ErrorNone;
                }

            }
            else
            {
                return ErrorDecodeFailed;
            }
        }

        void SetDecodeCallback( decode_callback dcb ) { _decode_function = dcb; }

        AVFormatContext* GetFormatContext()
        {
            return _fmt_ctx;
        }

        AVCodecContext* GetVideoCodecContext() { return _v_decode_codec_ctx; }
        AVCodecContext* GetAudioCodecContext() { return _a_decode_codec_ctx; }

    private:
        AVFormatContext* _fmt_ctx = nullptr;
        AVCodecContext* _v_decode_codec_ctx = nullptr;
        AVCodecContext* _a_decode_codec_ctx = nullptr;
        AVFrame* _av_frm = nullptr;
        Conf _config;
        unsigned int _video_stream_index = -1;
        unsigned int _audio_stream_index = -1;
        AVPacket _packet, * _ppacket = &_packet;
        decode_callback _decode_function = nullptr;

        std::ofstream _osf;
    };
}

int main()
{
    FFMPEG::DecodeContext<std::string> dctx(std::string("./source.mp4"));
    dctx.OpenStream();

    FFMPEG::MuxContext mctx;
    mctx.OpenStream( dctx.GetVideoCodecContext(), dctx.GetAudioCodecContext() );
    dctx.SetDecodeCallback( [ &dctx, &mctx ]( AVMediaType type, AVFrame* frame )
    {
        mctx.WriteFrame( type, frame );
    } );
    while(0 == dctx.DecodeStream() ) { }

    system( "pause" );
    return 0;
}