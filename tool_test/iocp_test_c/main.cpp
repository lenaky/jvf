#include "util/spdlog_wrap.h"
#include <string>

#include "simplechat_client.pb.h"
#include "simplechat_server.pb.h"

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else
#pragma comment(lib, "libprotobuf.lib")
#endif

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
        inet_pton( AF_INET, _dest_ip.c_str(), &server.sin_addr );
        server.sin_family = AF_INET;
        server.sin_port = htons( _port );
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

    SOCKET GetSocket() const { return _socket; }

private:
    std::string _dest_ip;
    unsigned int _port;
    SOCKET _socket = INVALID_SOCKET;

    bool _send_thread_run = true;
    std::thread _send_thread;
};

void ParseString( std::string const& org, std::string const& delim, std::vector<std::string>& parsed )
{
    std::string s = org;
    size_t pos = 0;
    std::string token;
    while( ( pos = s.find( delim ) ) != std::string::npos )
    {
        token = s.substr( 0, pos );
        s.erase( 0, pos + delim.length() );
        parsed.push_back( token );
    }

    if( 0 < s.size() )
    {
        parsed.push_back( s );
    }
}

char* g_send_buffer = nullptr;
size_t g_send_buffer_size = 0;

void CheckAlloc( int size )
{
    if( nullptr == g_send_buffer )
    {
        g_send_buffer_size = size;
        g_send_buffer = new char[ size ];
    }
    else if( size > g_send_buffer_size )
    {
        delete[]g_send_buffer;
        g_send_buffer_size = size;
        g_send_buffer = new char[ size ];
    }
}

void SendCommand( SocketClient& sc, std::vector<std::string> const& cmds )
{
    if( cmds.size() <= 1 )
    {
        LOG_E( "wierd. size={}", cmds.size() );
        return;
    }

    if( 0 == cmds[ 0 ].compare( "login" ) )
    {
        chat::chat_login_req req;
        req.set_id( "jayjay" );
        req.set_pw( "hello" );

        // packet size / msg id / msg body

        int packet_number = htonl( req.GetDescriptor()->index() );        
        unsigned int packet_size = sizeof( int )/*packet size*/ + sizeof( int )/*packet number*/ + req.ByteSizeLong();
        CheckAlloc( packet_size );

        unsigned int payload_size = sizeof( int ) + req.ByteSizeLong();
        memcpy( g_send_buffer, &payload_size, sizeof( int ) );
        memcpy( g_send_buffer + sizeof( int ), &packet_number, sizeof( int ) );
        auto ret = req.SerializeToArray( g_send_buffer + sizeof( int ) + sizeof( int ), req.ByteSizeLong() );
        if( false == ret )
        {
            LOG_E( "Failed Serialization" );
            return;
        }

        auto send_result = send( sc.GetSocket(), g_send_buffer, packet_size, 0 );
        LOG_D( "Send. {} bytes", send_result );
    }
    else if( 0 == cmds[ 0 ].compare( "create" ) )
    {
        chat::chat_create_req req;
        auto ids = req.add_ids();
        int packet_number = htonl( req.GetDescriptor()->index() );

    }
    else if( 0 == cmds[ 0 ].compare( "message" ) )
    {
        chat::chat_message_req req;
        int packet_number = htonl( req.GetDescriptor()->index() );

    }
}

int main()
{
    using namespace std::string_literals;
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    SocketClient sc( std::string( "127.0.0.1" ), 10010 );
    sc.InitClient();
    sc.ConnectToServer();

    while( true )
    {
        LOG_D( "Reday to send...");
        std::string message;
        std::getline( std::cin, message );
        std::vector<std::string> cmds;
        ParseString( message, " "s, cmds );
        SendCommand( sc, cmds );
    }

    return 0;
}