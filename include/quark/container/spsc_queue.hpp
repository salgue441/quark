/**
 * @file container/spsc_queue.hpp
 * @brief Single-producer / single-consumer bounded ring queue.
 *
 * Classic empty/full distinction: a ring of `capacity` slots holds at most
 * `capacity - 1` live elements. No hazard pointers — the producer and
 * consumer alone own slot lifetimes.
 *
 * @author Carlos Salguero
 * @date 2026-07-22
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/container/spsc_queue.hpp>
 *
 * quark::SpscQueue<int> q(8);
 * q.try_push(1);
 * int v = 0;
 * q.try_pop(v);
 * @endcode
 *
 * @see HeapRingStorage
 * @see Backoff
 */

#pragma once

#include <quark/container/detail/ring_storage.hpp>
#include <quark/core/arch.hpp>
#include <quark/core/error.hpp>
#include <quark/util/assert.hpp>
#include <quark/util/backoff.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace quark {

/**
 * @brief Bounded SPSC queue over a pluggable ring storage backend.
 *
 * @tparam T Move-constructible element type
 * @tparam Storage Ring backend (default @ref HeapRingStorage)
 *
 * @warning Concurrent use requires exactly one producer thread and one
 *          consumer thread.
 */
template <typename T, typename Storage = HeapRingStorage<T>>
  requires RingStorage<Storage>
class SpscQueue {
public:
  static_assert(std::is_move_constructible_v<T>,
                "SpscQueue requires move-constructible T");

  /**
   * @brief Builds a queue with `capacity` slots (usable depth = capacity − 1).
   * @param capacity Must be >= 2
   * @throws std::invalid_argument if capacity < 2
   */
  explicit SpscQueue(std::size_t capacity) : m_storage(capacity) {
    if (capacity < 2)
      throw std::invalid_argument("SpscQueue capacity must be >= 2");
    m_head.value.store(0, std::memory_order_relaxed);
    m_tail.value.store(0, std::memory_order_relaxed);
  }

  SpscQueue(const SpscQueue &) = delete;
  SpscQueue &operator=(const SpscQueue &) = delete;
  SpscQueue(SpscQueue &&) = delete;
  SpscQueue &operator=(SpscQueue &&) = delete;

  ~SpscQueue() {
    auto head = m_head.value.load(std::memory_order_relaxed);
    auto tail = m_tail.value.load(std::memory_order_relaxed);
    while (tail != head) {
      std::destroy_at(static_cast<T *>(m_storage.slot(tail)));
      tail = next_index(tail);
    }
  }

  /** @brief Total slot count (not the maximum live element count). */
  [[nodiscard]] std::size_t capacity() const noexcept {
    return m_storage.size();
  }

  /**
   * @brief Approximate live element count (relaxed; not for synchronization).
   */
  [[nodiscard]] std::size_t size_approx() const noexcept {
    const auto head = m_head.value.load(std::memory_order_relaxed);
    const auto tail = m_tail.value.load(std::memory_order_relaxed);
    const auto cap = m_storage.size();
    return (head + cap - tail) % cap;
  }

  /**
   * @brief Attempts to enqueue by moving `value`.
   * @return false if the queue is full
   */
  [[nodiscard]] bool try_push(T &&value) {
    return try_push_impl(std::move(value));
  }

  /**
   * @brief Attempts to enqueue by copying `value`.
   * @return false if the queue is full
   */
  [[nodiscard]] bool try_push(const T &value)
    requires std::is_copy_constructible_v<T>
  {
    return try_push_impl(value);
  }

  /**
   * @brief Attempts to dequeue into `out`.
   * @return false if the queue is empty
   */
  [[nodiscard]] bool try_pop(T &out) {
    const auto tail = m_tail.value.load(std::memory_order_relaxed);
    if (tail == m_head.value.load(std::memory_order_acquire))
      return false;

    auto *addr = static_cast<T *>(m_storage.slot(tail));
    out = std::move(*addr);
    addr->~T();
    m_tail.value.store(next_index(tail), std::memory_order_release);
    return true;
  }

  /** @brief Non-blocking push returning @ref VoidResult. */
  [[nodiscard]] VoidResult push(T &&value) {
    if (try_push(std::move(value)))
      return Ok();
    return Err(Error::QueueFull);
  }

  /** @brief Non-blocking copy-push returning @ref VoidResult. */
  [[nodiscard]] VoidResult push(const T &value)
    requires std::is_copy_constructible_v<T>
  {
    if (try_push(value))
      return Ok();
    return Err(Error::QueueFull);
  }

  /** @brief Non-blocking pop returning the value or @ref Error::QueueEmpty. */
  [[nodiscard]] Result<T> pop() {
    T out;
    if (try_pop(out))
      return Ok(std::move(out));
    return Err<T>(Error::QueueEmpty);
  }

  /** @brief Spins with @ref Backoff until @ref try_push succeeds. */
  void push_wait(T &&value) {
    Backoff backoff;
    while (!try_push(std::move(value)))
      backoff.pause();
  }

  /** @brief Spins with @ref Backoff until copy-push succeeds. */
  void push_wait(const T &value)
    requires std::is_copy_constructible_v<T>
  {
    Backoff backoff;
    while (!try_push(value))
      backoff.pause();
  }

  /** @brief Spins with @ref Backoff until @ref try_pop succeeds. */
  void pop_wait(T &out) {
    Backoff backoff;
    while (!try_pop(out))
      backoff.pause();
  }

private:
  template <typename U>
  [[nodiscard]] bool try_push_impl(U &&value) {
    const auto head = m_head.value.load(std::memory_order_relaxed);
    const auto next = next_index(head);
    if (next == m_tail.value.load(std::memory_order_acquire))
      return false;

    ::new (m_storage.slot(head)) T(std::forward<U>(value));
    m_head.value.store(next, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t next_index(std::size_t index) const noexcept {
    const auto cap = m_storage.size();
    const auto next = index + 1;
    return next == cap ? 0 : next;
  }

  Storage m_storage;
  CacheAligned<std::atomic<std::size_t>> m_head{};
  CacheAligned<std::atomic<std::size_t>> m_tail{};
};

} // namespace quark
