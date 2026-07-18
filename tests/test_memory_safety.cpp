// tests/test_memory_safety.cpp
//
// Unit + stress tests for memory/tagged_ptr.hpp and memory/hazard_ptr.hpp.
// Build with: g++ -std=c++23 -O2 -fsanitize=thread,address -I include tests/test_memory_safety.cpp -o test_memory_safety
// Or via CMake with -DQUARK_SANITIZE=ON

#include <quark/memory/tagged_ptr.hpp>
#include <quark/memory/hazard_ptr.hpp>
 
#include <atomic>
#include <cassert>
#include <iostream>
#include <format>
#include <thread>
#include <vector>

static int tests_run    = 0;
static int tests_passed = 0;
 
#define CHECK(expr) do {                                                     \
    ++tests_run;                                                             \
    if (expr) { ++tests_passed; }                                           \
    else {                                                                   \
        std::cerr << std::format("  FAIL  {}:{} — {}\n",                   \
                                 __FILE__, __LINE__, #expr);                \
    }                                                                        \
} while (false)
 
#define SECTION(name) std::cout << std::format("\n[{}]\n", name)

void test_tagged_ptr_basic() {
    SECTION("TaggedPtr — construction and accessors");
 
    int x = 42;
    quark::TaggedPtr<int> tp(&x, 7);
 
    CHECK(tp.ptr() == &x);
    CHECK(tp.tag() == 7);
    CHECK(*tp == 42);
    CHECK(static_cast<bool>(tp) == true);
}
 
void test_tagged_ptr_null() {
    SECTION("TaggedPtr — null pointer");
 
    quark::TaggedPtr<int> tp;
    CHECK(tp.ptr() == nullptr);
    CHECK(tp.tag() == 0);
    CHECK(!tp);
}
 
void test_tagged_ptr_next() {
    SECTION("TaggedPtr — next() bumps version");
 
    int x = 1, y = 2;
    quark::TaggedPtr<int> tp(&x, 3);
 
    auto tp2 = tp.next(&y);
    CHECK(tp2.ptr() == &y);
    CHECK(tp2.tag() == 4);     // incremented
    CHECK(tp != tp2);
}
 
void test_tagged_ptr_version_wrap() {
    SECTION("TaggedPtr — tag wraps at 65535");
 
    int x = 0;
    quark::TaggedPtr<int> tp(&x, 65535u);
    auto wrapped = tp.next(&x);
    CHECK(wrapped.tag() == 0);  // wraps silently
}
 
void test_tagged_ptr_equality() {
    SECTION("TaggedPtr — equality checks both ptr and tag");
 
    int x = 0;
    quark::TaggedPtr<int> a(&x, 1);
    quark::TaggedPtr<int> b(&x, 1);
    quark::TaggedPtr<int> c(&x, 2);  // same ptr, different tag
 
    CHECK(a == b);
    CHECK(a != c);
}
 
void test_atomic_tagged_ptr_cas() {
    SECTION("AtomicTaggedPtr — CAS succeeds and bumps version");
 
    int x = 0, y = 0;
    quark::TaggedPtr<int> init(&x, 0);
    quark::AtomicTaggedPtr<int> atp(init);
 
    auto expected = atp.load(std::memory_order_acquire);
    auto desired  = expected.next(&y);
 
    bool ok = atp.compare_exchange_strong(
        expected, desired,
        std::memory_order_release,
        std::memory_order_acquire);
 
    CHECK(ok);
    CHECK(atp.load().ptr() == &y);
    CHECK(atp.load().tag() == 1);
}
 
void test_atomic_tagged_ptr_cas_aba_prevention() {
    SECTION("AtomicTaggedPtr — stale CAS fails due to version mismatch");
 
    int x = 0;
    quark::TaggedPtr<int> v0(&x, 0);
    quark::AtomicTaggedPtr<int> atp(v0);
 
    // Thread A reads v0
    auto a_snapshot = atp.load(std::memory_order_acquire);
 
    // Thread B changes and changes back (ABA): v0 → v1 → v0 (but tag is now 2)
    {
        auto cur = atp.load(std::memory_order_acquire);
        auto v1  = cur.next(&x);                  // tag = 1
        atp.compare_exchange_strong(cur, v1,
            std::memory_order_release,
            std::memory_order_acquire);
 
        cur = atp.load(std::memory_order_acquire);
        auto v2 = cur.next(&x);                   // tag = 2, same ptr
        atp.compare_exchange_strong(cur, v2,
            std::memory_order_release,
            std::memory_order_acquire);
    }
 
    // Thread A tries its stale CAS — must FAIL because tag differs (0 vs 2)
    auto a_desired = a_snapshot.next(&x);
    bool ok = atp.compare_exchange_strong(
        a_snapshot, a_desired,
        std::memory_order_release,
        std::memory_order_acquire);
 
    CHECK(!ok);                            // ABA correctly detected
    CHECK(atp.load().tag() == 2);          // value set by thread B is intact
}
 
// ── HazardPointer tests ───────────────────────────────────────────────────────
 
struct Node {
    int   value;
    std::atomic<Node*> next{nullptr};
 
    explicit Node(int v) : value(v) {}
};
 
void test_hazard_guard_protects() {
    SECTION("HazardGuard — protect publishes pointer, clear removes it");
 
    auto& domain = quark::default_domain();
    auto& handle = quark::thread_handle(domain);
    auto* rec    = handle.record();
 
    Node n(99);
 
    {
        quark::HazardGuard guard(domain, rec, 0);
        guard.protect(&n);
 
        // Slot should now contain &n
        void* published = rec->slots[0].load(std::memory_order_acquire);
        CHECK(published == static_cast<void*>(&n));
    }
 
    // After guard destruction, slot is cleared
    void* after = rec->slots[0].load(std::memory_order_acquire);
    CHECK(after == nullptr);
}
 
void test_retire_then_scan_frees_safe_nodes() {
    SECTION("HazardDomain — retire frees node when no hazard covers it");
 
    auto& domain = quark::default_domain();
 
    static std::atomic<int> free_count{0};
 
    struct TrackedNode {
        int value;
        ~TrackedNode() { free_count.fetch_add(1, std::memory_order_relaxed); }
    };
 
    free_count = 0;
 
    auto* n1 = new TrackedNode{1};
    auto* n2 = new TrackedNode{2};
 
    // Retire both — no hazard guards active, so scan should free both
    domain.retire(n1);
    domain.retire(n2);
    domain.flush();   // force scan of this thread's retire list
 
    CHECK(free_count.load() == 2);
}
 
void test_retire_defers_protected_node() {
    SECTION("HazardDomain — retire defers node while hazard guard holds it");
 
    auto& domain = quark::default_domain();
    auto& handle = quark::thread_handle(domain);
    auto* rec    = handle.record();
 
    static std::atomic<int> free_count{0};
 
    struct TrackedNode {
        int value;
        ~TrackedNode() { free_count.fetch_add(1, std::memory_order_relaxed); }
    };
 
    free_count = 0;
    auto* n = new TrackedNode{42};
 
    {
        // Guard is active — n is protected
        quark::HazardGuard guard(domain, rec, 0);
        guard.protect(n);
 
        domain.retire(n);
        domain.flush();  // scan runs — should defer, not free
 
        CHECK(free_count.load() == 0);  // still protected
    }
    // Guard destroyed — n no longer protected
 
    // Now flush again — n should be freed
    domain.flush();
    CHECK(free_count.load() == 1);
}
 
// ── Stress test ───────────────────────────────────────────────────────────────
//
// N threads each: acquire hazard slot → protect a live node → verify it isn't
// freed → release guard → retire. With ThreadSanitizer this catches data races.
//
void stress_hazard_concurrent() {
    SECTION("HazardDomain — concurrent protect + retire stress test");
 
    constexpr int THREADS   = 8;
    constexpr int ITERS     = 1'000;
 
    auto& domain = quark::default_domain();
 
    std::atomic<int> freed{0};
    std::atomic<int> errors{0};
 
    struct StressNode {
        std::atomic<bool> alive{true};
        int               value;
        explicit StressNode(int v) : value(v) {}
        ~StressNode() { alive.store(false, std::memory_order_release); }
    };
 
    // Shared node all threads will try to protect
    auto* shared = new StressNode(7);
 
    auto worker = [&] {
        auto& handle = quark::thread_handle(domain);
        auto* rec    = handle.record();
 
        for (int i = 0; i < ITERS; ++i) {
            quark::HazardGuard guard(domain, rec, 0);
            guard.protect(shared);
 
            // Validate pointer hasn't been freed while we hold the guard
            // (alive would be false if the destructor ran)
            if (!shared->alive.load(std::memory_order_acquire)) {
                errors.fetch_add(1, std::memory_order_relaxed);
            }
            // guard clears slot on scope exit
        }
    };
 
    std::vector<std::thread> threads;
    threads.reserve(THREADS);
    for (int i = 0; i < THREADS; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads) t.join();
 
    // Now retire the shared node — all guards are gone
    domain.retire(shared);
    domain.flush();
 
    CHECK(errors.load() == 0);  // no use-after-free observed
}
 
// ── Main ──────────────────────────────────────────────────────────────────────
 
int main() {
    std::cout << "=== quark memory safety tests ===\n";
 
    // TaggedPtr
    test_tagged_ptr_basic();
    test_tagged_ptr_null();
    test_tagged_ptr_next();
    test_tagged_ptr_version_wrap();
    test_tagged_ptr_equality();
    test_atomic_tagged_ptr_cas();
    test_atomic_tagged_ptr_cas_aba_prevention();
 
    // HazardPointer
    test_hazard_guard_protects();
    test_retire_then_scan_frees_safe_nodes();
    test_retire_defers_protected_node();
    stress_hazard_concurrent();
 
    std::cout << std::format("\n{}/{} tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}