/**
 * @file tests/test_move_only_function.cpp
 * @brief Catch2 coverage for detail::move_only_function.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 */

#include <quark/util/detail/move_only_function.hpp>

#include <catch2/catch_test_macros.hpp>

#include <utility>

TEST_CASE("move_only_function empty and invoke", "[util]") {
  using quark::detail::move_only_function;

  move_only_function<void()> empty;
  REQUIRE_FALSE(empty);

  int calls = 0;
  move_only_function<void()> f([&] { ++calls; });
  REQUIRE(f);
  f();
  REQUIRE(calls == 1);

  move_only_function<void()> g(std::move(f));
  REQUIRE_FALSE(f);
  REQUIRE(g);
  g();
  REQUIRE(calls == 2);
}

TEST_CASE("move_only_function heap path", "[util]") {
  using quark::detail::move_only_function;

  int calls = 0;
  struct Big {
    char pad[128]{};
    int *counter{};
    void operator()() const { ++(*counter); }
  };
  Big big;
  big.counter = &calls;
  move_only_function<void()> h(big);
  h();
  REQUIRE(calls == 1);
}
