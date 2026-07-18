/**
 * @file quark/error.hpp
 * @brief Error handling utilities for lock-free data structures.
 *
 * This header provides a comprehensive error handling system based on
 * std::excepted. It defines error codes, rich error information structures,
 * and convenient Result type aliases along with factory function for
 * creating successful and erroneous results.
 *
 * The error handling follows a functional programming style, similar to
 * Rust's Result type, allowing for clean error propagation and handling.
 *
 * @author Carlos Salguero
 * @date 2026-07-17
 * @copyright Copyright (c) 2026
 *
 * @example
 * @code
 * #include <quark/error.hpp>
 *
 * quark::Result<int> divide(int a, int b) {
 *     if (b == 0) {
 *         return quark::Err<int>(quark::Error::InvalidArgument, "Division by zero");
 *     }
 *     return quark::Ok(a / b);
 * }
 *
 * void example() {
 *     auto result = divide(10, 2);
 *     if (result) {
 *         std::cout << "Result: " << *result << '\n';
 *     } else {
 *         std::cout << "Error: " << result.error().message << '\n';
 *     }
 * }
 * @endcode
 */

#pragma once

#include <expected>
#include <string>
#include <system_error>

namespace quark {

/**
 * @brief Error codes for lock-free data structure operations.
 *
 * This enumeration represents all possible error conditions that can occur
 * during operations on lock-free data structures in the library.
 */
enum class Error {
  QueueFull,              ///< The queue has reached its maximum capacity.
  QueueEmpty,             ///< The queue contains no elements.
  AllocationFailed,       ///< Memory allocation for a new node failed.
  HazardPointerExhausted, ///< No hazard pointer slots are available.
  InvalidArgument,        ///< An invalid argument was provided.
  Timeout,                ///< An operation exceeded its time limit.
};

/**
 * @brief Rich error information structure.
 *
 * Provides both an error code and a human-readable message. This type is
 * used as the error type in all Result aliases.
 *
 * The struct is designed for convenience with implicit conversion from
 * Error enum values, allowing for concise error returns:
 * @code
 * return quark::Error::QueueFull;
 * @endcode
 */
struct ErrorInfo {
  Error code;          ///< The error code.
  std::string message; ///< Human-readable error description.

  /**
   * @brief Constructs an ErrorInfo from an Error code.
   *
   * The message will be automatically populated with a default description
   * for the given error code. This constructor enables implicit conversion
   * from Error to ErrorInfo.
   *
   * @param code The error code
   */
  ErrorInfo(Error code) : code(code), message(default_message(code)) {}

  /**
   * @brief Constructs an ErrorInfo with a custom message.
   *
   * @param code The error code
   * @param msg The custom error message
   */
  ErrorInfo(Error code, std::string msg)
      : code(code), message(std::move(msg)) {}

private:
  /**
   * @brief Returns the default message for an error code.
   *
   * @param error The error code
   * @return A string_view containing the default error description
   */
  static constexpr std::string_view default_message(Error error) {
    switch (error) {
    case Error::QueueFull:
      return "Queue is at capacity";
    case Error::QueueEmpty:
      return "Queue is empty";
    case Error::AllocationFailed:
      return "Allocation failed";
    case Error::HazardPointerExhausted:
      return "No hazard pointers available";
    case Error::InvalidArgument:
      return "Invalid argument";
    case Error::Timeout:
      return "Operation timed out";
    }

    return "Unknown error";
  }
};

/**
 * @brief Result type alias for operations that return a value.
 *
 * This is a convenience alias for std::expected<T, ErrorInfo>, providing
 * a consistent error handling interface throughout the library.
 *
 * @tparam T The type of the success value
 *
 * @code
 * quark::Result<int> divide(int a, int b) {
 *     if (b == 0)
 *         return quark::Err<int>(quark::Error::InvalidArgument, "Division by zero");
 *     return quark::Ok(a / b);
 * }
 * @endcode
 */
template <typename T> using Result = std::expected<T, ErrorInfo>;

/**
 * @brief Result type alias for operations that return nothing on success.
 *
 * This is a convenience alias for std::expected<void, ErrorInfo>, used for
 * operations that either succeed (with no value) or fail with an error.
 *
 * @code
 * quark::VoidResult doSomething() {
 *     if (full())
 *          return quark::Err(quark::Error::QueueFull);
 *
 *     return quark::Ok();
 * }
 * @endcode
 */
using VoidResult = std::expected<void, ErrorInfo>;

/**
 * @brief Creates a successful Result containing a value.
 *
 * @tparam T The deduced type of the value
 * @param val The value to wrap in a Result
 * @return A Result containing the value
 *
 * @code
 * auto result = quark::Ok(42);
 * @endcode
 */
template <typename T> auto Ok(T &&val) {
  return Result<std::decay_t<T>>(std::forward<T>(val));
}

/**
 * @brief Creates a successful VoidResult.
 *
 * @return A VoidResult indicating success with no value
 *
 * @code
 * quark::VoidResult operation() {
 *     // ... do something that might fail
 *     return quark::Ok();
 * }
 * @endcode
 */
inline auto Ok() { return VoidResult{}; }

/**
 * @brief Creates an error Result with the given error code.
 *
 * @tparam T The success type of the Result (deduced for non-void, or
 *           specified for void)
 * @param code The error code
 * @return A Result containing the error
 *
 * @code
 * // For non-void results, type is deduced
 * auto result = quark::Err(quark::Error::QueueEmpty);
 *
 * // For void results, template argument must be specified
 * auto result = quark::Err<void>(quark::Error::QueueEmpty);
 * @endcode
 */
template <typename T = void> auto Err(Error code) {
  if constexpr (std::is_void_v<T>)
    return VoidResult{std::unexpected{ErrorInfo{code}}};
  else
    return Result<T>{std::unexpected{ErrorInfo{code}}};
}

/**
 * @brief Creates an error Result with the given error code and custom message.
 *
 * @tparam T The success type of the Result (deduced for non-void, or specified
 * for void)
 * @param code The error code
 * @param msg The custom error message
 * @return A Result containing the error
 *
 * @code
 * // For non-void results, type is deduced
 * auto result = quark::Err(quark::Error::InvalidArgument, "Invalid index");
 *
 * // For void results, template argument must be specified
 * auto result = quark::Err<void>(quark::Error::Timeout, "Operation timed out");
 * @endcode
 */
template <typename T = void> auto Err(Error code, std::string msg) {
  if constexpr (std::is_void_v<T>)
    return VoidResult{std::unexpected{ErrorInfo{code, std::move(msg)}}};
  else
    return Result<T>{std::unexpected{ErrorInfo{code, std::move(msg)}}};
}

} // namespace quark
