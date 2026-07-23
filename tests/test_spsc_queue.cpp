/**
 * @file tests/test_spsc_queue.cpp
 * @brief Catch2 tests for quark::SpscQueue.
 */

#include <quark/container/spsc_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

TEST_CASE("SpscQueue rejects capacity < 2", "[spsc]") {
  REQUIRE_THROWS_AS(quark::SpscQueue<int>(0), std::invalid_argument);
  REQUIRE_THROWS_AS(quark::SpscQueue<int>(1), std::invalid_argument);
}

TEST_CASE("SpscQueue try_push / try_pop fill and empty", "[spsc]") {
  quark::SpscQueue<int> q(4); // usable depth 3
  REQUIRE(q.capacity() == 4);

  REQUIRE(q.try_push(1));
  REQUIRE(q.try_push(2));
  REQUIRE(q.try_push(3));
  REQUIRE_FALSE(q.try_push(4));

  int v = 0;
  REQUIRE(q.try_pop(v));
  REQUIRE(v == 1);
  REQUIRE(q.try_pop(v));
  REQUIRE(v == 2);
  REQUIRE(q.try_pop(v));
  REQUIRE(v == 3);
  REQUIRE_FALSE(q.try_pop(v));
}

TEST_CASE("SpscQueue Result wrappers", "[spsc]") {
  quark::SpscQueue<int> q(2);
  auto ok = q.push(7);
  REQUIRE(ok);
  auto full = q.push(8);
  REQUIRE_FALSE(full);
  REQUIRE(full.error().code == quark::Error::QueueFull);

  auto got = q.pop();
  REQUIRE(got);
  REQUIRE(*got == 7);
  auto empty = q.pop();
  REQUIRE_FALSE(empty);
  REQUIRE(empty.error().code == quark::Error::QueueEmpty);
}

TEST_CASE("SpscQueue move-only elements", "[spsc]") {
  quark::SpscQueue<std::unique_ptr<int>> q(3);
  REQUIRE(q.try_push(std::make_unique<int>(42)));
  std::unique_ptr<int> out;
  REQUIRE(q.try_pop(out));
  REQUIRE(out);
  REQUIRE(*out == 42);
}

TEST_CASE("SpscQueue preserves order under 1P/1C stress", "[spsc][stress]") {
  constexpr int n = 50'000;
  quark::SpscQueue<int> q(1024);
  std::atomic<bool> start{false};

  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int i = 0; i < n; ++i) {
      while (!q.try_push(i)) {
      }
    }
  });

  std::vector<int> got;
  got.reserve(static_cast<std::size_t>(n));
  std::thread consumer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    int v = 0;
    for (int i = 0; i < n; ++i) {
      while (!q.try_pop(v)) {
      }
      got.push_back(v);
    }
  });

  start.store(true, std::memory_order_release);
  producer.join();
  consumer.join();

  REQUIRE(got.size() == static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    REQUIRE(got[static_cast<std::size_t>(i)] == i);
}

TEST_CASE("SpscQueue push_wait / pop_wait", "[spsc]") {
  quark::SpscQueue<int> q(4);
  std::atomic<int> consumed{0};

  std::thread consumer([&] {
    for (int i = 0; i < 3; ++i) {
      int v = 0;
      q.pop_wait(v);
      REQUIRE(v == i);
      consumed.fetch_add(1, std::memory_order_release);
    }
  });

  for (int i = 0; i < 3; ++i)
    q.push_wait(i);

  consumer.join();
  REQUIRE(consumed.load(std::memory_order_acquire) == 3);
}
