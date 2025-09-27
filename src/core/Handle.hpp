#pragma once

#include <cstdint>

template <typename HandleT>
struct GenerationalHandle {
  GenerationalHandle() = default;

  explicit GenerationalHandle(uint32_t idx, uint32_t gen) : idx_(idx), gen_(gen) {}

  [[nodiscard]] bool is_valid() const { return gen_ != 0; }

  [[nodiscard]] uint32_t get_gen() const { return gen_; }
  [[nodiscard]] uint32_t get_idx() const { return idx_; }
  friend bool operator!=(const GenerationalHandle& a, const GenerationalHandle& b) {
    return a.idx_ != b.idx_ || a.gen_ != b.gen_;
  }
  friend bool operator==(const GenerationalHandle& a, const GenerationalHandle& b) {
    return a.idx_ == b.idx_ && a.gen_ == b.gen_;
  }

  template <typename, typename>
  friend struct Pool;

 private:
  uint32_t idx_{};
  uint32_t gen_{};
};
