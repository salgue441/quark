/**
 * @file tests/test_backoff.cpp
 * @brief Catch2 smoke tests for Backoff and cpu_relax.
 *
 * @author Carlos Salguero
 * @date 2026-07-18
 * @copyright Copyright (c) 2026
 */

#include <quark/util/backoff.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Backoff pause and reset", "[util]") {
  quark::Backoff b;
  b.pause();
  b.pause();
  b.reset();
  b.pause();
  SUCCEED();
}

TEST_CASE("cpu_relax does not throw", "[util]") {
  quark::cpu_relax();
  SUCCEED();
}
