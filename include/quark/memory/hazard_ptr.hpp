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
 * - Type-erased node deletion via std::move_only_function
 * - RAII guard for automatic slot management
 * - Cache-line aligned records to prevent false sharing
 * - Double-flush + orphan handoff on thread exit to avoid leaks / UAF
 *
 * @author Carlos Salguero
 * @date 2026-07-17
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
#include <quark/util/log.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quark {

inline constexpr std::size_t HP_PER_THREAD = 2;
inline constexpr std::size_t MAX_THREADS = 128;
inline constexpr std::size_t MAX_RETIRE_COUNT = MAX_THREADS * HP_PER_THREAD * 2;

/**
 * @brief Per-thread record containing hazard slots.
 */
struct alignas(CACHE_LINE) HazardRecord {
  std::array<std::atomic<void *>, HP_PER_THREAD> slots{};
  std::atomic<bool> in_use{false};
  std::atomic<std::thread::id> owner_id{};

  HazardRecord() {
    for (auto &s : slots)
      s.store(nullptr, std::memory_order_relaxed);
  }

  HazardRecord(const HazardRecord &) = delete;
  HazardRecord &operator=(const HazardRecord &) = delete;
};

/**
 * @brief Type-erased retired pointer with a move-only deleter.
 *
 * The deleter is invoked exactly once via reclaim(). Destroying a
 * RetiredNode without reclaim() does not free the pointer — callers must
 * reclaim, transfer ownership, or intentionally leak survivors.
 */
struct RetiredNode {
  void *ptr = nullptr;
  std::move_only_function<void()> deleter;

  RetiredNode() = default;

  template <typename T>
  explicit RetiredNode(T *p)
      : ptr(p), deleter([p] { delete p; }) {}

  RetiredNode(RetiredNode &&) noexcept = default;
  RetiredNode &operator=(RetiredNode &&) noexcept = default;

  RetiredNode(const RetiredNode &) = delete;
  RetiredNode &operator=(const RetiredNode &) = delete;

  void reclaim() noexcept {
    if (deleter) {
      deleter();
      deleter = nullptr;
      ptr = nullptr;
    }
  }
};

class HazardDomain;

/**
 * @brief RAII handle owning one hazard record and this thread's retire list
 *        for a domain.
 */
class ThreadHazardHandle {
public:
  explicit ThreadHazardHandle(HazardDomain &domain);

  ThreadHazardHandle(const ThreadHazardHandle &) = delete;
  ThreadHazardHandle &operator=(const ThreadHazardHandle &) = delete;

  ~ThreadHazardHandle();

  [[nodiscard]] HazardRecord *record() const noexcept { return m_record; }

  template <typename T> void retire(T *ptr) {
    if (ptr == nullptr)
      return;

    m_retire_list.push_back(RetiredNode{ptr});
    if (m_retire_list.size() >= MAX_RETIRE_COUNT)
      flush();
  }

  void flush();

private:
  HazardDomain &m_domain;
  HazardRecord *m_record;
  std::vector<RetiredNode> m_retire_list;
};

/**
 * @brief Global registry managing hazard pointers and retired nodes.
 */
class HazardDomain {
public:
  HazardDomain() = default;

  HazardDomain(const HazardDomain &) = delete;
  HazardDomain &operator=(const HazardDomain &) = delete;

  ~HazardDomain() {
    for (auto &rec : m_records) {
      assert(!rec.in_use.load(std::memory_order_acquire) &&
             "HazardDomain destroyed while threads still hold records");
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
   */
  template <typename T> void retire(T *ptr);

  /**
   * @brief Forces a scan of the calling thread's retire list for this domain.
   */
  void flush();

  /**
   * @brief Scans a retire list and reclaims nodes not present in any hazard
   *        slot. Merges any orphaned nodes left by exited threads first.
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
    survivors.reserve(retire_list.size());

    const auto freed_before = retire_list.size();

    for (auto &node : retire_list) {
      if (std::ranges::contains(hazards, node.ptr)) {
        survivors.push_back(std::move(node));
      } else {
        node.reclaim();
      }
    }

    QUARK_DEBUG("HazardDomain::scan - freed {}, deferred {}",
                freed_before - survivors.size(), survivors.size());

    retire_list = std::move(survivors);
  }

private:
  friend class ThreadHazardHandle;
  friend ThreadHazardHandle &thread_handle(HazardDomain &);

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

    QUARK_FATAL("HazardDomain: MAX_THREADS ({}) exhausted", MAX_THREADS);
    std::unreachable();
  }

  void release_record(HazardRecord *rec) noexcept {
    for (auto &s : rec->slots)
      s.store(nullptr, std::memory_order_release);

    rec->owner_id.store(std::thread::id{}, std::memory_order_relaxed);
    rec->in_use.store(false, std::memory_order_release);
  }

  void adopt_orphans(std::vector<RetiredNode> nodes) {
    if (nodes.empty())
      return;
    std::lock_guard lock(m_orphan_mutex);
    m_orphans.insert(m_orphans.end(), std::make_move_iterator(nodes.begin()),
                     std::make_move_iterator(nodes.end()));
  }

  std::array<HazardRecord, MAX_THREADS> m_records{};
  std::mutex m_orphan_mutex;
  std::vector<RetiredNode> m_orphans;
};

inline ThreadHazardHandle::ThreadHazardHandle(HazardDomain &domain)
    : m_domain(domain), m_record(domain.acquire_record()) {}

inline ThreadHazardHandle::~ThreadHazardHandle() {
  // Flush while our slots may still publish protections, clear slots, then
  // flush again so nodes we were protecting become reclaimable.
  m_domain.scan(m_retire_list);
  m_domain.release_record(m_record);
  m_domain.scan(m_retire_list);

  // Survivors are still protected by other threads — hand off to the domain
  // so a later scan (or domain destruction) can reclaim them.
  m_domain.adopt_orphans(std::move(m_retire_list));
}

inline void ThreadHazardHandle::flush() { m_domain.scan(m_retire_list); }

/**
 * @brief RAII guard for a single hazard slot.
 */
class HazardGuard {
public:
  HazardGuard(HazardRecord *record, std::size_t slot_index) noexcept
      : m_slot(record->slots[slot_index]) {
    assert(slot_index < HP_PER_THREAD);
  }

  // Back-compat overload (domain unused).
  HazardGuard(HazardDomain &, HazardRecord *record,
              std::size_t slot_index) noexcept
      : HazardGuard(record, slot_index) {}

  HazardGuard(const HazardGuard &) = delete;
  HazardGuard &operator=(const HazardGuard &) = delete;

  ~HazardGuard() noexcept { clear(); }

  void protect(void *ptr) noexcept {
    m_slot.store(ptr, std::memory_order_release);
  }

  template <typename T> void protect(T *ptr) noexcept {
    protect(static_cast<void *>(ptr));
  }

  void clear() noexcept { m_slot.store(nullptr, std::memory_order_release); }

private:
  std::atomic<void *> &m_slot;
};

[[nodiscard]] inline HazardDomain &default_domain() {
  static HazardDomain domain;
  return domain;
}

/**
 * @brief Returns the calling thread's handle for a domain.
 */
inline ThreadHazardHandle &
thread_handle(HazardDomain &domain = default_domain()) {
  thread_local std::unordered_map<HazardDomain *,
                                  std::unique_ptr<ThreadHazardHandle>>
      handles;
  auto &slot = handles[&domain];
  if (!slot)
    slot = std::make_unique<ThreadHazardHandle>(domain);
  return *slot;
}

template <typename T> void HazardDomain::retire(T *ptr) {
  thread_handle(*this).retire(ptr);
}

inline void HazardDomain::flush() { thread_handle(*this).flush(); }

} // namespace quark
