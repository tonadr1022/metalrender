#pragma once
#include <functional>
#include <string_view>
#include <tuple>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

// src: https://github.com/JuanDiegoMontoya/Frogfood/blob/main/src/Fvog/detail/Hash2.h

namespace util::hash {

template <typename T>
struct tuple_hash;

template <class T>
inline void hash_combine(auto& seed, const T& v) {
  static_assert(std::is_default_constructible_v<std::hash<T>>, "Type must be hashable");
  seed ^= std::hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <class Tuple, size_t Index = std::tuple_size_v<Tuple> - 1>
struct HashValueImpl {
  static void apply(size_t& seed, const Tuple& tuple) {
    HashValueImpl<Tuple, Index - 1>::apply(seed, tuple);
    hash_combine(seed, std::get<Index>(tuple));
  }
};

template <class Tuple>
struct HashValueImpl<Tuple, 0> {
  static void apply(size_t& seed, const Tuple& tuple) { hash_combine(seed, std::get<0>(tuple)); }
};

template <typename... TT>
struct tuple_hash<std::tuple<TT...>> {
  size_t operator()(const std::tuple<TT...>& tt) const {
    size_t seed = 0;
    HashValueImpl<std::tuple<TT...>>::apply(seed, tt);
    return seed;
  }
};

constexpr uint32_t fnv1a_32(const char* s, std::size_t count) {
  return ((count ? fnv1a_32(s, count - 1) : 2166136261u) ^ s[count]) * 16777619u;
}

constexpr size_t str_len(const char* s) {
  size_t size = 0;
  while (s[size]) {
    size++;
  }
  return size;
}

struct HashedString {
  uint32_t hash_value;

  // explicit keyword
  // NOLINTBEGIN
  constexpr HashedString(uint32_t hash) noexcept : hash_value(hash) {}
  constexpr HashedString(const char* s) noexcept : hash_value(0) {
    hash_value = fnv1a_32(s, str_len(s));
  }
  constexpr HashedString(const char* s, std::size_t cnt) noexcept : hash_value(0) {
    hash_value = fnv1a_32(s, cnt);
  }
  constexpr HashedString(std::string_view s) noexcept : hash_value(0) {
    hash_value = fnv1a_32(s.data(), s.size());
  }
  constexpr operator uint32_t() const noexcept { return hash_value; }
  // explicit keyword
  // NOLINTEND
};

}  // namespace util::hash

}  // namespace TENG_NAMESPACE
