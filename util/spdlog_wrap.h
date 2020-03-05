#ifndef __SPDLOG_WRAP_H__
#define __SPDLOG_WRAP_H__

#include <iostream>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"

namespace JVF
{
    class Logger
    {
    public:
        // shoud be replaced to json or else
        struct LogConf
        {
            bool use_console = true;
            bool use_file = true;
            unsigned int file_size = 1024 * 1024 * 10; // 10MB default
            unsigned int managing_file_amount = 10; // 10 EA 
            std::string path = "logs/test.log";
            std::string console_log_level = "debug";
            std::string file_log_level = "debug";
        };

    public:
        Logger() = default;
        ~Logger()
        {
            if( nullptr != _logger )
            {
                delete _logger;
            }
        }

        static Logger& GetInstance()
        {
            static Logger lg;
            return lg;
        }

        spdlog::logger* GetLogger() { return _logger; }

        void Initialize( LogConf const& conf )
        {
            try
            {
                using namespace spdlog::sinks;

                std::vector<spdlog::sink_ptr> sinks_list;
                if( true == conf.use_console )
                {
                    auto console_sink = std::make_shared<stdout_color_sink_mt>();
                    console_sink->set_level( spdlog::level::from_str( conf.console_log_level ) );
                    console_sink->set_pattern( "[%L] [%Y %T][%t]%v" );
                    sinks_list.emplace_back( std::forward<spdlog::sink_ptr>( console_sink ) );
                }

                if( true == conf.use_file )
                {
                    auto rotating_sink = std::make_shared<rotating_file_sink_mt>( conf.path,
                                                                                  conf.file_size,
                                                                                  conf.managing_file_amount );
                    rotating_sink->set_level( spdlog::level::from_str( conf.file_log_level ) );
                    rotating_sink->set_pattern( "[%L] [%Y %T][%t]%v" );
                    sinks_list.emplace_back( std::forward<spdlog::sink_ptr>( rotating_sink ) );
                }

                _logger = new spdlog::logger( "multi_sink", sinks_list.begin(), sinks_list.end() );
                _logger->set_level( spdlog::level::level_enum::debug );

            }
            catch( const spdlog::spdlog_ex & ex )
            {
                std::cout << "Initialize failed. reason : " << ex.what() << std::endl;
            }
        }

    private:
        spdlog::logger* _logger = nullptr;

    };
};

#define LOGGER() JVF::Logger::GetInstance()

#define LOG_T(fmt, ...) if (nullptr != LOGGER().GetLogger()) { LOGGER().GetLogger()->trace("[{}@{}] "##fmt, __FUNCTION__, __LINE__, __VA_ARGS__); } 
#define LOG_D(fmt, ...) if (nullptr != LOGGER().GetLogger()) { LOGGER().GetLogger()->debug("[{}@{}] "##fmt, __FUNCTION__, __LINE__, __VA_ARGS__); } 
#define LOG_I(fmt, ...) if (nullptr != LOGGER().GetLogger()) { LOGGER().GetLogger()->info("[{}@{}] "##fmt, __FUNCTION__, __LINE__, __VA_ARGS__); } 
#define LOG_W(fmt, ...) if (nullptr != LOGGER().GetLogger()) { LOGGER().GetLogger()->warn("[{}@{}] "##fmt, __FUNCTION__, __LINE__, __VA_ARGS__); } 
#define LOG_E(fmt, ...) if (nullptr != LOGGER().GetLogger()) { LOGGER().GetLogger()->error("[{}@{}] "##fmt, __FUNCTION__, __LINE__, __VA_ARGS__); } 
#define LOG_C(fmt, ...) if (nullptr != LOGGER().GetLogger()) { LOGGER().GetLogger()->critical("[{}@{}] "##fmt, __FUNCTION__, __LINE__, __VA_ARGS__); } 

#endif
