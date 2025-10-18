#include "Stats.hpp"

#include <numeric>
namespace util {

float RollingAvgCtr::avg() const {
  return vals_.empty() ? 0 : std::accumulate(vals_.begin(), vals_.end(), 0.0) / vals_.size();
}

}  // namespace util
