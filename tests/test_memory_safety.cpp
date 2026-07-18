/**
 * @file tests/test_memory_safety.cpp
 * @brief Catch2 unit and stress tests for TaggedPtr and HazardDomain.
 *
 * Covers construction/CAS/ABA behavior for tagged pointers and protect /
 * retire / flush semantics for hazard pointers, including concurrent and
 * seeded-RNG stress sections.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 */

#include <quark/memory/hazard_ptr.hpp>
#include <quark/memory/tagged_ptr.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

TEST_CASE("TaggedPtr construction", "[tagged_ptr]") {
  int x = 42;
  quark::TaggedPtr<int> tp(&x, 7);

  REQUIRE(tp.ptr() == &x);
  REQUIRE(tp.tag() == 7);
  REQUIRE(*tp == 42);
  REQUIRE(static_cast<bool>(tp) == true);
}

TEST_CASE("TaggedPtr null", "[tagged_ptr]") {
  quark::TaggedPtr<int> tp;
  REQUIRE(tp.ptr() == nullptr);
  REQUIRE(tp.tag() == 0);
  REQUIRE_FALSE(tp);
}

TEST_CASE("TaggedPtr next bumps version", "[tagged_ptr]") {
  int x = 1, y = 2;
  quark::TaggedPtr<int> tp(&x, 3);

  auto tp2 = tp.next(&y);
  REQUIRE(tp2.ptr() == &y);
  REQUIRE(tp2.tag() == 4);
  REQUIRE(tp != tp2);
}

TEST_CASE("TaggedPtr tag wrap", "[tagged_ptr]") {
  int x = 0;
  quark::TaggedPtr<int> tp(&x, 65535u);
  auto wrapped = tp.next(&x);
  REQUIRE(wrapped.tag() == 0);
}

TEST_CASE("TaggedPtr equality", "[tagged_ptr]") {
  int x = 0;
  quark::TaggedPtr<int> a(&x, 1);
  quark::TaggedPtr<int> b(&x, 1);
  quark::TaggedPtr<int> c(&x, 2);

  REQUIRE(a == b);
  REQUIRE(a != c);
}

TEST_CASE("AtomicTaggedPtr CAS", "[tagged_ptr]") {
  int x = 0, y = 0;
  quark::TaggedPtr<int> init(&x, 0);
  quark::AtomicTaggedPtr<int> atp(init);

  auto expected = atp.load(std::memory_order_acquire);
  auto desired = expected.next(&y);

  bool ok = atp.compare_exchange_strong(expected, desired,
                                        std::memory_order_release,
                                        std::memory_order_acquire);

  REQUIRE(ok);
  REQUIRE(atp.load().ptr() == &y);
  REQUIRE(atp.load().tag() == 1);
}

TEST_CASE("AtomicTaggedPtr ABA prevention", "[tagged_ptr]") {
  int x = 0;
  quark::TaggedPtr<int> v0(&x, 0);
  quark::AtomicTaggedPtr<int> atp(v0);

  auto a_snapshot = atp.load(std::memory_order_acquire);

  {
    auto cur = atp.load(std::memory_order_acquire);
    auto v1 = cur.next(&x);
    atp.compare_exchange_strong(cur, v1, std::memory_order_release,
                                std::memory_order_acquire);

    cur = atp.load(std::memory_order_acquire);
    auto v2 = cur.next(&x);
    atp.compare_exchange_strong(cur, v2, std::memory_order_release,
                                std::memory_order_acquire);
  }

  auto a_desired = a_snapshot.next(&x);
  bool ok = atp.compare_exchange_strong(a_snapshot, a_desired,
                                        std::memory_order_release,
                                        std::memory_order_acquire);

  REQUIRE_FALSE(ok);
  REQUIRE(atp.load().tag() == 2);
}

struct Node {
  int value;
  std::atomic<Node *> next{nullptr};

  explicit Node(int v) : value(v) {}
};

TEST_CASE("Hazard protect publishes and clears", "[hazard]") {
  auto &domain = quark::default_domain();
  auto &handle = quark::thread_handle(domain);
  auto *rec = handle.record();

  Node n(99);

  {
    quark::HazardGuard guard(domain, rec, 0);
    guard.protect(&n);

    void *published = rec->slots[0].load(std::memory_order_acquire);
    REQUIRE(published == static_cast<void *>(&n));
  }

  void *after = rec->slots[0].load(std::memory_order_acquire);
  REQUIRE(after == nullptr);
}

TEST_CASE("Hazard flush reclaims unprotected", "[hazard]") {
  auto &domain = quark::default_domain();

  static std::atomic<int> free_count{0};

  struct TrackedNode {
    int value;
    ~TrackedNode() { free_count.fetch_add(1, std::memory_order_relaxed); }
  };

  free_count = 0;

  auto *n1 = new TrackedNode{1};
  auto *n2 = new TrackedNode{2};

  domain.retire(n1);
  domain.retire(n2);
  domain.flush();

  REQUIRE(free_count.load() == 2);
}

TEST_CASE("Hazard custom reclaimer", "[hazard]") {
  auto &domain = quark::default_domain();
  int freed = 0;
  auto *p = new int(7);
  domain.retire(p, [&](int *q) {
    ++freed;
    delete q;
  });
  domain.flush();
  REQUIRE(freed == 1);
}

TEST_CASE("Hazard protect blocks reclaim", "[hazard]") {
  auto &domain = quark::default_domain();
  auto &handle = quark::thread_handle(domain);
  auto *rec = handle.record();

  static std::atomic<int> free_count{0};

  struct TrackedNode {
    int value;
    ~TrackedNode() { free_count.fetch_add(1, std::memory_order_relaxed); }
  };

  free_count = 0;
  auto *n = new TrackedNode{42};

  {
    quark::HazardGuard guard(domain, rec, 0);
    guard.protect(n);

    domain.retire(n);
    domain.flush();

    REQUIRE(free_count.load() == 0);
  }

  domain.flush();
  REQUIRE(free_count.load() == 1);
}

TEST_CASE("Hazard multi-domain", "[hazard]") {
  auto *d1 = new quark::HazardDomain();
  auto *d2 = new quark::HazardDomain();

  int freed1 = 0;
  int freed2 = 0;
  auto *p1 = new int(1);
  auto *p2 = new int(2);

  d1->retire(p1, [&](int *q) {
    ++freed1;
    delete q;
  });
  d2->retire(p2, [&](int *q) {
    ++freed2;
    delete q;
  });
  d1->flush();
  d2->flush();

  REQUIRE(freed1 == 1);
  REQUIRE(freed2 == 1);

  quark::release_thread_handle(*d1);
  quark::release_thread_handle(*d2);
  REQUIRE(d1->live_handles() == 0);
  REQUIRE(d2->live_handles() == 0);
  delete d1;
  delete d2;
}

TEST_CASE("Hazard concurrent stress", "[hazard][stress]") {
  constexpr int THREADS = 8;
  constexpr int ITERS = 1'000;

  auto &domain = quark::default_domain();

  std::atomic<int> errors{0};

  struct StressNode {
    std::atomic<bool> alive{true};
    int value;
    explicit StressNode(int v) : value(v) {}
    ~StressNode() { alive.store(false, std::memory_order_release); }
  };

  auto *shared = new StressNode(7);

  auto worker = [&] {
    auto &handle = quark::thread_handle(domain);
    auto *rec = handle.record();

    for (int i = 0; i < ITERS; ++i) {
      quark::HazardGuard guard(domain, rec, 0);
      guard.protect(shared);

      if (!shared->alive.load(std::memory_order_acquire)) {
        errors.fetch_add(1, std::memory_order_relaxed);
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(THREADS);
  for (int i = 0; i < THREADS; ++i)
    threads.push_back(std::thread(worker));
  for (auto &t : threads)
    t.join();

  domain.retire(shared);
  domain.flush();

  REQUIRE(errors.load() == 0);
}

TEST_CASE("Hazard seeded RNG scheduler", "[hazard][stress]") {
  constexpr std::uint32_t seed = 0xC0FFEEu;
  std::mt19937 rng(seed);
  auto *domain = new quark::HazardDomain();

  struct SchedNode {
    std::atomic<bool> alive{true};
    ~SchedNode() { alive.store(false, std::memory_order_release); }
  };

  constexpr int N = 32;
  std::vector<SchedNode *> nodes;
  nodes.reserve(N);
  for (int i = 0; i < N; ++i)
    nodes.push_back(new SchedNode());

  auto &handle = quark::thread_handle(*domain);
  auto *rec = handle.record();
  std::uniform_int_distribution<int> op_dist(0, 2);
  std::uniform_int_distribution<int> node_dist(0, N - 1);

  constexpr int OPS = 2'000;
  for (int i = 0; i < OPS; ++i) {
    const int op = op_dist(rng);
    if (op == 0) {
      quark::HazardGuard guard(*domain, rec, 0);
      auto *n = nodes[static_cast<std::size_t>(node_dist(rng))];
      if (n != nullptr) {
        guard.protect(n);
        REQUIRE(n->alive.load(std::memory_order_acquire));
      }
    } else if (op == 1) {
      const auto idx = static_cast<std::size_t>(node_dist(rng));
      if (nodes[idx] != nullptr) {
        domain->retire(nodes[idx]);
        nodes[idx] = nullptr;
      }
    } else {
      domain->flush();
    }
  }
  domain->flush();

  auto *shared = new SchedNode();
  std::atomic<int> errors{0};
  constexpr int THREADS = 4;
  constexpr int ITERS = 500;

  auto worker = [&] {
    auto &h = quark::thread_handle(*domain);
    auto *r = h.record();
    for (int i = 0; i < ITERS; ++i) {
      quark::HazardGuard guard(*domain, r, 0);
      guard.protect(shared);
      if (!shared->alive.load(std::memory_order_acquire))
        errors.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(THREADS);
  for (int i = 0; i < THREADS; ++i)
    threads.push_back(std::thread(worker));
  for (auto &t : threads)
    t.join();

  domain->retire(shared);
  for (auto *n : nodes) {
    if (n != nullptr)
      domain->retire(n);
  }
  domain->flush();

  REQUIRE(errors.load() == 0);

  quark::release_thread_handle(*domain);
  REQUIRE(domain->live_handles() == 0);
  delete domain;
}

TEST_CASE("Hazard destroy after release_thread_handle", "[hazard][lifetime]") {
  auto *domain = new quark::HazardDomain();
  (void)quark::thread_handle(*domain);
  quark::release_thread_handle(*domain);
  REQUIRE(domain->live_handles() == 0);
  delete domain;
}
