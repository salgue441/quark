/**
 * @file util/bench.hpp
 * @brief Performance measurement utilities for profiling lock-free operations.
 *
 * This header provides RAII-based timing utilities for measuring and logging
 * the execution time of code blocks. It is primarily intended for debugging
 * and performance analysis during development.
 *
 * The ScopedTimer class automatically measures the time from construction to
 * destruction and outputs the elapsed time in milliseconds to stdout. The
 * QUARK_TIMED macro provides a convenient syntax for creating scoped timers.
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
 * #include <quark/util/bench.hpp>
 *
 * void benchmark() {
 *     QUARK_TIMED("sort_operation") {
 *         std::vector<int> data = { ... };
 *         std::sort(data.begin(), data.end());
 *     }
 *
 *     {
 *         quark::ScopedTimer timer("explicit_timer");
 *         // ... operations ...
 *     }
 * }
 * @endcode
 */
#pragma once

#include <chrono>
#include <print>
#include <string>
#include <utility>

namespace quark {

/**
 * @brief RAII timer for measuring and logging execution time.
 *
 * Uses std::chrono::steady_clock and reports via std::println (C++23).
 */
class ScopedTimer {
public:
  explicit ScopedTimer(std::string name) noexcept
      : m_name(std::move(name)), m_start(std::chrono::steady_clock::now()) {}

  ScopedTimer(const ScopedTimer &) = delete;
  ScopedTimer &operator=(const ScopedTimer &) = delete;

  ~ScopedTimer() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - m_start)
                        .count();

    std::println("[timer] {}: {:.3f} ms", m_name, ns / 1'000'000.0);
  }

private:
  std::string m_name;
  std::chrono::steady_clock::time_point m_start;
};

} // namespace quark

/**
 * @brief Macro for convenient scoped timing.
 *
 * @param name A string literal or expression convertible to std::string that
 *        names this timer.
 *
 * @code
 * QUARK_TIMED("single") expensive_function();
 *
 * QUARK_TIMED("block") {
 *  // ... do work ...
 * }
 * @endcode
 *
 * @see ScopedTimer
 */
#define QUARK_TIMED(name) if (quark::ScopedTimer _t{name}; true)
