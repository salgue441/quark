#include <quark/core/version.hpp>

#include <cstdio>

int main() {
  std::printf("quark %s\n", quark::Version::string);
  return quark::Version::string[0] == '\0';
}
