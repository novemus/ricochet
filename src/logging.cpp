#include "logging.h"
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/attributes/current_thread_id.hpp>
#include <boost/log/attributes/function.hpp>
#include <boost/core/ignore_unused.hpp>

#include <iomanip>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace ricochet::logging {

namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;

static boost::log::sources::severity_logger<boost::log::trivial::severity_level> g_logger;
static boost::log::trivial::severity_level g_level;

// Get native thread ID (TID) visible in system utilities like htop
static uint64_t get_native_thread_id()
{
#if defined(_WIN32)
    return GetCurrentThreadId();
#elif defined(__APPLE__)
    uint64_t tid;
    pthread_threadid_np(pthread_self(), &tid);
    return tid;
#elif defined(__linux__)
    return syscall(SYS_gettid);
#else
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
}

boost::log::sources::severity_logger<boost::log::trivial::severity_level>& get_logger()
{
    return g_logger;
}

void init_console_logging(boost::log::trivial::severity_level level)
{
    // Add common attributes (timestamp, thread id, etc.)
    logging::add_common_attributes();

    boost::log::core::get()->add_global_attribute("NativeThreadID", 
        boost::log::attributes::make_function([]() -> uint64_t { return get_native_thread_id(); }));

    // Set up console sink with formatting
    logging::add_console_log(
        std::clog,
        keywords::format = (
            expr::stream
                << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                << " "
                << expr::attr<uint64_t>("NativeThreadID")
                << " "
                << std::setw(8) << std::left
                << logging::trivial::severity
                << ": "
                << expr::smessage
        ),
        keywords::filter = logging::trivial::severity >= level
    );
}

void init_file_logging(const std::string& filename, 
                      boost::log::trivial::severity_level level,
                      size_t rotation_size)
{
    // Add common attributes if not already added
    logging::add_common_attributes();
    
    // Add native thread ID attribute
    boost::log::core::get()->add_global_attribute("NativeThreadID", 
        boost::log::attributes::make_function([]() -> uint64_t { return get_native_thread_id(); }));
    
    // Set up file sink with rotation
    logging::add_file_log(
        keywords::file_name = filename,
        keywords::rotation_size = rotation_size,
        keywords::time_based_rotation = logging::sinks::file::rotation_at_time_point(0, 0, 0),
        keywords::format = (
            expr::stream
                << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                << " "
                << expr::attr<uint64_t>("NativeThreadID")
                << " "
                << std::setw(8) << std::left
                << logging::trivial::severity
                << ": "
                << expr::smessage
        ),
        keywords::filter = logging::trivial::severity >= level,
        keywords::auto_flush = true
    );
}

void set_log_level(boost::log::trivial::severity_level level)
{
    g_level = level;
    logging::core::get()->set_filter(logging::trivial::severity >= level);
}

boost::log::trivial::severity_level get_log_level()
{
    return g_level;
}

boost::log::trivial::severity_level parse_log_level(const std::string& level_str)
{
    if (level_str == "trace" || level_str == "TRACE")
    {
        return boost::log::trivial::trace;
    }
    else if (level_str == "debug" || level_str == "DEBUG")
    {
        return boost::log::trivial::debug;
    }
    else if (level_str == "info" || level_str == "INFO")
    {
        return boost::log::trivial::info;
    }
    else if (level_str == "warning" || level_str == "WARNING")
    {
        return boost::log::trivial::warning;
    }
    else if (level_str == "error" || level_str == "ERROR")
    {
        return boost::log::trivial::error;
    }
    else if (level_str == "fatal" || level_str == "FATAL")
    {
        return boost::log::trivial::fatal;
    }
    else
    {
        // Default to info level
        return boost::log::trivial::info;
    }
}

void cleanup()
{
    // Flush all sinks
    logging::core::get()->flush();
    // Remove all sinks
    logging::core::get()->remove_all_sinks();
}

} // namespace ricochet::logging
