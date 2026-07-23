#include <quark/container/ms_queue.hpp>
#include <catch2/catch_test_macros.hpp>

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
