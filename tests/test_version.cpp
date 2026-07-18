/**
 * @file tests/test_version.cpp
 * @brief Catch2 checks that generated Version matches PROJECT_VERSION.
 */

#include <quark/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("Version matches project 0.1.0", "[version]") {
  REQUIRE(quark::Version::major == 0);
  REQUIRE(quark::Version::minor == 1);
  REQUIRE(quark::Version::patch == 0);
  REQUIRE(std::string_view{quark::Version::string} == "0.1.0");
}
