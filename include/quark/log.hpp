/**
 * @file logger.hpp
 * @brief Thread-safe logging system with configurable sinks and log levels.
 *
 * This header provides a comprehensive logging framework with support for
 * multiple output sinks (console, file), log levels, colored output, and
 * automatic log rotation. It is designed for high-performance logging in
 * concurrent environments with minimal contention.
 *
 * The logging system follows a singleton pattern and provides both direct
 * logging methods and convenient macros for different log levels. Log
 * records automatically capture source location information and timestamps.
 *
 * @author Carlos Salguero
 * @date 2026-07-17
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/logger.hpp>
 *
 * void example() {
 *     // Configure logging
 *     auto console = std::make_shared<quark::log::ConsoleSink>();
 *     quark::log::Logger::instance().add_sink(console);
 *     quark::log::Logger::instance().set_level(quark::log::Level::Debug);
 *
 *     // Using macros
 *     quark_INFO("Application started");
 *     quark_DEBUG("Processing item {}", item_id);
 *     quark_ERROR("Failed to process: {}", error_message);
 *
 *     // Using direct logging
 *     quark::log::Logger::instance().log(
 *         quark::log::Level::Warn,
 *         std::source_location::current(),
 *         "Custom log message with value: {}",
 *         42
 *     );
 * }
 * @endcode
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace quark::log {
/**
 * @brief Log severity levels.
 *
 * Defines the available log levels in increasing order of severity.
 * The Off level can be used to disable all logging.
 */
enum class Level : uint8_t {
  Trace, ///< Very detailed diagnostic information
  Debug, ///< Debugging information useful for development
  Info,  ///< General information about program execution
  Warn,  ///< Warning about potentially problematic situations
  Error, ///< Error conditions that don't prevent continued execution
  Fatal, ///< Critical errors that cause program termination
  Off    ///< No logging output (disables all logging)
};

/**
 * @brief Returns the string representation of a log level.
 *
 * @param level The log level
 * @return A string_view containing the level name (e.g., "INFO", "ERROR")
 */
constexpr std::string_view level_name(Level level) {
  switch (level) {
  case Level::Trace:
    return "TRACE";
  case Level::Debug:
    return "DEBUG";
  case Level::Info:
    return "INFO";
  case Level::Warn:
    return "WARN";
  case Level::Error:
    return "ERROR";
  case Level::Fatal:
    return "FATAL";
  case Level::Off:
    return "OFF";
  }

  return "UNKNOWN";
}

/**
 * @brief Returns the ANSI color code for a log level.
 *
 * @param level The log level
 * @return A string_view containing the ANSI color escape sequence
 *
 * @note Color codes are: Trace=white, Debug=cyan, Info=green,
 *       Warn=yellow, Error=red, Fatal=magenta
 */
constexpr std::string_view level_color(Level level) {
  switch (level) {
  case Level::Trace:
    return "\033[37m";
  case Level::Debug:
    return "\033[36m";
  case Level::Info:
    return "\033[32m";
  case Level::Warn:
    return "\033[33m";
  case Level::Error:
    return "\033[31m";
  case Level::Fatal:
    return "\033[35m";
  case Level::Off:
    return "\033[0m";
  default:
    return "\033[0m";
  }
}

/// @brief ANSI color reset sequence.
constexpr std::string_view COLOR_RESET = "\033[0m";

/**
 * @brief Represents a single log record.
 *
 * Contains all information about a logged event including the message,
 * severity level, source location, timestamp, and thread ID.
 */
struct Record {
  Level level;                   ///< Log severity level
  std::string message;           ///< Formatted log message
  std::source_location location; ///< Source location of the log call
  std::chrono::system_clock::time_point timestamp; ///< Time of the log event
  std::thread::id thread_id;                       ///< ID of the logging thread
};

/**
 * @brief Abstract base class for log output destinations.
 *
 * All log sinks must inherit from this class and implement the emit() method.
 * Sinks can be added to the logger to direct log output to different
 * destinations (console, file, network, etc.).
 */
class Sink {
public:
  virtual ~Sink() = default;

  /**
   * @brief Emits a log record to the sink.
   *
   * @param record The log record to output
   */
  virtual void emit(const Record &record) = 0;

  /**
   * @brief Flushes any buffered output.
   *
   * The default implementation does nothing.
   */
  virtual void flush() {}
};

/**
 * @brief Log sink that writes to the console (stdout or stderr).
 *
 * Supports colored output for different log levels. Thread-safe with
 * mutex protection for the output stream.
 */
class ConsoleSink : public Sink {
public:
  /**
   * @brief Constructs a ConsoleSink.
   *
   * @param use_color Whether to use ANSI color codes in output
   * @param use_stderr Whether to write to stderr (true) or stdout (false)
   */
  explicit ConsoleSink(bool use_color = true, bool use_stderr = false)
      : m_use_color(use_color), m_stream(use_stderr ? std::cerr : std::cout) {}

  /**
   * @brief Emits a log record to the console.
   *
   * Formats the record with timestamp, level, source location, and message,
   * then writes it to the output stream with a newline.
   *
   * @param record The log record to output
   */
  void emit(const Record &record) override {
    auto msg = format(record);
    std::lock_guard lock(m_mutex);

    m_stream << msg << std::endl;
  }

  /**
   * @brief Flushes the output stream.
   *
   * Forces any buffered output to be written.
   */
  void flush() override {
    std::lock_guard(m_mutex);
    m_stream.flush();
  }

private:
  /**
   * @brief Formats a log record for console output.
   *
   * @param record The log record to format
   * @return A formatted string
   */
  std::string format(const Record &record) {
    auto ts = format_timestamp(record.timestamp);
    auto loc = std::format(
        "{}:{}",
        std::string_view(record.location.file_name())
            .substr(std::string_view(record.location.file_name()).rfind(/) + 1),
        record.location.line());

    if (m_use_color) {
      return std::format("{}[{}] [{}] [{}]{} {}", level_color(record.level), ts,
                         level_name(record.level), loc, COLOR_RESET,
                         record.message);
    }

    return std::format("[{}] [{}] [{}] {}", ts, level_name(record.level), loc,
                       record.message);
  }

  /**
   * @brief Formats a timestamp for output.
   *
   * @param timestamp The timestamp to format
   * @return A formatted timestamp string in "YYYY-MM-DD HH:MM:SS.mmm" format
   */
  static std::string
  format_timestamp(const std::chrono::system_clock::time_point &timestamp) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  timestamp.time_since_epoch()) %
              100;

    auto t = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                       tm.tm_min, tm.tm_sec, ms.count());
  }

private:
  bool m_use_color;       ///< Whether to use colored output
  std::ostream &m_stream; ///< Reference to the output stream
  std::mutex m_mutex;     ///< Mutex for thread-safe stream access
};

/**
 * @brief Log sink that writes to rotating files.
 *
 * Automatically rotates log files when they exceed a maximum size.
 * Maintains a specified number of backup files.
 */
class FileSink : public Sink {
public:
  /**
   * @brief Configuration for the FileSink.
   */
  struct Config {
    std::string path = "lockfree.log"; ///< Base path for log files
    std::size_t max_bytes =
        10 * 1024 * 1024; ///< Maximum file size (default: 10MB)
    int max_files = 5;    ///< Maximum number of backup files to keep
  };

  /**
   * @brief Constructs a FileSink with the given configuration.
   *
   * @param config The configuration to use
   */
  explicit FileSink(Config config) : m_config(std::move(config)) {
    open_file();
  }

  /**
   * @brief Emits a log record to the file.
   *
   * Writes the formatted record to the current log file. If the file exceeds
   * the maximum size, it triggers log rotation.
   *
   * @param record The log record to output
   */
  void emit(const Record &record) override {
    auto line = std::format(
        "[{}] [{}] [{}:{}] {}\n", format_timestamp(record.timestamp),
        level_name(record.level), record.location.file_name(),
        record.location.line(), record.message);

    std::lock_guard lock(m_mutex);
    m_file << line;
    m_bytes_written += line.size();

    if (m_bytes_written >= m_config.max_bytes) {
      rotate();
    }
  }

  /**
   * @brief Flushes the file stream.
   *
   * Forces any buffered data to be written to disk.
   */
  void flush() override {
    std::lock_guard lock(m_mutex);
    m_file.flush();
  }

private:
  /**
   * @brief Opens the current log file.
   *
   * Opens the file in append mode and calculates the current size.
   */
  void open_file() {
    m_file.open(m_config.path, std::ios::app);
    m_bytes_written = std::filesystem::exists(m_config.path)
                          ? std::filesystem::file_size(m_config.path)
                          : 0;
  }

  /**
   * @brief Rotates the log files.
   *
   * Renames existing log files to create backups (e.g., .0.log, .1.log, etc.)
   * and starts a new log file.
   */
  void rotate() {
    m_file.close();
    for (int i = m_config.max_files - 1; i >= 1; --i) {
      auto src = std::format("{}.{}.log", m_config.path, i - 1);
      auto dest = std::format("{}.{}.log", m_config.path, i);

      if (std::filesystem::exists(src))
        std::filesystem::rename(src, dest);
    }

    std::filesystem::rename(m_config.path, m_config.path + ".0.log");
    open_file();
  }

  /**
   * @brief Formats a timestamp for output.
   *
   * @param timestamp The timestamp to format
   * @return A formatted timestamp string in "YYYY-MM-DD HH:MM:SS.mmm" format
   */
  static std::string
  format_timestamp(const std::chrono::system_clock::time_point &timestamp) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  timestamp.time_since_epoch()) %
              100;

    auto t = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                       tm.tm_min, tm.tm_sec, ms.count());
  }

private:
  Config m_config;                 ///< The sink configuration
  std::ofstream m_file;            ///< Output file stream
  std::size_t m_bytes_written = 0; ///< Bytes written to the current file
  std::mutex m_mutex;              ///< Mutex for thread-safe file access
};

/**
 * @brief Main logger class (singleton).
 *
 * Manages multiple log sinks and log level filtering. Provides thread-safe
 * logging with support for formatted messages and source location capture.
 *
 * The logger is a singleton accessed via Logger::instance(). Log records are
 * dispatched to all registered sinks. If a Fatal level log is emitted, all
 * sinks are flushed and std::abort() is called.
 */
class Logger {
public:
  /**
   * @brief Gets the singleton logger instance.
   *
   * @return Reference to the global logger instance
   */
  static Logger &instance() {
    static Logger logger;
    return logger;
  }

  /**
   * @brief Adds a sink to the logger.
   *
   * @param sink The sink to add (shared_ptr ownership)
   */
  void add_sink(std::shared_ptr<Sink> sink) {
    std::lock_guard lock(m_mutex);
    m_sinks.push_back(std::move(sink));
  }

  /**
   * @brief Sets the minimum log level.
   *
   * Messages with a level below this threshold are ignored.
   *
   * @param level The minimum log level to process
   */
  void set_level(Level level) {
    m_min_level.store(level, std::memory_order_relaxed);
  }

  /**
   * @brief Logs a formatted message.
   *
   * @tparam Args The types of the format arguments
   * @param level The log level
   * @param location The source location (automatically captured by macro)
   * @param fmt The format string
   * @param args The format arguments
   */
  template <typename... Args>
  void log(Level level, std::source_location location,
           std::format_strings<Args...> fmt, Arg &&...args) {
    if (level < m_min_level.load(std::memory_order_relaxed))
      return;

    Record record{
        .level = level,
        .message = std::format(fmt, std::forward<Args>(args)...),
        .location = location,
        .timestamp = std::chrono::system_clock::now(),
        .thread_id = std::this_thread::get_id(),
    };

    std::lock_guard lock(m_mutex);
    for (auto &sink : m_sinks)
      sink->emit(record);

    if (level == Level::Fatal) {
      for (auto &sink : m_sinks)
        sink->flush();
      std::abort();
    }
  }

private:
  Logger() = default;
  std::atomic<Level> m_min_level{Level::Info}; ///< Minimum log level
  std::vector<std::shared_ptr<Sink>> m_sinks;  ///< Registered log sinks
  std::mutex m_mutex;                          ///< Mutex for sink access
};

/**
 * @brief Log macro that takes an explicit level.
 *
 * @param level The log level (e.g., Info, Debug, Error)
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_LOG(Info, "User {} logged in", username);
 * @endcode
 */
#define QUARK_LOG(level, fmt, ...)                                                \
  ::quark::log::Logger::instance().log(::quark::log::Level::level,                   \
                                    std::source_location::current(),           \
                                    fmt __VA_OPT__(, ) __VA_ARGS__)

/**
 * @brief Logs a TRACE level message.
 *
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_TRACE("Entering function {}", function_name);
 * @endcode
 */
#define QUARK_TRACE(fmt, ...) quark_LOG(Trace, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a DEBUG level message.
 *
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_DEBUG("Variable value: {}", value);
 * @endcode
 */
#define QUARK_DEBUG(fmt, ...) quark_LOG(Debug, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an INFO level message.
 *
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_INFO("Application initialized successfully");
 * @endcode
 */
#define QUARK_INFO(fmt, ...) quark_LOG(Info, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a WARN level message.
 *
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_WARN("Memory usage is high: {}%", usage);
 * @endcode
 */
#define QUARK_WARN(fmt, ...) quark_LOG(Warn, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an ERROR level message.
 *
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_ERROR("Failed to open file: {}", filename);
 * @endcode
 */
#define QUARK_ERROR(fmt, ...) quark_LOG(Error, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a FATAL level message and terminates the program.
 *
 * After logging, all sinks are flushed and std::abort() is called.
 *
 * @param fmt The format string
 * @param ... The format arguments
 *
 * @code
 * quark_FATAL("Critical system failure: {}", error);
 * @endcode
 */
#define QUARK_FATAL(fmt, ...) quark_LOG(Fatal, fmt, ##__VA_ARGS__)
} // namespace quark::log
