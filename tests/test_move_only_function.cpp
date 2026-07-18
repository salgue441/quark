#include <quark/util/detail/move_only_function.hpp>

#include <cstdio>
#include <utility>

static int fails = 0;

#define EXPECT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++fails;                                                                 \
    }                                                                          \
  } while (0)

int main() {
  using quark::detail::move_only_function;

  move_only_function<void()> empty;
  EXPECT(!empty);

  int calls = 0;
  move_only_function<void()> f([&] { ++calls; });
  EXPECT(static_cast<bool>(f));
  f();
  EXPECT(calls == 1);

  move_only_function<void()> g(std::move(f));
  EXPECT(!f);
  EXPECT(static_cast<bool>(g));
  g();
  EXPECT(calls == 2);

  // Large callable forces heap path
  struct Big {
    char pad[128];
    int *counter;
    void operator()() const { ++(*counter); }
  };
  Big big{};
  big.counter = &calls;
  move_only_function<void()> h(big);
  h();
  EXPECT(calls == 3);

  if (fails != 0)
    return 1;
  std::puts("ok");
  return 0;
}
