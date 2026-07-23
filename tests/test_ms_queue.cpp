#include <quark/container/ms_queue.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("MsQueue empty pop", "[ms_queue]") {
  quark::MsQueue<int> q;
  int v = 0;
  REQUIRE_FALSE(q.try_pop(v));
}
