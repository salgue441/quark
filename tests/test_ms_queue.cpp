#include <quark/container/ms_queue.hpp>
#include <quark/util/backoff.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

TEST_CASE("MsQueue empty pop", "[ms_queue]") {
  quark::MsQueue<int> q;
  int v = 0;
  REQUIRE_FALSE(q.try_pop(v));
}

TEST_CASE("MsQueue single-thread push pop order", "[ms_queue]") {
  quark::MsQueue<int> q;
  REQUIRE(q.try_push(1));
  REQUIRE(q.try_push(2));
  int v = 0;
  REQUIRE(q.try_pop(v));
  REQUIRE(v == 1);
  REQUIRE(q.try_pop(v));
  REQUIRE(v == 2);
}

TEST_CASE("MsQueue Result push/pop", "[ms_queue]") {
  quark::MsQueue<int> q;
  REQUIRE(q.push(9));
  auto r = q.pop();
  REQUIRE(r);
  REQUIRE(*r == 9);
  REQUIRE_FALSE(q.pop());
  REQUIRE(q.pop().error().code == quark::Error::QueueEmpty);
}

TEST_CASE("MsQueue move-only unique_ptr round-trip", "[ms_queue]") {
  quark::MsQueue<std::unique_ptr<int>> q;
  REQUIRE(q.try_push(std::make_unique<int>(42)));
  std::unique_ptr<int> out;
  REQUIRE(q.try_pop(out));
  REQUIRE(out);
  REQUIRE(*out == 42);
  REQUIRE_FALSE(q.try_pop(out));
}

TEST_CASE("MsQueue destructor drains remaining elements", "[ms_queue]") {
  auto ptr = std::make_unique<int>(7);
  int *raw = ptr.get();
  {
    quark::MsQueue<std::unique_ptr<int>> q;
    REQUIRE(q.try_push(std::move(ptr)));
    REQUIRE(q.try_push(std::make_unique<int>(8)));
    // destroy q with elements still inside — must not leak / must run unique_ptr dtors
  }
  (void)raw; // if ASan/leaks: unique_ptrs destroyed; no need to use raw after
  SUCCEED();
}

TEST_CASE("MsQueue borrowed domain does not destroy domain", "[ms_queue]") {
  quark::HazardDomain domain;
  {
    quark::MsQueue<int> q(domain);
    REQUIRE(q.try_push(1));
    int v = 0;
    REQUIRE(q.try_pop(v));
    REQUIRE(v == 1);
  }
  // domain still usable (queue did not destroy it)
  quark::MsQueue<int> q2(domain);
  REQUIRE(q2.try_push(2));
  int v = 0;
  REQUIRE(q2.try_pop(v));
  REQUIRE(v == 2);
  // Borrowed queues do not release handles; workers must before domain teardown.
  quark::release_thread_handle(domain);
}

TEST_CASE("MsQueue MPMC stress", "[ms_queue][stress]") {
  constexpr int Producers = 4;
  constexpr int Consumers = 4;
  constexpr int PerProducer = 2000; // keep runtime reasonable for CI
  constexpr int Total = Producers * PerProducer;

  quark::MsQueue<int> q;
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::vector<std::atomic<int>> seen(static_cast<std::size_t>(Total));
  for (auto &s : seen)
    s.store(0);

  std::vector<std::thread> threads;
  for (int p = 0; p < Producers; ++p) {
    threads.emplace_back([&, p] {
      quark::Backoff backoff;
      for (int i = 0; i < PerProducer; ++i) {
        const int id = p * PerProducer + i;
        while (!q.try_push(int(id)))
          backoff.pause();
        produced.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (int c = 0; c < Consumers; ++c) {
    threads.emplace_back([&] {
      quark::Backoff backoff;
      for (;;) {
        int v = 0;
        if (q.try_pop(v)) {
          seen[static_cast<std::size_t>(v)].fetch_add(1, std::memory_order_relaxed);
          if (consumed.fetch_add(1, std::memory_order_relaxed) + 1 == Total)
            return;
        } else {
          if (consumed.load(std::memory_order_relaxed) == Total)
            return;
          backoff.pause();
        }
      }
    });
  }
  for (auto &t : threads)
    t.join();
  REQUIRE(produced.load() == Total);
  REQUIRE(consumed.load() == Total);
  for (int i = 0; i < Total; ++i)
    REQUIRE(seen[static_cast<std::size_t>(i)].load() == 1);
}

TEST_CASE("MsQueue shared HazardDomain across threads", "[ms_queue]") {
  quark::HazardDomain domain;
  quark::MsQueue<int> q(domain);
  std::atomic<bool> ready{false};
  std::thread producer([&] {
    quark::Backoff b;
    while (!q.try_push(42))
      b.pause();
    ready.store(true);
    quark::release_thread_handle(domain);
  });
  std::thread consumer([&] {
    quark::Backoff b;
    int v = 0;
    while (!ready.load())
      b.pause();
    while (!q.try_pop(v))
      b.pause();
    REQUIRE(v == 42);
    quark::release_thread_handle(domain);
  });
  producer.join();
  consumer.join();
  // main may also have a handle if it touched the queue — release if needed
  quark::release_thread_handle(domain);
}
