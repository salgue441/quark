#include <quark/memory/hazard_ptr.hpp>

int main() {
  quark::HazardDomain domain;
  (void)quark::thread_handle(domain);
  // Intentionally destroy with live handle — must abort
  // (domain destructor runs at end of main while handle still in map)
  return 0;
}
