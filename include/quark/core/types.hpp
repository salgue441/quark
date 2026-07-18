/**
 * @file core/types.hpp
 * @brief Core types and constants shared across the quark library.
 *
 * Pulls in configuration, version, error/`Result`, and architecture helpers.
 * Domain-specific constants (tagged-pointer layout, hazard-pointer limits)
 * remain in their respective modules under `quark/memory/`.
 *
 * @author Carlos Salguero
 * @date 2026
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/core/types.hpp>
 *
 * static_assert(quark::Version::major >= 0);
 * quark::CacheAligned<int> counter{};
 * quark::Result<int> value = quark::Ok(42);
 * @endcode
 */

#pragma once

#include <quark/core/arch.hpp>
#include <quark/core/config.hpp>
#include <quark/core/error.hpp>
#include <quark/core/version.hpp>
