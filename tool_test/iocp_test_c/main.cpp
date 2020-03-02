#include "util/spdlog_wrap.h"
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>


// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"

class SocketClient
{
public:
    SocketClient(std::string const& dest_ip, unsigned int const port) : _dest_ip( dest_ip ), _port( port )
    { }

    ~SocketClient()
    {
        WSACleanup();
    }

    void InitClient()
    {
        WSADATA wsaData;
        auto ret = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
        if( 0 != ret )
        {
            LOG_E( "WSAStartup failed. {}", ret );
            return;
        }
    }

    void ConnectToServer()
    {
        _socket = socket( AF_INET, SOCK_STREAM, 0);
        if( INVALID_SOCKET == _socket )
        {
            LOG_E( "socket create failed. {}", WSAGetLastError() );
            WSACleanup();
            return;
        }

        SOCKADDR_IN server;
        server.sin_port = htons( _port );
        inet_pton( AF_INET, _dest_ip.c_str(), &server.sin_addr );
        auto ret = connect( _socket, ( SOCKADDR* )&server, sizeof( server ) );
        if( SOCKET_ERROR == ret )
        {
            LOG_E( "connect failed." );
            closesocket( _socket );
            _socket = INVALID_SOCKET;
            return;
        }

        LOG_I( "connect to server success" );
    }

    void StartToSendThread()
    {
        _send_thread = std::thread( [ this ](){
            Worker();
        } );
    }

    void StopClient()
    {
        _send_thread_run = false;
        if( _send_thread.joinable() )
        {
            _send_thread.join();
        }
    }

    void Worker()
    {
        std::string send_msg( "this is dummy message" );

        while( _send_thread_run )
        {
            auto ret = send( _socket, send_msg.c_str(), send_msg.size(), 0 );
            if( SOCKET_ERROR == ret )
            {
                LOG_E( "Send failed." );
            }
            else
            {
                LOG_I( "Send Success." );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        }
    }

private:
    std::string _dest_ip;
    unsigned int _port;
    SOCKET _socket = INVALID_SOCKET;

    bool _send_thread_run = true;
    std::thread _send_thread;
};

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    SocketClient sc( std::string( "192.168.0.17" ), 10010 );
    sc.InitClient();
    sc.ConnectToServer();
    sc.StartToSendThread();

    int i = 0;
    std::cin >> i;

    sc.StopClient();
    return 0;
}