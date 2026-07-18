/**
 * @file quark.hpp
 * @brief Umbrella header for the quark lock-free library.
 *
 * Includes the full public surface. Prefer narrower headers in production
 * translation units to keep compile times down.
 *
 * @author Carlos Salguero
 * @date 2026
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/quark.hpp>
 * @endcode
 */

#pragma once

#include <quark/core/types.hpp>
#include <quark/memory/hazard_ptr.hpp>
#include <quark/memory/tagged_ptr.hpp>
#include <quark/util/bench.hpp>
#include <quark/util/log.hpp>
