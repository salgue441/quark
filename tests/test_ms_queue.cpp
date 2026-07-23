#include <quark/container/ms_queue.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

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
