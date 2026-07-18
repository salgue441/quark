/**
 * @file hazard_pointer.hpp
 * @brief Hazard-pointer-based safe memory reclamation for lock-free data
 * structures.
 *
 * This header provides a complete implementation of the hazard pointer
 * technique for safe memory reclamation in lock-free data structures. It solves
 * the use-after-free problem where a thread may dereference a node pointer that
 * has been removed and deleted by another thread.
 *
 * The implementation follows Michael's 2004 algorithm with the following key
 * features:
 * - Thread-local retire lists with automatic scanning when full
 * - Global slot table with per-thread hazard slots
 * - Type-erased node deletion for generic use
 * - RAII guard for automatic slot management
 * - Cache-line aligned records to prevent false sharing
 *
 * @author Carlos Salguero
 * @date 2026-07-17
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/hazard_pointer.hpp>
 *
 * // Node in a lock-free stack
 * struct Node {
 *     int value;
 *     quark::AtomicTaggedPtr<Node> next;
 * };
 *
 * // Pop operation with hazard pointer protection
 * Node* pop(quark::AtomicTaggedPtr<Node>& head, quark::HazardDomain& domain) {
 *     auto handle = quark::thread_handle(domain);
 *     int slot = 0;
 *
 *     while (true) {
 *         quark::HazardGuard guard(domain, handle.record(), slot);
 *
 *         auto expected = head.load(std::memory_order_acquire);
 *         guard.protect(expected.ptr());  // Publish pointer
 *
 *         // Re-validate: head may have changed
 *         if (head.load(std::memory_order_acquire) != expected) {
 *             continue;  // Retry with new value
 *         }
 *
 *         Node* node = expected.ptr();
 *         if (node == nullptr) return nullptr;
 *
 *         auto next = node->next.load(std::memory_order_acquire);
 *         auto desired = expected.next(next.ptr());
 *
 *         if (head.compare_exchange_strong(expected, desired,
 *                                          std::memory_order_acquire)) {
 *             // We own node, no one else can access it
 *             domain.retire(node);  // Queue for deletion
 *             return node;
 *         }
 *     }
 * }
 * @endcode
 */

#pragma once

#include "arch.hpp"
#include "error.hpp"
#include "log.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace quark {
/**
 * @brief Number of hazard pointers per thread.
 *
 * Each thread owns this many hazard slots. The default of 2 is sufficient
 * for most queue operations (head pointer and next pointer). Hash-map resize
 * operations might need more; tune per use-case.
 *
 * @note This is a compile-time constant to enable fixed-size arrays.
 */
inline constexpr std::size_t HP_PER_THREAD = 2;

/**
 * @brief Maximum number of threads that can simultaneously hold hazard slots.
 *
 * This limits the size of the global slot table. The default of 128 threads
 * is sufficient for most applications.
 *
 * @warning If more threads try to acquire hazard records, a fatal error is
 * logged and the program aborts.
 */
inline constexpr std::size_t MAX_THREADS = 128;

/**
 * @brief Maximum size of the retire list before forcing a scan.
 *
 * When a thread's retire list grows beyond this threshold, a scan is triggered
 * to free as many nodes as possible. The rule of thumb is:
 * R ≥ (MAX_THREADS × HP_PER_THREAD) + some slack
 *
 * The default value ensures efficient memory reclamation while bounding
 * memory usage.
 */
inline constexpr std::size_t MAX_RETIRE_COUNT = MAX_THREADS * HP_PER_THREAD * 2;

/**
 * @brief Per-thread record containing hazard slots.
 *
 * Each thread owns one HazardRecord containing HP_PER_THREAD hazard slots.
 * The record is cache-line aligned to prevent false sharing between threads.
 * The slots are atomic to allow concurrent reads during global scans.
 */
struct aligned(CACHE_LINE) HazardRecord {
  /**
   * @brief The hazard slots.
   *
   * Each slot can hold a single pointer that a thread is currently protecting.
   * A release store is used when publishing a pointer, and acquire loads
   * are used during scans to ensure visibility.
   */
  std::array<std::atomic<void *>, HP_PER_THREAD> slots{};

  /**
   * @brief Ownership flag indicating if this record is in use.
   *
   * Set to true when a thread claims this record, false when released.
   * Uses atomic operations with acquire/release semantics for thread safety.
   */
  std::atomic<bool> in_use{false};

  /**
   * @brief ID of the thread that owns this record.
   *
   * Used for debugging purposes only. Stored atomically but not used in
   * the algorithm.
   */
  std::atomic<std::thread::id> owner_id{};

  /**
   * @brief Default constructor initializes all slots to nullptr.
   */
  HazardRecord() {
    for (auto &s : slots)
      s.store(nullptr, std::memory_order_relaxed);
  }
};

/**
 * @brief Type-erased retired pointer with its deleter.
 *
 * Stores a pointer and a function object that knows how to delete it.
 * This allows a single HazardDomain to manage pointers of different types
 * without requiring templating on the node type.
 */
struct RetiredNode {
  void *ptr = nullptr;           ///< The pointer to be deleted
  std::function<void()> deleter; ///< The deleter function
};

/**
 * @brief Global registry managing hazard pointers and retired nodes.
 *
 * The HazardDomain maintains the global table of hazard records and provides
 * operations for retiring nodes and scanning for safe deletion. Typically,
 * one instance is used per data structure type, or a single process-wide
 * instance can be shared across all structures.
 *
 * The domain is non-copyable and non-movable to ensure address stability
 * for pointers stored in hazard slots.
 *
 * @note All methods are thread-safe except where noted.
 */
class HazardDomain {
public:
  /**
   * @brief Default constructor.
   *
   * Initializes the global record table with all records marked as not in use.
   */
  HazardDomain() = default;

  // Non-copyable, non-movable — address stability required.
  HazardDomain(const HazardDomain &) = delete;
  HazardDomain &operator=(const HazardDomain &) = delete;

  /**
   * @brief Acquires a hazard record for the current thread.
   *
   * This method is called once per thread on first use. It scans the global
   * table for an unused record and claims it atomically.
   *
   * @return A pointer to the acquired HazardRecord
   *
   * @warning If MAX_THREADS is exceeded, a fatal error is logged and
   *          std::abort() is called. This should be treated as a programming
   *          error in tests.
   */
  HazardRecord *acquire_record() {
    for (auto &rec : m_records) {
      bool expected = false;
      if (rec.in_use.compare_exchange_strong(expected, true,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
        rec.owner_id.store(std::this_thread::get_id(),
                           std::memory_order_relaxed);

        return &rec;
      }

      QUARK_FATAL("HazardDomain: MAX_THREADS ({}) exhausted")
    }
  }

  /**
   * @brief Releases a hazard record when a thread exits.
   *
   * Clears all hazard slots, resets the owner ID, and marks the record
   * as not in use. Should be called from the thread destructor.
   *
   * @param rec The record to release
   */
  void release_record(HazardRecord *rec) {
    for (auto &s : rec->slots) {
      s.store(nullptr, std::memory_order_release);
    }

    rec->owner_id.store(std::thread::id{}, std::memory_order_relaxed);
    rec->in_use.store(false, std::memory_order_release);
  }

  /**
   * @brief Marks a pointer for eventual deletion.
   *
   * Adds a pointer to the current thread's retire list with a deleter that
   * knows the correct type. If the retire list grows beyond MAX_RETIRE_COUNT,
   * a scan is triggered to free safe nodes.
   *
   * @tparam T The type of the pointer (deduced)
   * @param ptr The pointer to retire
   *
   * @note The pointer should no longer be reachable from the data structure.
   *       It may be deferred if currently protected by a hazard pointer.
   */
  template <typename T> void retire(T *ptr) {
    auto &list = get_retire_list();
    list.push_back(RetiredNode{.ptr = ptr, .deleter = [ptr] { delete ptr; }});

    if (list.size() >= MAX_RETIRE_COUNT)
      scan(list);
  }

  /**
   * @brief Scans the retire list and frees safe nodes.
   *
   * This method collects all currently published hazard pointers from all
   * threads, then iterates through the retire list. Any node whose address
   * is not in the hazard set is safely deleted. Nodes that are still
   * protected remain in the retire list for a future scan.
   *
   * @param retire_list The retire list to scan (modified in-place)
   *
   * @note This operation is O(threads × HP_PER_THREAD + retire_list_size)
   *       and may be expensive. It is automatically triggered when retire
   *       lists grow too large.
   */
  void scan(std::vector<RetiredNode> &retire_list) {
    std::vector<void *> hazards;
    hazards.reserve(MAX_THREADS * HP_PER_THREAD);

    for (auto &rec : m_records) {
      if (!rec.in_use.load(std::memory_order_acquire))
        continue;
      for (auto &slot : rec.slots) {
        void *p = slot.load(std::memory_order_acquire);
        if (p != nullptr)
          hazards.push_back(p);
      }
    }

    std::vector<RetiredNode> survivors;
    survivors.reserve(retire_list.size());

    for (auto &node : retire_list) {
      bool hazardous = false;
      for (void *h : hazards) {
        if (h == node.ptr) {
          hazardous = true;
          break;
        }
      }

      if (hazardous) {
        survivors.push_back(std::move(node));
      } else {
        node.deleter();
      }
    }

    QUARK_DEBUG("HazardDomain::scan - freed {}, deferred {}",
                retire_list.size() - survivors.size(), survivors.size());

    retire_list = std::move(survivors);
  }

  /**
   * @brief Forces a scan of the current thread's retire list.
   *
   * Useful for ensuring all pending deletions are processed before thread exit.
   */
  void flush() { scan(get_retire_list()); }

private:
  // Per-thread retire list -- no synchronization needed.
  std::vector<RetiredNode> &get_retire_list() {
    thread_local std::vector<RetiredNode> retire_list;
    return retire_list;
  }

private:
  std::array<HazardRecord, MAX_THREADS> m_records{}; ///< Hazard global table.
};

/**
 * @brief RAII handle for thread-local hazard record acquisition.
 *
 * Acquires a HazardRecord on first use and releases it when the thread exits.
 * One instance exists per (thread, domain) pair via thread_local storage.
 *
 * This class automatically manages the lifecycle of a hazard record,
 * ensuring proper cleanup even in the presence of exceptions.
 */
class ThreadHazardHandle {
public:
  /**
   * @brief Constructs a handle for the given domain.
   *
   * Immediately acquires a hazard record from the domain.
   *
   * @param domain The hazard domain to use
   */
  explicit ThreadHazardHandle(HazardDomain &domain)
      : m_domain(domain), m_record(domain.acquire_record()) {}

  /**
   * @brief Destructor releases the hazard record.
   *
   * Flushes the retire list and releases the record back to the domain.
   */
  ~ThreadHazarHandle() {
    m_domain.flush();
    m_domain.release_record(m_record);
  }

  /**
   * @brief Returns the acquired hazard record.
   *
   * @return The record for this thread
   */
  HazardRecord *record() const noexcept { return m_record; }

private:
  HazardDomain &m_domain;
  HazardRecord *m_record;
};

/**
 * @brief RAII guard for a single hazard slot.
 *
 * This is the primary interface for protecting pointers. When a pointer is
 * loaded from a lock-free data structure, the guard protects it so that
 * it cannot be freed by another thread while it's being accessed.
 *
 * Typical usage pattern:
 * 1. Load a pointer from the data structure
 * 2. Protect it with the guard (publish to slot)
 * 3. Re-validate the pointer (it may have changed)
 * 4. If valid, safely dereference and use it
 * 5. If the pointer is removed from the structure, retire it
 *
 * The guard automatically clears the slot on destruction.
 *
 * @note A thread must have already acquired a HazardRecord before creating
 *       a HazardGuard.
 *
 * @code
 * auto handle = helium::thread_handle(domain);
 * {
 *     helium::HazardGuard guard(domain, handle.record(), 0);
 *     guard.protect(node_ptr);
 *     // ... safe access to node_ptr ...
 * } // slot automatically cleared
 * @endcode
 */
class HazardGuard {
public:
  /**
   * @brief Constructs a guard for a specific hazard slot.
   *
   * @param domain The hazard domain (reserved for future use)
   * @param record The thread's hazard record
   * @param slot_index The slot index within the record (0 to HP_PER_THREAD-1)
   */
  HazardGuard(HazardDomain &domain, HazardRecord *record,
              std::size_t slot_index) noexcept
      : m_slot(record->slots[slot_index]) {
    assert(slot_index < HP_PER_THREAD);
    (void)domain;
  }

  /**
   * @brief Publishes a pointer into the hazard slot.
   *
   * Stores the pointer with release semantics to ensure that any writes
   * to the pointed-to object are visible to other threads before the
   * pointer becomes visible. This pairs with acquire loads during scans.
   *
   * @param ptr The pointer to protect (may be nullptr)
   */
  void protect(void *ptr) noexcept {
    m_slot.store(ptr, std::memory_order_release);
  }

  /**
   * @brief Clears the hazard slot.
   *
   * Stores nullptr with release semantics. Called automatically on destruction.
   */
  void clear() noexcept { m_slot.store(nullptr, std::memory_order_release); }

  /**
   * @brief Destructor automatically clears the slot.
   */
  ~HazardGuard() noexcept { clear(); }

  // Non-copyable
  HazardGuard(const HazardGuard &) = delete;
  HazardGuard &operator=(const HazardGuard &) = delete;

private:
  std::atomic<void *> &m_slot; ///< Reference to the hazard slot
};

/**
 * @brief Returns the default process-wide hazard domain.
 *
 * For projects that only need one domain. Access via helium::default_domain().
 *
 * @return Reference to the singleton default domain
 */
inline HazardDomain &default_domain() {
  static HazardDomain domain;
  return domain;
}

/**
 * @brief Returns the calling thread's handle for a domain.
 *
 * Lazily creates a ThreadHazardHandle for the current thread and domain.
 * The handle is thread-local and persists for the lifetime of the thread.
 *
 * @param domain The hazard domain (defaults to default_domain())
 * @return Reference to the thread's handle
 */
inline ThreadHazardHandle &
thread_handle(HazardDomain &domain = default_domain()) {
  thread_local ThreadHazardHandle handle(domain);
  return handle;
}

} // namespace quark
