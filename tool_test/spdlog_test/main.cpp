#include <iostream>
#include <string>

#include "util/spdlog_warp.h"

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );
    LOGGER().GetLogger()->debug( "hahahaha" );
    LOGGER().GetLogger()->info( "hahahaha" );
    LOGGER().GetLogger()->warn( "hahahaha" );
    LOGGER().GetLogger()->error( "hahahaha" );

    return 0;
}