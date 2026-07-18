/**
 * @file util/assert.hpp
 * @brief Debug assertions gated by quark::Config::debug_checks.
 *
 * Provides a zero-overhead assertion path for release builds and a hard
 * failure path when `Config::debug_checks` is enabled. Prefer
 * `QUARK_ASSERT` at call sites; use `assert_that` when you need an explicit
 * source location override.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/util/assert.hpp>
 *
 * void pop_unchecked(Node* node) {
 *     QUARK_ASSERT(node != nullptr);
 *     // ...
 * }
 * @endcode
 *
 * @see quark::Config::debug_checks
 */

#pragma once

#include <quark/core/config.hpp>

#include <cassert>
#include <cstdlib>
#include <source_location>
#include <string_view>

namespace quark {

/**
 * @brief Fails the process when debug checks are enabled and `cond` is false.
 *
 * When `Config::debug_checks` is false, the body is discarded after
 * `if constexpr` constant folding and the call is a no-op.
 *
 * @param cond Expression converted to bool; must be true when checks are on
 * @param expr Stringified expression used for diagnostics (may be unused)
 * @param loc Call-site location (defaults to the caller)
 *
 * @note Does not depend on the logging subsystem, so it remains usable
 *       during early startup / fatal paths.
 */
inline void assert_that(bool cond, std::string_view expr,
                        std::source_location loc =
                            std::source_location::current()) noexcept {
  if constexpr (Config::debug_checks) {
    if (!cond) {
      (void)expr;
      (void)loc;
      assert(false && "QUARK_ASSERT failed");
      std::abort();
    }
  } else {
    (void)cond;
    (void)expr;
    (void)loc;
  }
}

} // namespace quark

/**
 * @brief Asserts that `expr` is true when `quark::Config::debug_checks` is on.
 *
 * @param expr Boolean expression to evaluate
 *
 * @see quark::assert_that
 */
#define QUARK_ASSERT(expr)                                                     \
  ::quark::assert_that(static_cast<bool>(expr), #expr)
