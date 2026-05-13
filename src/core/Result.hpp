#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

template <class T>
concept ResultArrowTarget = std::is_object_v<T>;

template <class E>
class Unexpected {
 public:
  explicit Unexpected(E error) : error_(std::move(error)) {}

  [[nodiscard]] E&& error() && { return std::move(error_); }

 private:
  E error_;
};

template <class E>
[[nodiscard]] Unexpected<std::decay_t<E>> make_unexpected(E&& error) {
  return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

template <class T, class E = std::string>
class Result {
 public:
  // NOLINTBEGIN(google-explicit-constructor)
  Result(T value) : value_(std::move(value)) {}

  template <class G>
  Result(Unexpected<G>&& error) : error_(std::move(error).error()) {}
  // NOLINTEND(google-explicit-constructor)

  [[nodiscard]] bool has_value() const { return value_.has_value(); }
  [[nodiscard]] explicit operator bool() const { return has_value(); }

  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  [[nodiscard]] T& operator*() { return *value_; }
  [[nodiscard]] const T& operator*() const { return *value_; }

  [[nodiscard]] T* operator->()
    requires ResultArrowTarget<T>
  {
    return std::addressof(*value_);
  }
  [[nodiscard]] const T* operator->() const
    requires ResultArrowTarget<T>
  {
    return std::addressof(*value_);
  }
  // NOLINTEND(bugprone-unchecked-optional-access)

  [[nodiscard]] E& error() { return error_; }
  [[nodiscard]] const E& error() const { return error_; }

 private:
  std::optional<T> value_;
  E error_{};
};

template <class E>
class Result<void, E> {
 public:
  Result() = default;

  // NOLINTBEGIN(google-explicit-constructor)
  template <class G>
  Result(Unexpected<G>&& error) : has_value_(false), error_(std::move(error).error()) {}
  // NOLINTEND(google-explicit-constructor)

  [[nodiscard]] bool has_value() const { return has_value_; }
  [[nodiscard]] explicit operator bool() const { return has_value(); }

  [[nodiscard]] E& error() { return error_; }
  [[nodiscard]] const E& error() const { return error_; }

 private:
  bool has_value_{true};
  E error_{};
};

template <class T>
Result(T) -> Result<T>;

template <class E>
Result(Unexpected<E>&&) -> Result<void, E>;

template <class T, class E>
[[nodiscard]] T&& unwrap(Result<T, E>&& result) {
  return std::move(*result);
}

#define REQUIRED_OR_RETURN(result)              \
  do {                                          \
    if (!(result)) {                            \
      return make_unexpected((result).error()); \
    }                                           \
  } while (false)

}  // namespace TENG_NAMESPACE
