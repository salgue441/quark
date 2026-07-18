/**
 * @file cache.hpp
 * @brief Cache-aware utilities for performance optimization.
 *
 * This header provides utilities for cache-line alignment and padding to
 * reduce false sharing in concurrent data structures. It defines the cache
 * line size for the target architecture and provides a template for
 * cache-aligned data types.
 *
 * False sharing occurs when multiple threads access different variables that
 * reside on the same cache line, causing unnecessary cache coherence traffic.
 * By ensuring that frequently accessed data is placed on separate cache lines,
 * performance can be significantly improved in multi-threaded applications.
 *
 * @author Carlos Salguero
 * @date 2026
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <lf/cache.hpp>
 *
 * // A counter that will be accessed by multiple threads
 * struct alignas(CACHE_LINE) ThreadLocalCounter {
 *     std::atomic<std::size_t> value;
 * };
 *
 * // Using CacheAligned for automatic padding
 * lf::CacheAligned<std::atomic<std::size_t>> counters[4];
 *
 * // Each counter is guaranteed to be on its own cache line,
 * // preventing false sharing between threads.
 * @endcode
 */

#pragma once

#include <cstddef>
#include <new>

namespace lf {

/**
 * @brief The size of a CPU cache line in bytes.
 *
 * This constant represents the size of a cache line for the target architecture.
 * It is used to align data structures to avoid false sharing between cores.
 *
 * On compilers that support `std::hardware_destructive_interference_size`
 * (C++17 and later), this value is automatically determined at compile time.
 * Otherwise, a default of 64 bytes is used, which is the cache line size
 * for most modern x86-64 and ARM processors.
 *
 * @note The value is defined as `constexpr` and can be used in compile-time
 *       contexts such as `alignas()` and array sizes.
 *
 * @see CacheAligned
 */
inline constexpr std::size_t CACHE_LINE =
#ifdef __cpp_lib_hardware_interference_size
    std::hardware_destructive_interference_size;
#else
    64;  ///< Default cache line size for most architectures
#endif

/**
 * @brief A wrapper that ensures a value is aligned to a cache line boundary.
 *
 * This template wraps a value of type T and pads it so that the entire
 * structure occupies an exact multiple of the cache line size. This ensures
 * that when multiple CacheAligned objects are placed in an array, each object
 * starts on a new cache line, preventing false sharing.
 *
 * The padding is calculated as:
 * @code
 * CACHE_LINE - (sizeof(T) % CACHE_LINE)
 * @endcode
 * If sizeof(T) is already a multiple of CACHE_LINE, the padding size is 0.
 *
 * @tparam T The type to be cache-aligned
 *
 * @code
 * // Array of counters, each on a separate cache line
 * lf::CacheAligned<std::atomic<size_t>> counters[4];
 *
 * // Access counter for thread 0
 * counters[0].value.fetch_add(1);
 *
 * // Access counter for thread 1 - on a different cache line
 * counters[1].value.fetch_add(1);
 * @endcode
 *
 * @note This wrapper is useful for variables that are frequently accessed
 *       by different threads. For variables that are accessed by a single
 *       thread or rarely accessed, the padding may be unnecessary overhead.
 *
 * @see CACHE_LINE
 */
template <typename T>
struct alignas(CACHE_LINE) CacheAligned {
    /**
     * @brief The wrapped value.
     *
     * This is the actual data that benefits from cache line alignment.
     */
    T value;

    /**
     * @brief Padding bytes to fill the remainder of the cache line.
     *
     * This array ensures that the total size of the structure is an exact
     * multiple of CACHE_LINE. The padding is calculated automatically by
     * the compiler based on the alignment requirement.
     *
     * The actual size of this array is:
     * @code
     * CACHE_LINE - (sizeof(T) % CACHE_LINE)
     * @endcode
     *
     * @note If sizeof(T) is exactly a multiple of CACHE_LINE, the size of
     *       this array will be 0 (zero-length array).
     */
    char m_pad[CACHE_LINE - sizeof(T) % CACHE_LINE];
};

} // namespace lf
