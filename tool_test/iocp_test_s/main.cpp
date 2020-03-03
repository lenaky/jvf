#include "util/spdlog_wrap.h"

#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <Ws2tcpip.h>

#include <thread>
#include <vector>

enum PacketDirection : int
{
    PACKET_DIRECTION_IN = 0, // recv
    PACKET_DIRECTION_OUT = 1 // send
};

struct OverlappedEx
{
    WSAOVERLAPPED overlapped = { 0 };
    WSABUF overlapped_buffer = { 0 };
    char data_buffer[ 1024 ] = { 0, };
    PacketDirection direction = PACKET_DIRECTION_IN;
    
};

struct ClientInfo
{
    SOCKET client_sock = INVALID_SOCKET;
    OverlappedEx in;
    OverlappedEx out;
};

class IOCPServer
{
public:
    IOCPServer( std::string const& ip, 
                unsigned int const port, 
                DWORD const worker_thread_amount ) : _ip( ip ), 
                                                     _port( port ), 
                                                     _worker_thread_amount( worker_thread_amount )
    {

    }

    bool InitServer()
    {
        WSADATA wsaData;
        auto ret = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
        if( 0 != ret )
        {
            LOG_E( "WSAStartup Failed. {}", WSAGetLastError() );
            return false;
        }
        // overlapped socket 생성
        _listen_sock = WSASocket( AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED );
        if( INVALID_SOCKET == _listen_sock )
        {
            LOG_E( "WSASocket Failed. {}", WSAGetLastError() );
            return false;
        }
        LOG_I( "InitServer success" );

        return true;
    }

    bool BindSocket()
    {
        SOCKADDR_IN server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons( _port );
        inet_pton( AF_INET, _ip.c_str(), &server_addr.sin_addr );
        auto ret = bind( _listen_sock, ( SOCKADDR* )&server_addr, sizeof( SOCKADDR_IN ) );
        if( 0 != ret )
        {
            LOG_E( "bind failed! {}", WSAGetLastError() );
            return false;
        }

        LOG_I( "BindSocket success" );

        return true;
    }

    bool ListenSocket()
    {
        auto ret = listen( _listen_sock, 10 );
        if( 0 != ret )
        {
            LOG_E( "listen failed. {}", WSAGetLastError() );
            return false;
        }
        LOG_I( "ListenSocket success" );
        return true;
    }

    bool StartServer()
    {
        // If the function fails, the return value is NULL. 
        // To get extended error information, call the GetLastError function.
        _iocp_handle = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, NULL, _worker_thread_amount );
        if( NULL == _iocp_handle )
        {
            LOG_E( "CreateIoCompletionPort failed. {}", GetLastError() );
            return false;
        }

        for( DWORD i = 0; i < _worker_thread_amount; i++ )
        {
            _worker_threads.emplace_back( [ this ](){
                Worker();
            } );
        }

        _accept_thread = std::thread( [ this ](){
            AcceptThread();
        } );
        
        LOG_I( "StartServer success" );

        return true;
    }

    void AcceptThread()
    {
        SOCKADDR_IN client_addr;
        int addr_len = sizeof( SOCKADDR_IN );

        while( _accept_run )
        {
            ClientInfo* cli_info = new ClientInfo;
            LOG_I( "Wait for accept..." );
            cli_info->client_sock = accept( _listen_sock, ( SOCKADDR* )&client_addr, &addr_len );
            if( INVALID_SOCKET == cli_info->client_sock )
            {
                delete cli_info;
                continue;
            }
            LOG_I( "Wait for accept...ok" );

            {
                auto ret = CreateIoCompletionPort( ( HANDLE )cli_info->client_sock,
                                                   _iocp_handle,
                                                   ( ULONG_PTR )cli_info,
                                                   0 );

                if( NULL == ret || _iocp_handle != ret )
                {
                    LOG_E( "CreateIoCompletionPort failed. {}", GetLastError() );
                    delete cli_info;
                    continue;
                }
            }

            {
                DWORD flag = 0, recv_size = 0;
                cli_info->in.overlapped_buffer.len = 1024;
                cli_info->in.overlapped_buffer.buf = cli_info->in.data_buffer;
                cli_info->in.direction = PacketDirection::PACKET_DIRECTION_IN;

                auto ret = WSARecv( cli_info->client_sock,
                                    &cli_info->in.overlapped_buffer,
                                    1,
                                    &recv_size,
                                    &flag,
                                    ( LPWSAOVERLAPPED )&cli_info->in,
                                    NULL );
                if( SOCKET_ERROR == ret && WSAGetLastError() != ERROR_IO_PENDING )
                {
                    LOG_E( "WSARecv failed. {}", WSAGetLastError() )
                    continue;
                }

                char ip[ 32 ] = { 0, };
                inet_ntop( AF_INET, &client_addr.sin_addr, ip, sizeof( ip ) - 1 );
                LOG_I( "connected ip : {}", ip );

                _client_infos.push_back( cli_info );
            }
        }
    }

    void Worker()
    {
        while( _worker_run )
        {
            ClientInfo* cli_info = nullptr;
            DWORD io_size = 0;
            LPOVERLAPPED lpoverlapped = nullptr;

            while( _worker_run )
            {
                LOG_I( "Checking CompletionPort..." );
                BOOL ret = GetQueuedCompletionStatus( _iocp_handle,
                                                      &io_size,
                                                      ( PULONG_PTR )&cli_info,
                                                      &lpoverlapped,
                                                      INFINITE );
                LOG_I( "Checking CompletionPort...ok" );
                if( TRUE == ret && 0 == io_size && NULL == lpoverlapped )
                {
                    _worker_run = false;
                    break;
                }

                if( NULL == lpoverlapped )
                {
                    continue;
                }

                if( FALSE == ret || ( 0 == io_size && TRUE == ret ) )
                {
                    LOG_E( "Close Connection!" );
                    CloseConnection( cli_info );

                }

                OverlappedEx* overlappedex = ( OverlappedEx* )lpoverlapped;

                if( PacketDirection::PACKET_DIRECTION_IN == overlappedex->direction )
                {
                    overlappedex->data_buffer[ io_size ] = '\0';
                    LOG_I( "Recved Message : {}", overlappedex->data_buffer );

                    {
                        DWORD flag = 0, recv_size = 0;
                        cli_info->in.overlapped_buffer.len = 1024;
                        cli_info->in.overlapped_buffer.buf = cli_info->in.data_buffer;
                        cli_info->in.direction = PacketDirection::PACKET_DIRECTION_IN;

                        auto ret = WSARecv( cli_info->client_sock,
                                            &cli_info->in.overlapped_buffer,
                                            1,
                                            &recv_size,
                                            &flag,
                                            ( LPWSAOVERLAPPED )&cli_info->in,
                                            NULL );
                        if( SOCKET_ERROR == ret && WSAGetLastError() != ERROR_IO_PENDING )
                        {
                            LOG_E( "WSARecv failed. {}", WSAGetLastError() );
                            continue;
                        }
                    }
                }
            }
        }
    }

    bool StopServer()
    {
        _accept_run = false;
        if( _accept_thread.joinable() )
        {
            _accept_thread.join();
        }

        _worker_run = false;
        std::for_each( _worker_threads.begin(), _worker_threads.end(), []( std::thread& thr ) {
            if( thr.joinable() )
            {
                thr.join();
            }
        } );
        return true;
    }

    void CloseConnection( ClientInfo* cli_info )
    {
        struct linger lg = { 0, 0 }; // SO_DONTLINGER

        shutdown( cli_info->client_sock, SD_BOTH );
        setsockopt( cli_info->client_sock, SOL_SOCKET, SO_LINGER, ( char* )&lg, sizeof( lg ) );
        closesocket( cli_info->client_sock );
        // client_infos 에서 삭제
    }

private:
    std::string _ip;
    unsigned int _port = 0;
    SOCKET _listen_sock = INVALID_SOCKET;
    HANDLE _iocp_handle = INVALID_HANDLE_VALUE;
    DWORD _worker_thread_amount = 0;

    std::vector< ClientInfo* > _client_infos;

    std::thread _accept_thread;
    std::vector<std::thread> _worker_threads;

    bool _accept_run = true;
    bool _worker_run = true;

};

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    IOCPServer server( std::string( "127.0.0.1" ), 10010, 1 );
    server.InitServer();
    server.BindSocket();
    server.ListenSocket();
    server.StartServer();

    int i = 0;
    std::cin >> i;

    server.StopServer();

    return 0;
}