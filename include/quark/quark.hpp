/**
 * @file quark.hpp
 * @brief Umbrella header for the quark lock-free library.
 *
 * Includes the full public surface:
 * - `core/` — version, config, `Result`/`Error`, `CacheAligned`
 * - `memory/` — `TaggedPtr`, hazard-pointer reclamation
 * - `container/` — `SpscQueue`, `MsQueue`, and related structures
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
 *     quark::SpscQueue<int> q(8);
 *     q.try_push(1);
 *
 *     quark::MsQueue<int> msq;
 *     msq.try_push(1);
 * }
 * @endcode
 */

#pragma once

#include <quark/container/ms_queue.hpp>
#include <quark/container/spsc_queue.hpp>
#include <quark/core/types.hpp>
#include <quark/memory/hazard_ptr.hpp>
#include <quark/memory/tagged_ptr.hpp>
#include <quark/util/assert.hpp>
#include <quark/util/backoff.hpp>
#include <quark/util/bench.hpp>
#include <quark/util/log.hpp>
