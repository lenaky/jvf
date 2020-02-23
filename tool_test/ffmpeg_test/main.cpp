#include <iostream>
#include <string>

#include "util/spdlog_wrap.h"

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );
    LOG_E( "hahaha" );
    return 0;
}