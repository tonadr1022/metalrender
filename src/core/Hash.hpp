#pragma once
#include <functional>
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
}  // namespace util::hash

}  // namespace TENG_NAMESPACE
