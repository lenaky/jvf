#include <iostream>
#include "util/spdlog_wrap.h"
#include "example.pb.h"
#include "pbjson.hpp"

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else
#pragma comment(lib, "libprotobuf.lib")
#endif

int main()
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    example::Config conf;
    conf.set_server_timeout( 100 );
    conf.set_client_max_count( 200 );

    LOG_I( "Message : {}", conf.DebugString() );

    std::string out_str;
    pbjson::pb2json( &conf, out_str );

    LOG_I( "Json Message : {}", out_str );

    return 0;
}