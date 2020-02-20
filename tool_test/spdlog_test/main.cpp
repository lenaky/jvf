#include <iostream>
#include <string>

#include "util/spdlog_warp.h"

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );
    
    for( int i = 0; i < 1000000; i++ )
    {
        LOG_T( "This is trace log {}", 100 );
        LOG_I( "This is info log {}", 200 );
        LOG_D( "This is debug log {}", 300 );
        LOG_E( "This is error log {}", 400 );
        LOG_W( "This is warn log {}", 500 );
        LOG_C( "This is critical log {}", 600 );
    }

    return 0;
}