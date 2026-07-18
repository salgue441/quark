/**
 * @file core/version.hpp
 * @brief Library version identifiers for quark.
 *
 * @author Carlos Salguero
 * @date 2026
 * @copyright Copyright (c) 2026
 */

#pragma once

#define QUARK_VERSION_MAJOR 0
#define QUARK_VERSION_MINOR 1
#define QUARK_VERSION_PATCH 0

#define QUARK_VERSION_STRING "0.1.0"

namespace quark {

/**
 * @brief Compile-time semantic version of the library.
 */
struct Version {
  static constexpr int major = QUARK_VERSION_MAJOR;
  static constexpr int minor = QUARK_VERSION_MINOR;
  static constexpr int patch = QUARK_VERSION_PATCH;

  static constexpr const char *string = QUARK_VERSION_STRING;
};

} // namespace quark
