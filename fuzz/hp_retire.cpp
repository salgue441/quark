#include <quark/memory/hazard_ptr.hpp>
#include <quark/memory/tagged_ptr.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

struct Node {
  int value;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  quark::HazardDomain domain;
  auto &handle = quark::thread_handle(domain);
  std::vector<Node *> live;

  for (size_t i = 0; i < size; ++i) {
    switch (data[i] & 3u) {
    case 0: {
      auto *n = new Node{static_cast<int>(i)};
      live.push_back(n);
      quark::HazardGuard g(handle.record(), 0);
      g.protect(n);
      break;
    }
    case 1: {
      if (live.empty())
        break;
      Node *n = live.back();
      live.pop_back();
      domain.retire(n);
      break;
    }
    case 2:
      domain.flush();
      break;
    default: {
      int x = 0;
      quark::AtomicTaggedPtr<int> atm(quark::TaggedPtr<int>(&x, 0));
      auto cur = atm.load();
      auto next = cur.next(&x);
      (void)atm.compare_exchange_strong(cur, next);
      break;
    }
    }
  }

  for (Node *n : live)
    domain.retire(n);
  domain.flush();
  quark::release_thread_handle(domain);
  return 0;
}
