#pragma once

#include <cstdint>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

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
  template <typename, typename>
  friend struct BlockPool;

  [[nodiscard]] uint64_t to64() const {
    return static_cast<uint64_t>(gen_) << 32 | static_cast<uint64_t>(idx_);
  }

  explicit GenerationalHandle(uint64_t packed)
      : idx_(packed & UINT32_MAX), gen_((packed >> 32) & UINT32_MAX) {}

 private:
  uint32_t idx_{};
  uint32_t gen_{};
};

inline bool generational_handle_u64_is_valid(uint64_t packed) { return packed >> 32 != 0; }

} // namespace TENG_NAMESPACE
