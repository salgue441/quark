/**
 * @file container/ms_queue.hpp
 * @brief Unbounded Michael-Scott MPMC queue with hazard-pointer reclamation.
 *
 * Classic MS (1996) linked-list queue: a sentinel node, tagged head/tail
 * pointers, and hazard protection on dequeue. Domains may be owned by the
 * queue or borrowed from the caller.
 *
 * @author Carlos Salguero
 * @date 2026-07-22
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/container/ms_queue.hpp>
 *
 * quark::MsQueue<int> q;
 * q.try_push(1);
 * int v = 0;
 * q.try_pop(v); // false when empty
 * @endcode
 *
 * @see HazardDomain
 * @see TaggedPtr
 * @see Backoff
 */

#pragma once

#include <quark/core/arch.hpp>
#include <quark/core/error.hpp>
#include <quark/memory/hazard_ptr.hpp>
#include <quark/memory/tagged_ptr.hpp>
#include <quark/util/assert.hpp>
#include <quark/util/backoff.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace quark {

/**
 * @brief Unbounded lock-free MPMC queue (Michael & Scott).
 *
 * @tparam T Move-constructible element type
 *
 * @warning Concurrent use requires a live @ref HazardDomain (owned or
 *          borrowed) for the lifetime of all workers.
 */
template <typename T> class MsQueue {
public:
  static_assert(std::is_move_constructible_v<T>,
                "MsQueue requires move-constructible T");

  /**
   * @brief Constructs a queue that owns its @ref HazardDomain.
   */
  MsQueue()
      : m_owned_domain(std::make_unique<HazardDomain>()),
        m_domain(m_owned_domain.get()) {
    init_sentinel();
  }

  /**
   * @brief Constructs a queue that borrows `domain`.
   * @param domain Must outlive the queue and all worker threads that use it
   */
  explicit MsQueue(HazardDomain &domain) : m_domain(&domain) {
    init_sentinel();
  }

  MsQueue(const MsQueue &) = delete;
  MsQueue &operator=(const MsQueue &) = delete;
  MsQueue(MsQueue &&) = delete;
  MsQueue &operator=(MsQueue &&) = delete;

  ~MsQueue() {
    auto head = m_head.value.load(std::memory_order_relaxed);
    delete head.ptr();

    if (m_owned_domain) {
      release_thread_handle(*m_domain);
      m_owned_domain.reset();
    }
  }

  /**
   * @brief Attempts to enqueue by moving `value`.
   * @return false if node allocation fails
   */
  [[nodiscard]] bool try_push(T &&value) {
    return try_push_impl(std::move(value));
  }

  /**
   * @brief Attempts to enqueue by copying `value`.
   * @return false if node allocation fails
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
    auto &handle = thread_handle(*m_domain);
    Backoff backoff;

    for (;;) {
      HazardGuard guard_head(handle.record(), 0);

      auto head = m_head.value.load(std::memory_order_acquire);
      guard_head.protect(head.ptr());

      if (m_head.value.load(std::memory_order_acquire) != head) {
        backoff.pause();
        continue;
      }

      auto tail = m_tail.value.load(std::memory_order_acquire);
      QUARK_ASSERT(head.ptr() != nullptr);
      auto next = head.ptr()->next.load(std::memory_order_acquire);

      if (m_head.value.load(std::memory_order_acquire) != head) {
        backoff.pause();
        continue;
      }

      if (head.ptr() == tail.ptr()) {
        if (next.ptr() == nullptr)
          return false;

        // Lagging tail: help swing it forward.
        m_tail.value.compare_exchange_weak(tail, tail.next(next.ptr()),
                                           std::memory_order_release,
                                           std::memory_order_acquire);
        backoff.pause();
        continue;
      }

      if (next.ptr() == nullptr) {
        backoff.pause();
        continue;
      }

      // Protect the successor before using it (slot 1; head stays in slot 0).
      HazardGuard guard_next(handle.record(), 1);
      guard_next.protect(next.ptr());

      if (m_head.value.load(std::memory_order_acquire) != head) {
        backoff.pause();
        continue;
      }

      Node *const old_head = head.ptr();
      Node *const next_node = next.ptr();

      // Swing head to next; only then is this thread the exclusive owner of
      // the value in next_node (which becomes the new sentinel).
      if (!m_head.value.compare_exchange_weak(head, head.next(next_node),
                                              std::memory_order_release,
                                              std::memory_order_acquire)) {
        backoff.pause();
        continue;
      }

      T *addr = std::launder(reinterpret_cast<T *>(next_node->storage));
      out = std::move(*addr);
      std::destroy_at(addr);
      m_domain->retire(old_head);
      return true;
    }
  }

  /** @brief Non-blocking push returning @ref VoidResult. */
  [[nodiscard]] VoidResult push(T &&value) {
    if (try_push(std::move(value)))
      return Ok();
    return Err(Error::AllocationFailed);
  }

  /** @brief Non-blocking copy-push returning @ref VoidResult. */
  [[nodiscard]] VoidResult push(const T &value)
    requires std::is_copy_constructible_v<T>
  {
    if (try_push(value))
      return Ok();
    return Err(Error::AllocationFailed);
  }

  /** @brief Non-blocking pop returning the value or @ref Error::QueueEmpty. */
  [[nodiscard]] Result<T> pop() {
    T out;
    if (try_pop(out))
      return Ok(std::move(out));
    return Err<T>(Error::QueueEmpty);
  }

private:
  struct Node {
    AtomicTaggedPtr<Node> next{};
    alignas(T) std::byte storage[sizeof(T)]{};
  };

  template <typename U>
  [[nodiscard]] bool try_push_impl(U &&value) {
    auto *node = new (std::nothrow) Node;
    if (node == nullptr)
      return false;

    ::new (static_cast<void *>(node->storage)) T(std::forward<U>(value));

    auto &handle = thread_handle(*m_domain);
    Backoff backoff;
    for (;;) {
      HazardGuard guard(handle.record(), 0);

      auto tail = m_tail.value.load(std::memory_order_acquire);
      QUARK_ASSERT(tail.ptr() != nullptr);
      guard.protect(tail.ptr());

      if (m_tail.value.load(std::memory_order_acquire) != tail) {
        backoff.pause();
        continue;
      }

      auto next = tail.ptr()->next.load(std::memory_order_acquire);

      if (next.ptr() == nullptr) {
        if (tail.ptr()->next.compare_exchange_weak(next, next.next(node),
                                                    std::memory_order_release,
                                                    std::memory_order_acquire)) {
          // Help swing tail to the new node (may already have been helped).
          m_tail.value.compare_exchange_weak(tail, tail.next(node),
                                             std::memory_order_release,
                                             std::memory_order_acquire);
          return true;
        }
      } else {
        m_tail.value.compare_exchange_weak(tail, tail.next(next.ptr()),
                                           std::memory_order_release,
                                           std::memory_order_acquire);
      }
      backoff.pause();
    }
  }

  void init_sentinel() {
    auto *sentinel = new Node();
    const TaggedPtr<Node> init(sentinel, 0);
    m_head.value.store(init, std::memory_order_relaxed);
    m_tail.value.store(init, std::memory_order_relaxed);
  }

  std::unique_ptr<HazardDomain> m_owned_domain;
  HazardDomain *m_domain = nullptr;

  CacheAligned<AtomicTaggedPtr<Node>> m_head{};
  CacheAligned<AtomicTaggedPtr<Node>> m_tail{};
};

} // namespace quark
