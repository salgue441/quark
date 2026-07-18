/**
 * @file quark.hpp
 * @brief Umbrella header for the quark lock-free library.
 *
 * Includes the full public surface:
 * - `core/` — version, config, `Result`/`Error`, `CacheAligned`
 * - `memory/` — `TaggedPtr`, hazard-pointer reclamation
 * - `util/` — logging, timing, assertions, CAS backoff
 *
 * Prefer narrower headers in production translation units to keep compile
 * times down.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/quark.hpp>
 *
 * int main() {
 *     auto& domain = quark::default_domain();
 *     (void)domain;
 *     quark::Backoff backoff;
 *     backoff.pause();
 * }
 * @endcode
 */

#pragma once

#include <quark/core/types.hpp>
#include <quark/memory/hazard_ptr.hpp>
#include <quark/memory/tagged_ptr.hpp>
#include <quark/util/assert.hpp>
#include <quark/util/backoff.hpp>
#include <quark/util/bench.hpp>
#include <quark/util/log.hpp>
