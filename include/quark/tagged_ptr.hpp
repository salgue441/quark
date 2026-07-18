/**
 * @file tagged_ptr.hpp
 * @brief ABA-safe tagged pointer implementation for lock-free data structures.
 *
 * This header provides a tagged pointer implementation that solves the ABA
 * problem in lock-free algorithms. The ABA problem occurs when a
 * Compare-And-Swap (CAS) operation on a pointer succeeds even though the
 * underlying data structure has changed, because the pointer value appears
 * identical.
 *
 * The solution packs a monotonically-increasing version counter into the unused
 * high bits of a 64-bit pointer. On x86-64 and ARM64 architectures, user-space
 * virtual addresses only use 48 bits, leaving 16 bits available for a version
 * counter. Every successful CAS operation increments the version, ensuring that
 * a recycled pointer at the same address will have a different tag.
 *
 * Memory Layout (64 bits):
 * @code
 *   [63..48]  version counter  (16 bits → 65,535 increments before wrap)
 *   [47..0]   pointer          (48 bits → full user-space address range)
 * @endcode
 *
 * The implementation provides both a non-atomic value type (TaggedPtr) and an
 * atomic wrapper (AtomicTaggedPtr) with convenient CAS operations.
 *
 * @author Carlos Salguero
 * @date 2026-07-17
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/tagged_ptr.hpp>
 *
 * // Node in a lock-free stack
 * struct Node {
 *     int value;
 *     quark::AtomicTaggedPtr<Node> next;
 * };
 *
 * // Push operation (simplified)
 * void push(quark::AtomicTaggedPtr<Node>& head, Node* new_node) {
 *     auto expected = head.load();
 *     while (true) {
 *         new_node->next.store(expected);
 *         auto desired = expected.next(new_node);
 *         if (head.compare_exchange_weak(expected, desired)) {
 *             break; // Success
 *         }
 *         // Expected is updated to current value on failure, retry
 *     }
 * }
 * @endcode
 */

namespace quark {
/**
 * @brief Number of bits used for the pointer portion.
 *
 * User-space virtual addresses on x86-64 and ARM64 use 48 bits.
 * This leaves 16 bits (bits 48-63) for the version counter.
 */
inline constexpr int PTR_BITS = 48;

/**
 * @brief Mask to extract the pointer portion.
 *
 * Low 48 bits: 0x0000FFFFFFFFFFFF
 */
inline constexpr uint64_t PTR_MASK = (1ULL << PTR_BITS) - 1;

/**
 * @brief Mask to extract the tag portion.
 *
 * High 16 bits: 0xFFFF000000000000
 */
inline constexpr uint64_t TAG_MASK = ~PTR_MASK;

/**
 * @brief Number of bits to shift for the tag portion.
 */
inline constexpr int TAG_SHIFT = PTR_BITS;

/**
 * @brief Non-atomic tagged pointer value type.
 *
 * This is a trivially copyable type that packs a pointer and a 16-bit version
 * counter into a single 64-bit value. It is designed to be stored inside
 * std::atomic<TaggedPtr<T>> for lock-free operations.
 *
 * The type is constexpr-friendly and can be used in compile-time contexts.
 * All operations are noexcept and should be optimized to register operations.
 *
 * @tparam T The type of the pointed-to object
 *
 * @note This type must be 64-bit and trivially copyable to be lock-free.
 *       A static assertion verifies this property in AtomicTaggedPtr.
 */
template <typename T> class TaggedPtr {
public:
  /**
   * @brief Default constructor.
   *
   * Initializes to a null pointer with tag 0.
   */
  constexpr TaggedPtr() noexcept : m_raw(0) {}

  /**
   * @brief Constructs a TaggedPtr from a pointer and optional tag.
   *
   * @param ptr The pointer to wrap (must fit in 48 bits)
   * @param tag The initial version counter (default: 0)
   *
   * @note The pointer must be within the 48-bit user-space address range.
   *       This is true for heap and stack addresses on all supported
   *       platforms. A debug assertion checks this at runtime.
   */
  explicit TaggedPtr(T *ptr, uint16_t tag = 0) noexcept
      : m_raw(pack(ptr, tag)) {
    assert((reinterpret_cast<uintptr_t>(ptr) & TAG_MASK) == 0 &&
           "Pointer exceeds 48-bit addressable range");
  }

  /**
   * @brief Returns the stored pointer.
   *
   * @return The pointer value (may be nullptr)
   */
  [[nodiscard]] T *ptr() const noexcept {
    return reinterpret_cast<T *>(m_raw & PTR_MASK);
  }

  /**
   * @brief Returns the stored version counter.
   *
   * @return The current tag value (0-65535)
   */
  [[nodiscard]] uint16_t tag() const noexcept {
    return static_cast<uint16_t>(m_raw >> TAG_SHIFT);
  }

  /**
   * @brief Pointer dereference operator.
   *
   * @return The stored pointer
   *
   * @warning Assumes the pointer is not null. Use in contexts where
   *          the pointer is guaranteed to be valid.
   */
  [[nodiscard]] T *operator->() const noexcept { return ptr(); }

  /**
   * @brief Pointer dereference operator.
   *
   * @return Reference to the pointed-to object
   *
   * @warning Assumes the pointer is not null. Use in contexts where
   *          the pointer is guaranteed to be valid.
   */
  [[nodiscard]] T &operator*() const noexcept { return *ptr(); }

  /**
   * @brief Tests if the pointer is not null.
   *
   * @return true if the pointer is non-null, false otherwise
   */
  [[nodiscard]] explicit operator bool() const noexcept {
    return ptr() != nullptr;
  }

  /**
   * @brief Creates a new TaggedPtr with an incremented tag.
   *
   * This is the fundamental operation for ABA prevention. Every successful
   * CAS should use this to increment the version counter.
   *
   * @param new_ptr The pointer for the new TaggedPtr
   * @return A new TaggedPtr with tag+1 (wraps from 65535 to 0)
   *
   * @note The tag wraps silently from 65535 to 0. In practice, queues are
   *       drained long before 65,535 consecutive ABA races on one slot.
   */
  [[nodiscard]] TaggedPtr next(T *new_ptr) const noexcept {
    return TaggedPtr(new_ptr, static_cast<uint16_t>(tag() + 1));
  }

  /**
   * @brief Creates a new TaggedPtr with the same pointer but bumped tag.
   *
   * Useful for incrementing the tag on the same pointer, e.g., for
   * null sentinel updates.
   *
   * @return A new TaggedPtr with the same pointer and tag+1
   */
  [[nodiscard]] TaggedPtr bumped() const noexcept { return next(ptr()); }

  /**
   * @brief Equality comparison.
   *
   * Both the pointer and tag must match for equality.
   *
   * @param o The other TaggedPtr to compare
   * @return true if both pointer and tag are equal
   */
  [[nodiscard]] bool operator==(const TaggedPtr &o) const noexcept {
    return m_raw == o.m_raw;
  }

  /**
   * @brief Inequality comparison.
   *
   * @param o The other TaggedPtr to compare
   * @return true if either pointer or tag differs
   */
  [[nodiscard]] bool operator!=(const TaggedPtr &o) const noexcept {
    return m_raw != o.m_raw;
  }

  /**
   * @brief Returns the raw 64-bit representation.
   *
   * Provided for compatibility with std::atomic internals.
   *
   * @return The packed pointer and tag as a 64-bit integer
   */
  [[nodiscard]] uint64_t raw() const noexcept { return m_raw; }

private:
  /**
   * @brief Packs a pointer and tag into a single 64-bit value
   *
   * @param ptr The pointer to pack
   * @param tag The tag to pack
   * @return The packed 64-bit value
   */
  static uint64_t pack(T *ptr, uint16_t tag) noexcept {
    return (static_cast<uint64_t>(tag) << TAG_SHIFT) |
           (reinterpret_cast<uintptr_t>(ptr) & PTR_MASK);
  }

private:
  uint64_t m_raw; ///< Packed pointer and tag
};

/**
 * @brief Atomic wrapper for tagged pointers.
 *
 * This is a thin wrapper around std::atomic<TaggedPtr<T>> with convenient
 * CAS operations. It is designed to be used in lock-free data structures
 * and provides the same memory ordering semantics as std::atomic.
 *
 * The wrapper prevents accidental copies (atomic types are not copyable)
 * and provides strong and weak compare-and-exchange operations with
 * ergonomic signatures.
 *
 * @tparam T The type of the pointed-to object
 *
 * @note This type should always be aligned to a cache line when used as
 *       head/tail pointers in lock-free queues. Use
 *       CacheAligned<AtomicTaggedPtr> from arch.hpp for this purpose.
 *
 * @see TaggedPtr
 */
template <typename T> class AtomicTaggedPtr {
public:
  /**
   * @brief Default constructor.
   *
   * Initializes to a null pointer with tag 0.
   */
  AtomicTaggedPtr() noexcept : m_atomic(TaggedPtr<T>{}) {}

  /**
   * @brief Constructor with initial value.
   *
   * @param init The initial TaggedPtr value
   */
  explicit AtomicTaggedPtr(TaggedPtr<T> init) noexcept : m_atomic(init) {}

  // Prevent accidental copies (atomics are not copyable anyway)
  AtomicTaggedPtr(const AtomicTaggedPtr &) = delete;
  AtomicTaggedPtr &operator=(const AtomicTaggedPtr &) = delete;

  /**
   * @brief Atomically loads the current value.
   *
   * @param order The memory ordering constraint (default: seq_cst)
   * @return The current TaggedPtr value
   */
  [[nodiscard]] TaggedPtr<T>
  load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return m_atomic.load(order);
  }

  /**
   * @brief Atomically stores a new value.
   *
   * @param desired The value to store
   * @param order The memory ordering constraint (default: seq_cst)
   */
  void store(TaggedPtr<T> desired,
             std::memory_order order = std::memory_order_seq_cst) noexcept {
    m_atomic.store(desired, order);
  }

  /**
   * @brief Performs a strong compare-and-exchange operation.
   *
   * Fails only if the current value differs from `expected` (no spurious
   * failures). Updates `expected` to the current value on failure.
   *
   * @param expected The expected value (updated on failure)
   * @param desired The desired value to store on success
   * @param success The memory ordering on success (default: seq_cst)
   * @param failure The memory ordering on failure (default: acquire)
   * @return true if the exchange succeeded, false otherwise
   *
   * @note On failure, `expected` is updated to the current value, allowing
   *       the caller to retry without an additional load.
   */
  bool compare_exchange_strong(
      TaggedPtr<T> &expected, TaggedPtr<T> desired,
      std::memory_order success = std::memory_order_seq_cst,
      std::memory_order failure = std::memory_order_acquire) noexcept {
    return m_atomic.compare_exchange_strong(expected, desired, success,
                                            failure);
  }

  /**
   * @brief Performs a weak compare-and-exchange operation.
   *
   * May spuriously fail even when the current value equals `expected`.
   * Use this in a loop where retries are cheap; prefer strong CAS for
   * single-attempt operations.
   *
   * @param expected The expected value (updated on failure)
   * @param desired The desired value to store on success
   * @param success The memory ordering on success (default: seq_cst)
   * @param failure The memory ordering on failure (default: acquire)
   * @return true if the exchange succeeded, false otherwise
   *
   * @note On some platforms, weak CAS may be faster than strong CAS
   *       on architectures that don't implement strong CAS natively.
   *       Updates `expected` to the current value on failure.
   */
  bool
  compare_exchange_weak(TaggedPtr<T> &expected, TaggedPtr<T> desired,
                        std::memory_order success = std::memory_order_seq_cst,
                        std::memory_order failure = std::memory_order_acquire) {
    return m_atomic.compare_exchange_weak(expected, desired, success, failure);
  }

private:
  std::atomic<TaggedPtr<T>> m_atomic;

  /**
   * @brief Static assertion ensuring lock-free operation.
   *
   * Verifies that TaggedPtr is 64-bit and trivially copyable, which is
   * required for std::atomic to be lock-free.
   */
  static_assert(std::atomic<TaggedPtr<T>>::is_always_lock_free,
                "std::atomic<TaggedPtr<T>> is not lock-free on this platform. "
                "TaggedPtr must be 64-bit (trivially copyable, sizeof == 8.)");
};
} // namespace quark
