/**
 * @file timer.hpp
 * @brief Performance measurement utilities for profiling lock-free operations.
 *
 * This header provides RAII-based timing utilities for measuring and logging
 * the execution time of code blocks. It is primarily intended for debugging
 * and performance analysis during development.
 *
 * The ScopedTimer class automatically measures the time from construction to
 * destruction and outputs the elapsed time in milliseconds to stdout. The
 * LF_TIMED macro provides a convenient syntax for creating scoped timers.
 *
 * @warning These utilities should not be used in production code where
 *          performance is critical, as they introduce overhead from
 *          logging and chrono operations.
 *
 * @author Carlos Salguero
 * @date 2026-07-17
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/timer.hpp>
 *
 * void benchmark() {
 *     // Using the macro
 *     LF_TIMED("sort_operation") {
 *         std::vector<int> data = { ... };
 *         std::sort(data.begin(), data.end());
 *     }
 *
 *     // Using the class directly
 *     {
 *         quark::ScopedTimer timer("explicit_timer");
 *         // ... operations ...
 *     }
 * }
 * @endcode
 */
#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <string>

namespace quark {

/**
 * @brief RAII timer for measuring and logging execution time.
 *
 * ScopedTimer automatically measures the time from construction to destruction
 * and logs the elapsed time in milliseconds to stdout. It is designed for
 * performance profiling and debugging.
 *
 * The timer uses std::chrono::steady_clock for monotic time measurements that
 * are immune to system clock adjustments.
 *
 * @code
 * {
 *      quark::ScopedTimer timer("expensive_operation");
 *      // ... do expensive operation ...
 * } // Automatically logs: "[timer] expensive_operation: 42.123 ms"
 * @endcode
 *
 * @note The destructor logs to std::cout, which may introduce overhead and
 *       should not be used in production code where performance is critical.
 */
class ScopedTimer {
public:
  /**
   * @brief Constructs a ScopedTimer with a given name.
   *
   * Records the current time using std::chrono::steady_clock as the start time.
   * The name is stored for logging purposes and will appear in the output
   * message.
   *
   * @param name The name of the timer, used for logging.
   */
  explicit ScopedTimer(std::string name) : m_name(std::move(name)) {
    m_start = std::chrono::steady_clock::now();
  }

  /**
   * @brief Destroys the ScopedTimer and logs the elapsed time.
   *
   * Calculates the duration from the start time to the current time, converts
   * it to milliseconds, and logs the result to std::cout.
   *
   * The output format is: `[timer] <name>: <duration> ms\n`
   * where <duration>` is formatted with 3 decimal places.
   *
   * @note If the elapsed time is extremely small (in the order of nanoseconds),
   *       the milliseconds value may be less than 0.001 and will be displayed
   *       as 0.000 ms.
   */
  ~ScopedTimer() {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - m_start)
                  .count();

    std::cout << std::format("[timer] {}: {:.3f} ms\n", m_name,
                             ns / 1'000'000.0);
  }

private:
  std::string m_name; ///< The name of this timer
  std::chrono::steady_clock::time_point
      m_start; ///< The time when the timer started
};

/**
 * @brief Macro for convenient scoped timing.
 *
 * Creates a ScopedTimer that lasts for the duration of the following statement
 * or block. The timer is automatically destroyed when the scope ends.
 *
 * @param name A string literal or expression convertible to std::string that
 *        names this timer.
 *
 * @code
 * // Single statement
 * QUARK_TIMED("single") expensive_function();
 *
 * // Block Scope
 * QUARK_TIMED("block") {
 *  // ... do work ...
 * }
 * @endcode
 *
 * @warning The macro uses an `if` statement with `true` condition to create a
 *          scope. This ensures the timer is destroyed immediately after the
 *          controlled statement/block, but may be confusing to static analyzer.
 *          Use the ScopedTimer class directly if you prefer more explicit code.
 *
 * @see ScopedTimer
 */
#define QUARK_TIMED(name) if (quark::ScopedTimer _t{name}; true)
} // namespace quark
