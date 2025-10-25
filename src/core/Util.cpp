#include "Util.hpp"

#include <algorithm>

std::string binary_rep(size_t val) {
  int bits = 64;
  std::string res;
  res.reserve(bits);
  while (bits) {
    res += std::to_string(val & 1);
    val = val >> 1;
    bits--;
  }
  std::ranges::reverse(res);
  return res;
}
