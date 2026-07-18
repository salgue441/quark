/**
 * @file util/detail/move_only_function.hpp
 * @brief Small-buffer move-only callable polyfill (`void()`).
 *
 * Used by hazard-pointer reclaimers so Apple Clang / older libc++ builds
 * do not depend on `std::move_only_function`.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace quark::detail {

template <typename Signature>
class move_only_function;

template <>
class move_only_function<void()> {
  static constexpr std::size_t buf_size = 32;

  struct vtable {
    void (*invoke)(void *storage);
    void (*destroy)(void *storage);
    void (*move)(void *dst, void *src);
  };

  template <typename T>
  static const vtable *local_vtable() {
    static const vtable vt{
        [](void *p) { (*static_cast<T *>(p))(); },
        [](void *p) { std::destroy_at(static_cast<T *>(p)); },
        [](void *dst, void *src) {
          ::new (dst) T(std::move(*static_cast<T *>(src)));
          std::destroy_at(static_cast<T *>(src));
        }};
    return &vt;
  }

  template <typename T>
  static const vtable *heap_vtable() {
    static const vtable vt{
        [](void *p) { (**static_cast<T **>(p))(); },
        [](void *p) {
          delete *static_cast<T **>(p);
          *static_cast<T **>(p) = nullptr;
        },
        [](void *dst, void *src) {
          *static_cast<T **>(dst) = *static_cast<T **>(src);
          *static_cast<T **>(src) = nullptr;
        }};
    return &vt;
  }

public:
  move_only_function() noexcept = default;
  move_only_function(std::nullptr_t) noexcept {}

  template <typename F,
            typename T = std::decay_t<F>,
            typename = std::enable_if_t<!std::is_same_v<T, move_only_function>>>
  move_only_function(F &&f) {
    if constexpr (sizeof(T) <= buf_size && alignof(T) <= alignof(std::max_align_t)) {
      m_vt = local_vtable<T>();
      ::new (static_cast<void *>(m_buf)) T(std::forward<F>(f));
    } else {
      m_vt = heap_vtable<T>();
      auto **slot = reinterpret_cast<T **>(m_buf);
      *slot = new T(std::forward<F>(f));
    }
  }

  move_only_function(move_only_function &&other) noexcept { move_from(other); }

  move_only_function &operator=(move_only_function &&other) noexcept {
    if (this != &other) {
      reset();
      move_from(other);
    }
    return *this;
  }

  move_only_function(const move_only_function &) = delete;
  move_only_function &operator=(const move_only_function &) = delete;

  ~move_only_function() { reset(); }

  explicit operator bool() const noexcept { return m_vt != nullptr; }

  void operator()() {
    if (m_vt)
      m_vt->invoke(m_buf);
  }

  void reset() noexcept {
    if (m_vt) {
      m_vt->destroy(m_buf);
      m_vt = nullptr;
    }
  }

private:
  void move_from(move_only_function &other) noexcept {
    if (!other.m_vt)
      return;
    m_vt = other.m_vt;
    other.m_vt->move(m_buf, other.m_buf);
    other.m_vt = nullptr;
  }

  alignas(std::max_align_t) unsigned char m_buf[buf_size]{};
  const vtable *m_vt = nullptr;
};

} // namespace quark::detail
