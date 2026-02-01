#pragma once

#include <vector>

#include "core/EAssert.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace util {

class RollingAvgCtr {
 public:
  explicit RollingAvgCtr(size_t history_size) : history_size_(history_size) {
    ASSERT(history_size > 0);
  }

  void add(float val) {
    vals_.push_back(val);
    if (vals_.size() > history_size_) {
      vals_.erase(vals_.begin());
    }
  }

  [[nodiscard]] float avg() const;

 private:
  std::vector<float> vals_;
  size_t history_size_;
};

}  // namespace util

} // namespace TENG_NAMESPACE
