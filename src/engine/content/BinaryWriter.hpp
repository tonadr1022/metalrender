#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace teng::engine::content {

class BinaryWriter {
 public:
  [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }
  [[nodiscard]] std::vector<std::byte>& bytes() { return bytes_; }
  [[nodiscard]] size_t size() const { return bytes_.size(); }

  void write_u8(uint8_t value);
  void write_u16(uint16_t value);
  void write_u32(uint32_t value);
  void write_u64(uint64_t value);
  void write_i32(int32_t value);
  void write_i64(int64_t value);
  void write_f32(float value);
  void write_bytes(std::span<const std::byte> bytes);
  void write_fixed_string(std::string_view text, size_t size);
  void align_to(size_t alignment);
  void patch_u32(size_t offset, uint32_t value);
  void patch_u64(size_t offset, uint64_t value);

 private:
  std::vector<std::byte> bytes_;
};

}  // namespace teng::engine::content
