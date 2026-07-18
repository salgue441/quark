/**
 * @file util/backoff.hpp
 * @brief Progressive backoff helpers for contended CAS loops.
 *
 * Starts with CPU pause hints, then yields the thread, then sleeps briefly.
 * Keeps hot paths responsive under light contention without spinning forever
 * under heavy load. Typical use is one `Backoff` instance per retry loop.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/util/backoff.hpp>
 *
 * quark::Backoff backoff;
 * while (!head.compare_exchange_weak(expected, desired)) {
 *     backoff.pause();
 *     expected = head.load(std::memory_order_acquire);
 * }
 * @endcode
 *
 * @see cpu_relax
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace quark {

/**
 * @brief Emits an architecture pause / yield hint for spin loops.
 *
 * On x86 this maps to `PAUSE`; on ARM to `yield`; elsewhere it falls back
 * to `std::this_thread::yield()`.
 *
 * @note Intended for very short waits inside tight CAS loops.
 */
inline void cpu_relax() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}

/**
 * @brief Exponential backoff state for a single retry loop.
 *
 * Progression by step:
 * 1. Tight `cpu_relax()` spins (powers of two iterations)
 * 2. `std::this_thread::yield()`
 * 3. Short sleeps, capped at 64 µs
 *
 * Call @ref reset when leaving a contended region so the next loop starts
 * from the cheapest stage again.
 */
class Backoff {
public:
  /**
   * @brief Resets the backoff schedule to the initial spin stage.
   */
  void reset() noexcept { m_step = 0; }

  /**
   * @brief Advances one backoff step and blocks accordingly.
   *
   * Safe to call repeatedly from a CAS failure path. The internal step
   * counter saturates so sleep duration remains bounded.
   */
  void pause() noexcept {
    constexpr std::uint32_t spin_limit = 6;
    constexpr std::uint32_t yield_limit = 10;
    constexpr std::uint32_t max_sleep_us = 64;

    if (m_step < spin_limit) {
      for (std::uint32_t i = 0; i < (1u << m_step); ++i)
        cpu_relax();
    } else if (m_step < yield_limit) {
      std::this_thread::yield();
    } else {
      const auto us = std::min<std::uint32_t>(1u << (m_step - yield_limit),
                                              max_sleep_us);
      std::this_thread::sleep_for(std::chrono::microseconds(us));
    }

    if (m_step < 16)
      ++m_step;
  }

private:
  std::uint32_t m_step = 0; ///< Current stage in the backoff schedule
};

} // namespace quark
