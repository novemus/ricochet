#pragma once

#include <string>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/support/date_time.hpp>

namespace ricochet::logging {

// Initialize console logging with optional filtering
void init_console_logging(boost::log::trivial::severity_level level = boost::log::trivial::info);

// Initialize file logging with rotation
void init_file_logging(const std::string& filename, 
                      boost::log::trivial::severity_level level = boost::log::trivial::info,
                      size_t rotation_size = 10 * 1024 * 1024);

// Set global log level filtering
void set_log_level(boost::log::trivial::severity_level level);

// Get global log level filtering
boost::log::trivial::severity_level get_log_level();

// Convert string to log level
boost::log::trivial::severity_level parse_log_level(const std::string& level_str);

// Cleanup logging resources
void cleanup();

// Convenience macros for logging
#define LOG_FATAL() BOOST_LOG_SEV(ricochet::logging::get_logger(), boost::log::trivial::fatal)
#define LOG_WARNING() BOOST_LOG_SEV(ricochet::logging::get_logger(), boost::log::trivial::warning)
#define LOG_INFO() BOOST_LOG_SEV(ricochet::logging::get_logger(), boost::log::trivial::info)
#define LOG_ERROR() BOOST_LOG_SEV(ricochet::logging::get_logger(), boost::log::trivial::error)
#define LOG_DEBUG() BOOST_LOG_SEV(ricochet::logging::get_logger(), boost::log::trivial::debug)
#define LOG_TRACE() BOOST_LOG_SEV(ricochet::logging::get_logger(), boost::log::trivial::trace)

// Short logging macros for convenience
#define _ftl_ LOG_FATAL()
#define _err_ LOG_ERROR()
#define _inf_ LOG_INFO()
#define _wrn_ LOG_WARNING()
#define _dbg_ LOG_DEBUG()
#define _trc_(exp) if (logging::get_log_level() == boost::log::trivial::trace) [&]() { LOG_TRACE() << exp; }();

// Get the global logger instance
boost::log::sources::severity_logger<boost::log::trivial::severity_level>& get_logger();

} // namespace ricochet::logging
