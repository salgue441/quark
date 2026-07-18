/**
 * @file config.hpp
 * @brief Global configuration settings for the lock-free library.
 *
 * This header defines compile-time configuration constants that control
 * the behavior of the library. It provides a central place to enable or
 * disable features such as logging and debug checks based on build
 * configurations.
 *
 * The configuration is designed to be flexible, allowing external definition
 * of macros to override default behavior. This is particularly useful for
 * controlling the library's behavior in different build environments
 * (e.g., release vs. debug builds).
 *
 * @author Carlos Salguero
 * @date 2026
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <lf/config.hpp>
 *
 * void example() {
 *     if constexpr (lf::Config::debug_checks) {
 *         // Perform expensive validation only in debug builds
 *         validate_invariants();
 *     }
 *
 *     if constexpr (lf::Config::logging_enabled) {
 *         // Log operations for monitoring
 *         LF_INFO("Operation completed successfully");
 *     }
 * }
 * @endcode
 */

#pragma once

namespace lf {

/**
 * @brief Compile-time configuration settings for the library.
 *
 * This struct provides static constexpr members that define the library's
 * behavior at compile time. All members are evaluated at compile time,
 * allowing the compiler to optimize away code that is conditionally
 * disabled.
 *
 * The configuration can be customized by defining preprocessor macros
 * before including this header:
 * - `LF_LOGGING_ENABLED`: Set to 0 to disable logging, 1 to enable it
 * - `NDEBUG`: Standard macro; when defined, debug checks are disabled
 *
 * @note All members are `static constexpr`, so they can be used in
 *       compile-time contexts such as `if constexpr` and template
 *       metaprogramming.
 */
struct Config {
  /**
   * @brief Whether logging is enabled.
   *
   * When true, logging macros (LF_TRACE, LF_DEBUG, LF_INFO, etc.) will
   * produce output. When false, logging calls may be optimized away by
   * the compiler.
   *
   * The default is true. To disable logging, define `LF_LOGGING_ENABLED`
   * as 0:
   * @code
   * #define LF_LOGGING_ENABLED 0
   * #include <lf/config.hpp>
   * @endcode
   *
   * @note Even when logging is enabled, the actual log level can be
   *       controlled at runtime via Logger::set_level().
   *
   * @see Logger::set_level()
   */
#ifndef LF_LOGGING_ENABLED
  static constexpr bool logging_enabled = true;
#else
  static constexpr bool logging_enabled = (LF_LOGGING_ENABLED != 0);
#endif

  /**
   * @brief Whether debug checks are enabled.
   *
   * When true, the library performs additional validation and assertions
   * that may be expensive. When false, these checks are disabled for
   * maximum performance.
   *
   * This value is automatically set based on the presence of the `NDEBUG`
   * macro:
   * - If `NDEBUG` is not defined (debug build), debug_checks = true
   * - If `NDEBUG` is defined (release build), debug_checks = false
   *
   * @code
   * // Debug build: checks enabled
   * #undef NDEBUG
   * #include <lf/config.hpp>
   *
   * // Release build: checks disabled
   * #define NDEBUG
   * #include <lf/config.hpp>
   * @endcode
   *
   * @note Debug checks may include invariants, boundary checks,
   *       and other validation that adds overhead. They are intended for
   *       development and testing, not for production use.
   */
#ifdef NDEBUG
  static constexpr bool debug_checks = false;
#else
  static constexpr bool debug_checks = true;
#endif
};

} // namespace lf
