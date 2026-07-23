/**
 * @file container/detail/ring_storage.hpp
 * @brief Ring-buffer storage backends for lock-free queues.
 *
 * Separates slot ownership from queue index protocols (Open/Closed).
 * v1 ships @ref HeapRingStorage; additional backends can satisfy the same
 * surface without changing @ref SpscQueue.
 *
 * @author Carlos Salguero
 * @date 2026-07-22
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <quark/util/assert.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace quark {

/**
 * @brief Concept for raw ring slot storage used by @ref SpscQueue.
 *
 * @tparam S Storage type
 *
 * Requirements:
 * - `size()` returns the number of slots
 * - `slot(i)` returns aligned raw storage for one element (uninitialized)
 */
template <typename S>
concept RingStorage = requires(S &s, const S &cs, std::size_t i) {
  { cs.size() } -> std::same_as<std::size_t>;
  { s.slot(i) } -> std::convertible_to<void *>;
};

/**
 * @brief Heap-backed ring of uninitialized slots for type `T`.
 *
 * @tparam T Element type; storage is `alignas(T)` bytes per slot
 */
template <typename T>
class HeapRingStorage {
public:
  static_assert(!std::is_reference_v<T>, "T must not be a reference");

  /**
   * @brief Allocates `capacity` uninitialized slots.
   * @param capacity Number of slots (must be >= 1)
   */
  explicit HeapRingStorage(std::size_t capacity)
      : m_capacity(capacity),
        m_bytes(capacity == 0
                    ? nullptr
                    : static_cast<std::byte *>(::operator new(
                          capacity * sizeof(T), std::align_val_t{alignof(T)}))) {
    QUARK_ASSERT(capacity >= 1);
  }

  HeapRingStorage(const HeapRingStorage &) = delete;
  HeapRingStorage &operator=(const HeapRingStorage &) = delete;

  HeapRingStorage(HeapRingStorage &&other) noexcept
      : m_capacity(other.m_capacity), m_bytes(other.m_bytes) {
    other.m_capacity = 0;
    other.m_bytes = nullptr;
  }

  HeapRingStorage &operator=(HeapRingStorage &&other) noexcept {
    if (this != &other) {
      destroy_buffer();
      m_capacity = other.m_capacity;
      m_bytes = other.m_bytes;
      other.m_capacity = 0;
      other.m_bytes = nullptr;
    }
    return *this;
  }

  ~HeapRingStorage() { destroy_buffer(); }

  /** @brief Number of slots in the ring. */
  [[nodiscard]] std::size_t size() const noexcept { return m_capacity; }

  /**
   * @brief Returns raw storage for slot `index`.
   * @param index Slot index in `[0, size())`
   */
  [[nodiscard]] void *slot(std::size_t index) noexcept {
    QUARK_ASSERT(index < m_capacity);
    return m_bytes + index * sizeof(T);
  }

  /** @copydoc slot(std::size_t) */
  [[nodiscard]] const void *slot(std::size_t index) const noexcept {
    QUARK_ASSERT(index < m_capacity);
    return m_bytes + index * sizeof(T);
  }

private:
  void destroy_buffer() noexcept {
    if (m_bytes != nullptr) {
      ::operator delete(m_bytes, std::align_val_t{alignof(T)});
      m_bytes = nullptr;
    }
  }

  std::size_t m_capacity = 0;
  std::byte *m_bytes = nullptr;
};

static_assert(RingStorage<HeapRingStorage<int>>);

} // namespace quark
