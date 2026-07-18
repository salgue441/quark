/**
 * @file memory/hazard_ptr.hpp
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
 * - Per-(thread, domain) retire lists with automatic scanning when full
 * - Global slot table with per-thread hazard slots
 * - Type-erased node deletion via move_only_function reclaimers
 * - RAII guard for automatic slot management
 * - Cache-line aligned records to prevent false sharing
 * - Double-flush + orphan handoff on thread exit to avoid leaks / UAF
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/memory/hazard_ptr.hpp>
 *
 * struct Node {
 *     int value;
 *     quark::AtomicTaggedPtr<Node> next;
 * };
 *
 * Node* pop(quark::AtomicTaggedPtr<Node>& head, quark::HazardDomain& domain) {
 *     auto& handle = quark::thread_handle(domain);
 *     int slot = 0;
 *
 *     while (true) {
 *         quark::HazardGuard guard(handle.record(), slot);
 *
 *         auto expected = head.load(std::memory_order_acquire);
 *         guard.protect(expected.ptr());
 *
 *         if (head.load(std::memory_order_acquire) != expected) {
 *             continue;
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
 *             domain.retire(node);
 *             return node;
 *         }
 *     }
 * }
 * @endcode
 */

#pragma once

#include <quark/core/arch.hpp>
#include <quark/util/assert.hpp>
#include <quark/util/detail/move_only_function.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quark {

namespace detail {

/**
 * @brief Aborts after writing a fatal hazard-pointer diagnostic to stderr.
 *
 * Kept independent of the logging subsystem so reclamation stays usable on
 * toolchains where `std::print` / logging macros are incomplete.
 */
[[noreturn]] inline void hazard_fatal(std::string_view message) noexcept {
  std::fwrite(message.data(), 1, message.size(), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  std::abort();
}

} // namespace detail

/**
 * @brief Number of hazard pointers reserved per thread.
 *
 * Two slots cover typical queue operations (current node + successor).
 * Increase at compile time if a structure needs more simultaneous protections.
 */
inline constexpr std::size_t HP_PER_THREAD = 2;

/**
 * @brief Maximum concurrent threads that may hold hazard records.
 *
 * Sizes the global `HazardRecord` table. Exceeding this limit is fatal.
 */
inline constexpr std::size_t MAX_THREADS = 128;

/**
 * @brief Retire-list size that triggers an automatic scan.
 *
 * Rule of thumb: at least `MAX_THREADS * HP_PER_THREAD` plus slack so a
 * full hazard set cannot permanently stall reclamation.
 */
inline constexpr std::size_t MAX_RETIRE_COUNT = MAX_THREADS * HP_PER_THREAD * 2;

/**
 * @brief Per-thread record containing hazard slots.
 *
 * Cache-line aligned so adjacent threads do not false-share slot storage.
 * Slots are atomic to allow lock-free scans from other threads.
 */
struct alignas(CACHE_LINE) HazardRecord {
  std::array<std::atomic<void *>, HP_PER_THREAD>
      slots{}; ///< Published protected pointers
  std::atomic<bool> in_use{false}; ///< Whether a thread currently owns this record
  std::atomic<std::thread::id> owner_id{}; ///< Owning thread (debug aid)

  /**
   * @brief Initializes every hazard slot to nullptr.
   */
  HazardRecord() {
    for (auto &s : slots)
      s.store(nullptr, std::memory_order_relaxed);
  }

  HazardRecord(const HazardRecord &) = delete;
  HazardRecord &operator=(const HazardRecord &) = delete;
};

/**
 * @brief Type-erased retired pointer with a portable reclaimer.
 *
 * Stores the raw address in @ref ptr for hazard scanning and a
 * @ref quark::detail::move_only_function polyfill thunk invoked on reclaim.
 * The reclaimer runs exactly once via @ref reclaim.
 */
struct RetiredNode {
  void *ptr = nullptr;                              ///< Address pending reclamation
  detail::move_only_function<void()> reclaim_fn; ///< Reclaim thunk (polyfill)

  RetiredNode() = default;

  /**
   * @brief Builds a retired node with a scan address and reclaim thunk.
   * @param p Non-owning pointer compared against hazard slots during scan
   * @param fn Callable invoked exactly once when the node is reclaimed
   */
  explicit RetiredNode(void *p, detail::move_only_function<void()> fn)
      : ptr(p), reclaim_fn(std::move(fn)) {}

  RetiredNode(RetiredNode &&other) noexcept
      : ptr(other.ptr), reclaim_fn(std::move(other.reclaim_fn)) {
    other.ptr = nullptr;
  }

  RetiredNode &operator=(RetiredNode &&other) noexcept {
    if (this != &other) {
      reclaim();
      ptr = other.ptr;
      reclaim_fn = std::move(other.reclaim_fn);
      other.ptr = nullptr;
    }
    return *this;
  }
  RetiredNode(const RetiredNode &) = delete;
  RetiredNode &operator=(const RetiredNode &) = delete;

  /**
   * @brief Invokes the reclaimer once and clears the node.
   *
   * Safe to call repeatedly; subsequent calls are no-ops.
   */
  void reclaim() noexcept {
    if (reclaim_fn) {
      reclaim_fn();
      reclaim_fn.reset();
      ptr = nullptr;
    }
  }
};

class HazardDomain;

/**
 * @brief RAII handle owning one hazard record and this thread's retire list.
 *
 * One handle exists per `(thread, domain)` pair via @ref thread_handle.
 * Destruction double-flushes the retire list and hands remaining survivors
 * to the domain orphan list.
 */
class ThreadHazardHandle {
public:
  /**
   * @brief Acquires a hazard record from `domain`.
   * @param domain Domain that owns the global slot table
   */
  explicit ThreadHazardHandle(HazardDomain &domain);

  ThreadHazardHandle(const ThreadHazardHandle &) = delete;
  ThreadHazardHandle &operator=(const ThreadHazardHandle &) = delete;

  /**
   * @brief Flushes retirees, releases the record, then orphans survivors.
   */
  ~ThreadHazardHandle();

  /**
   * @brief Returns the thread's hazard record for this domain.
   */
  [[nodiscard]] HazardRecord *record() const noexcept {
    check_epoch();
    return m_record;
  }

  /**
   * @brief Queues `ptr` for reclamation on this thread's retire list.
   * @tparam T Dynamic type of the retired object
   * @param ptr Pointer no longer reachable from the data structure
   *
   * @note A null pointer is ignored. Triggers @ref flush when the list
   *       exceeds @ref MAX_RETIRE_COUNT.
   */
  template <typename T> void retire(T *ptr) {
    check_epoch();
    if (ptr == nullptr)
      return;
    retire_impl(ptr, [ptr] { delete ptr; });
  }

  /**
   * @brief Queues `ptr` with a custom reclaimer on this thread's retire list.
   * @tparam T Dynamic type of the retired object
   * @tparam F Callable invoked as `deleter(ptr)` on reclaim
   * @param ptr Pointer no longer reachable from the data structure
   * @param deleter Custom reclaim function (pool return, arena free, etc.)
   *
   * @note A null pointer is ignored. Triggers @ref flush when the list
   *       exceeds @ref MAX_RETIRE_COUNT.
   */
  template <typename T, typename F> void retire(T *ptr, F &&deleter) {
    check_epoch();
    if (ptr == nullptr)
      return;
    retire_impl(ptr, [ptr, d = std::forward<F>(deleter)]() mutable { d(ptr); });
  }

  /**
   * @brief Scans this thread's retire list for the bound domain.
   */
  void flush();

private:
  void check_epoch() const;

  void retire_impl(void *ptr, detail::move_only_function<void()> fn) {
    m_retire_list.push_back(RetiredNode(ptr, std::move(fn)));
    if (m_retire_list.size() >= MAX_RETIRE_COUNT)
      flush();
  }

  HazardDomain &m_domain;                  ///< Bound reclamation domain
  HazardRecord *m_record;                  ///< Acquired hazard record
  std::uint64_t m_epoch;                   ///< Domain epoch at acquire time
  std::vector<RetiredNode> m_retire_list; ///< Per-handle retirees
};

/**
 * @brief Global registry managing hazard pointers and retired nodes.
 *
 * Non-copyable / non-movable so record addresses remain stable for the
 * lifetime of published hazard slots.
 *
 * @warning Destroy the domain only after all threads have released their
 *          @ref ThreadHazardHandle instances.
 */
class HazardDomain {
public:
  HazardDomain() = default;

  HazardDomain(const HazardDomain &) = delete;
  HazardDomain &operator=(const HazardDomain &) = delete;

  /**
   * @brief Reclaims orphaned nodes after asserting no records remain in use.
   *
   * @warning Aborts if any @ref ThreadHazardHandle is still live.
   */
  ~HazardDomain() {
    if (m_live_handles.load(std::memory_order_acquire) != 0) {
      detail::hazard_fatal(
          "HazardDomain destroyed with live ThreadHazardHandle(s)");
    }
    m_epoch.fetch_add(1, std::memory_order_acq_rel);

    for (auto &rec : m_records) {
      const bool busy = rec.in_use.load(std::memory_order_acquire);
      QUARK_ASSERT(!busy);
      (void)busy;
    }

    std::vector<RetiredNode> leftover;
    {
      std::lock_guard lock(m_orphan_mutex);
      leftover = std::move(m_orphans);
      m_orphans.clear();
    }

    // No live hazard records remain, so every orphan is safe to reclaim.
    for (auto &node : leftover)
      node.reclaim();
  }

  /**
   * @brief Retires a pointer onto the calling thread's list for this domain.
   * @tparam T Dynamic type of the retired object
   * @param ptr Pointer to retire (null ignored by the handle)
   */
  template <typename T> void retire(T *ptr);

  /**
   * @brief Retires a pointer with a custom reclaimer for this domain.
   * @tparam T Dynamic type of the retired object
   * @tparam F Callable invoked as `deleter(ptr)` on reclaim
   * @param ptr Pointer to retire (null ignored by the handle)
   * @param deleter Custom reclaim function
   */
  template <typename T, typename F> void retire(T *ptr, F &&deleter);

  /**
   * @brief Forces a scan of the calling thread's retire list for this domain.
   */
  void flush();

  /** @brief Monotonic epoch; bumped when the domain is destroyed. */
  [[nodiscard]] std::uint64_t epoch() const noexcept {
    return m_epoch.load(std::memory_order_acquire);
  }

  /** @brief Number of live @ref ThreadHazardHandle instances for this domain. */
  [[nodiscard]] std::uint64_t live_handles() const noexcept {
    return m_live_handles.load(std::memory_order_acquire);
  }

  /** @brief Increments the live-handle count (called by handle construction). */
  void on_handle_acquired() noexcept {
    m_live_handles.fetch_add(1, std::memory_order_acq_rel);
  }

  /** @brief Decrements the live-handle count (called by handle destruction). */
  void on_handle_released() noexcept {
    m_live_handles.fetch_sub(1, std::memory_order_acq_rel);
  }

  /**
   * @brief Scans a retire list and reclaims nodes not present in any hazard
   *        slot. Merges any orphaned nodes left by exited threads first.
   *
   * @param retire_list In/out list of pending retirees for one thread
   *
   * @note Complexity is O(threads × HP_PER_THREAD + retire_list.size()).
   */
  void scan(std::vector<RetiredNode> &retire_list) {
    {
      std::lock_guard lock(m_orphan_mutex);
      if (!m_orphans.empty()) {
        retire_list.insert(retire_list.end(),
                           std::make_move_iterator(m_orphans.begin()),
                           std::make_move_iterator(m_orphans.end()));
        m_orphans.clear();
      }
    }

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
    std::vector<RetiredNode> to_reclaim;
    survivors.reserve(retire_list.size());

    const auto freed_before = retire_list.size();

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
        to_reclaim.push_back(std::move(node));
      }
    }

    retire_list = std::move(survivors);

    const auto freed = freed_before - retire_list.size();
    (void)freed;

    for (auto &node : to_reclaim)
      node.reclaim();
  }

private:
  friend class ThreadHazardHandle;
  friend ThreadHazardHandle &thread_handle(HazardDomain &);

  /**
   * @brief Claims an unused hazard record for the calling thread.
   * @return Pointer to the acquired record
   * @warning Aborts if @ref MAX_THREADS is exhausted
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
    }

    detail::hazard_fatal(
        std::format("HazardDomain: MAX_THREADS ({}) exhausted", MAX_THREADS));
  }

  /**
   * @brief Clears slots and returns a record to the free pool.
   * @param rec Record previously returned by @ref acquire_record
   */
  void release_record(HazardRecord *rec) noexcept {
    for (auto &s : rec->slots)
      s.store(nullptr, std::memory_order_release);

    rec->owner_id.store(std::thread::id{}, std::memory_order_relaxed);
    rec->in_use.store(false, std::memory_order_release);
  }

  /**
   * @brief Appends retirees that outlived their retiring thread.
   * @param nodes Survivors still protected by other threads' hazards
   */
  void adopt_orphans(std::vector<RetiredNode> nodes) {
    if (nodes.empty())
      return;
    std::lock_guard lock(m_orphan_mutex);
    m_orphans.insert(m_orphans.end(), std::make_move_iterator(nodes.begin()),
                     std::make_move_iterator(nodes.end()));
  }

  std::array<HazardRecord, MAX_THREADS> m_records{}; ///< Global hazard table
  std::mutex m_orphan_mutex;           ///< Guards @ref m_orphans
  std::vector<RetiredNode> m_orphans; ///< Cross-thread deferred retirees
  std::atomic<std::uint64_t> m_epoch{1}; ///< Bumped on domain destruction
  std::atomic<std::uint64_t> m_live_handles{0}; ///< Active thread handles
};

inline void ThreadHazardHandle::check_epoch() const {
  if (m_epoch != m_domain.epoch())
    detail::hazard_fatal("ThreadHazardHandle: domain epoch mismatch");
}

inline ThreadHazardHandle::ThreadHazardHandle(HazardDomain &domain)
    : m_domain(domain), m_record(domain.acquire_record()),
      m_epoch(domain.epoch()) {
  domain.on_handle_acquired();
  QUARK_ASSERT(m_record != nullptr);
}

inline ThreadHazardHandle::~ThreadHazardHandle() {
  try {
    check_epoch();
    // Flush while our slots may still publish protections, clear slots, then
    // flush again so nodes we were protecting become reclaimable.
    m_domain.scan(m_retire_list);
    m_domain.release_record(m_record);
    m_domain.scan(m_retire_list);

    // Survivors are still protected by other threads — hand off to the domain
    // so a later scan (or domain destruction) can reclaim them.
    m_domain.adopt_orphans(std::move(m_retire_list));
  } catch (...) { // NOLINT(bugprone-empty-catch) destructor must not throw
  }
  m_domain.on_handle_released();
}

inline void ThreadHazardHandle::flush() {
  check_epoch();
  m_domain.scan(m_retire_list);
}

/**
 * @brief RAII guard for a single hazard slot.
 *
 * Publishes a pointer so concurrent scanners will not reclaim it. Clears the
 * slot automatically on destruction. A thread must already hold a
 * @ref ThreadHazardHandle / @ref HazardRecord before constructing a guard.
 *
 * @code
 * auto& handle = quark::thread_handle(domain);
 * {
 *     quark::HazardGuard guard(handle.record(), 0);
 *     guard.protect(node);
 *     // ... safe access ...
 * }
 * @endcode
 */
class HazardGuard {
public:
  /**
   * @brief Binds the guard to `record->slots[slot_index]`.
   * @param record Thread-local hazard record
   * @param slot_index Index in `[0, HP_PER_THREAD)`
   */
  HazardGuard(HazardRecord *record, std::size_t slot_index) noexcept
      : m_slot(record->slots[slot_index]) {
    QUARK_ASSERT(record != nullptr);
    QUARK_ASSERT(slot_index < HP_PER_THREAD);
  }

  /**
   * @brief Compatibility overload; the domain argument is unused.
   * @param record Thread-local hazard record
   * @param slot_index Index in `[0, HP_PER_THREAD)`
   */
  HazardGuard(HazardDomain &, HazardRecord *record,
              std::size_t slot_index) noexcept
      : HazardGuard(record, slot_index) {}

  HazardGuard(const HazardGuard &) = delete;
  HazardGuard &operator=(const HazardGuard &) = delete;

  /** @brief Clears the bound hazard slot. */
  ~HazardGuard() noexcept { clear(); }

  /**
   * @brief Publishes `ptr` into the hazard slot (release).
   * @param ptr Address to protect; may be nullptr
   */
  void protect(void *ptr) noexcept {
    m_slot.store(ptr, std::memory_order_release);
  }

  /**
   * @brief Typed convenience overload of @ref protect(void*).
   * @tparam T Pointee type
   * @param ptr Address to protect
   */
  template <typename T> void protect(T *ptr) noexcept {
    protect(static_cast<void *>(ptr));
  }

  /** @brief Stores nullptr into the slot (release). */
  void clear() noexcept { m_slot.store(nullptr, std::memory_order_release); }

private:
  std::atomic<void *> &m_slot; ///< Bound hazard slot
};

/**
 * @brief Returns the process-wide default hazard domain.
 * @return Reference to a function-local static domain
 */
[[nodiscard]] inline HazardDomain &default_domain() {
  static HazardDomain domain;
  return domain;
}

/**
 * @brief Thread-local map of domain → handle for the calling thread.
 */
inline auto &thread_handle_map() {
  thread_local std::unordered_map<HazardDomain *,
                                  std::unique_ptr<ThreadHazardHandle>>
      handles;
  return handles;
}

/**
 * @brief Returns the calling thread's handle for a domain.
 *
 * Handles are keyed by domain address so one thread may use multiple domains.
 *
 * @param domain Domain to bind (defaults to @ref default_domain)
 * @return Reference to the thread-local handle
 */
inline ThreadHazardHandle &
thread_handle(HazardDomain &domain = default_domain()) {
  auto &handles = thread_handle_map();
  if (auto it = handles.find(&domain); it != handles.end() && it->second)
    return *it->second;

  auto [it, inserted] =
      handles.emplace(&domain, std::make_unique<ThreadHazardHandle>(domain));
  (void)inserted;
  return *it->second;
}

/**
 * @brief Destroys this thread's handle for domain (required before destroying a
 *        non-static domain).
 */
inline void release_thread_handle(HazardDomain &domain) {
  thread_handle_map().erase(&domain);
}

template <typename T> void HazardDomain::retire(T *ptr) {
  thread_handle(*this).retire(ptr);
}

template <typename T, typename F>
void HazardDomain::retire(T *ptr, F &&deleter) {
  thread_handle(*this).retire(ptr, std::forward<F>(deleter));
}

inline void HazardDomain::flush() { thread_handle(*this).flush(); }

} // namespace quark
