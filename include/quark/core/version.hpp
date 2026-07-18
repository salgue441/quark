/**
 * @file core/version.hpp
 * @brief Library version identifiers for quark.
 *
 * Exposes both preprocessor macros (for build scripts / `#if` checks) and a
 * `quark::Version` aggregate for constexpr use in C++ code.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/core/version.hpp>
 *
 * static_assert(quark::Version::major == QUARK_VERSION_MAJOR);
 * static_assert(quark::Version::string[0] != '\0');
 * @endcode
 */

#pragma once

#define QUARK_VERSION_MAJOR 0 ///< Library major version component
#define QUARK_VERSION_MINOR 1 ///< Library minor version component
#define QUARK_VERSION_PATCH 0 ///< Library patch version component

#define QUARK_VERSION_STRING "0.1.0" ///< Dotted version string

namespace quark {

/**
 * @brief Compile-time semantic version of the library.
 *
 * Values mirror the `QUARK_VERSION_*` macros so either form can be used
 * depending on whether a preprocessor or constexpr context is required.
 */
struct Version {
  static constexpr int major = QUARK_VERSION_MAJOR; ///< Major component
  static constexpr int minor = QUARK_VERSION_MINOR; ///< Minor component
  static constexpr int patch = QUARK_VERSION_PATCH; ///< Patch component

  static constexpr const char *string =
      QUARK_VERSION_STRING; ///< Null-terminated dotted version
};

} // namespace quark
