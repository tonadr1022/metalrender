#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "core/Result.hpp"

namespace teng::engine::content {

class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] size_t position() const { return position_; }
  [[nodiscard]] size_t size() const { return bytes_.size(); }
  [[nodiscard]] std::span<const std::byte> bytes() const { return bytes_; }
  [[nodiscard]] Result<void> seek(size_t position);
  [[nodiscard]] Result<void> skip(size_t byte_count);
  [[nodiscard]] Result<std::span<const std::byte>> read_bytes(size_t byte_count);
  [[nodiscard]] Result<uint8_t> read_u8();
  [[nodiscard]] Result<uint16_t> read_u16();
  [[nodiscard]] Result<uint32_t> read_u32();
  [[nodiscard]] Result<uint64_t> read_u64();
  [[nodiscard]] Result<int32_t> read_i32();
  [[nodiscard]] Result<float> read_f32();
  [[nodiscard]] Result<std::string> read_fixed_string(size_t size);

 private:
  std::span<const std::byte> bytes_;
  size_t position_{};
};

[[nodiscard]] Result<std::span<const std::byte>> checked_subspan(
    std::span<const std::byte> bytes, uint64_t offset, uint64_t size);

}  // namespace teng::engine::content
