#include "util/spdlog_wrap.h"

#ifdef _DEBUG
#pragma comment(lib, "libprotobufd.lib")
#else
#pragma comment(lib, "libprotobuf.lib")
#endif

#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <Ws2tcpip.h>

#include <thread>
#include <vector>

#define IOCP_BUFFER_SIZE 1024 * 8 // 8k

class IOCPBuffer : public WSAOVERLAPPED
{
public:
    using byte = char;

    enum class PacketDirection : unsigned char
    {
        PacketDirection_None = 0,
        PacketDirection_Recv = 1,
        PacketDirection_Send = 2
    };

public:
    IOCPBuffer( unsigned int buffer_size, 
                PacketDirection direction ) : _buffer_size( buffer_size ), 
                                              _direction( direction )
    {
        _buffer = new byte[ buffer_size ];
    }

    virtual ~IOCPBuffer()
    {
        if( nullptr != _buffer )
            delete[] _buffer;
    }

    WSABUF& WsaBuf() { return _wsa_buffer; }
    byte* Buffer() { return _buffer; }

    bool CompareDirection( PacketDirection dir )
    {
        return dir == _direction;
    }

    void SetDirection( PacketDirection dir ) { _direction = dir; }

private:
    byte* _buffer = nullptr;
    size_t _buffer_size = 0;
    WSABUF _wsa_buffer{ 0 };
    PacketDirection _direction = PacketDirection::PacketDirection_None;
};

struct client_session
{
    SOCKET client_socket = INVALID_SOCKET;
};

struct _client_socket
{
    client_session* operator()()
    {
        return new client_session;
    }
} client_socket;

using Direction = IOCPBuffer::PacketDirection;

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

        // 쓰레드마다 버퍼 필요할듯..
        IOCPBuffer in_buffer( IOCP_BUFFER_SIZE, Direction::PacketDirection_Recv );

        while( _accept_run )
        {
            auto session = client_socket();
            LOG_I( "Wait for accept..." );
            session->client_socket = accept( _listen_sock, ( SOCKADDR* )&client_addr, &addr_len );
            if( INVALID_SOCKET == session->client_socket )
            {
                delete session;
                continue;
            }
            LOG_I( "Wait for accept...ok" );

            auto ret = CreateIoCompletionPort( ( HANDLE )session->client_socket,
                                                _iocp_handle,
                                                (ULONG_PTR)session ,
                                                0 );

            if( NULL == ret || _iocp_handle != ret )
            {
                LOG_E( "CreateIoCompletionPort failed. {}", GetLastError() );
                delete session;
                continue;
            }

            if( RecvData( session, &in_buffer ) )
            {
                char ip[ 32 ] = { 0, };
                inet_ntop( AF_INET, &client_addr.sin_addr, ip, sizeof( ip ) - 1 );
                LOG_I( "connected ip : {}", ip );
                _clients.push_back( session );
            }
            else
            {
                LOG_E( "connect failed" );
            }
        }
    }

    void Worker()
    {
        while( _worker_run )
        {
            client_session* session = nullptr;
            DWORD io_size = 0;
            LPOVERLAPPED lpoverlapped = nullptr;

            while( _worker_run )
            {
                LOG_I( "Checking CompletionPort..." );
                BOOL ret = GetQueuedCompletionStatus( _iocp_handle,
                                                      &io_size,
                                                      ( PULONG_PTR )&session,
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
                    CloseConnection( session );
                }

                IOCPBuffer* buffer = ( IOCPBuffer* )lpoverlapped;

                if( buffer->CompareDirection( Direction::PacketDirection_Recv ) )
                {
                    buffer->Buffer()[ io_size ] = '\0';
                    LOG_I( "Recved Message : {}", buffer->Buffer() );
                    if( RecvData( session, buffer ) )
                    {
                        LOG_I( "Recv Success" );
                    }
                }

                std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
            }
        }
    }

    bool RecvData( client_session* session, IOCPBuffer* buffer )
    {
        DWORD flag = 0, recv_size = 0;
        buffer->WsaBuf().len = IOCP_BUFFER_SIZE;
        buffer->WsaBuf().buf = buffer->Buffer();
        buffer->SetDirection( Direction::PacketDirection_Recv );

        auto ret = WSARecv( session->client_socket,
                            &buffer->WsaBuf(),
                            1,
                            &recv_size,
                            &flag,
                            ( LPWSAOVERLAPPED )buffer,
                            NULL );
        if( SOCKET_ERROR == ret && WSAGetLastError() != ERROR_IO_PENDING )
        {
            LOG_E( "WSARecv failed. {}", WSAGetLastError() );
            return false;
        }

        return true;
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

    void CloseConnection( client_session* cli_info )
    {
        struct linger lg = { 0, 0 }; // SO_DONTLINGER

        shutdown( cli_info->client_socket, SD_BOTH );
        setsockopt( cli_info->client_socket, SOL_SOCKET, SO_LINGER, ( char* )&lg, sizeof( lg ) );
        closesocket( cli_info->client_socket );
        // client_infos 에서 삭제
    }

private:
    std::string _ip;
    unsigned int _port = 0;
    SOCKET _listen_sock = INVALID_SOCKET;
    HANDLE _iocp_handle = INVALID_HANDLE_VALUE;
    DWORD _worker_thread_amount = 0;

    std::vector< client_session* > _clients;

    std::thread _accept_thread;
    std::vector<std::thread> _worker_threads;

    bool _accept_run = true;
    bool _worker_run = true;

};

int main()
{
    LOGGER().Initialize( { true, true, 1024 * 1024 * 10, 10, "logs/test.log", "debug", "debug" } );

    IOCPServer server( std::string( "127.0.0.1" ), 10010, 4 );
    server.InitServer();
    server.BindSocket();
    server.ListenSocket();
    server.StartServer();

    int i = 0;
    std::cin >> i;

    server.StopServer();

    return 0;
}