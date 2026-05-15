#include "engine/content/BinaryWriter.hpp"

#include <cstring>

#include "core/EAssert.hpp"

namespace teng::engine::content {

namespace {

void write_le(std::vector<std::byte>& out, uint64_t value, size_t byte_count) {
  for (size_t i = 0; i < byte_count; ++i) {
    out.push_back(static_cast<std::byte>((value >> (i * 8u)) & 0xffu));
  }
}

void patch_le(std::vector<std::byte>& out, size_t offset, uint64_t value, size_t byte_count) {
  ASSERT(offset + byte_count <= out.size());
  for (size_t i = 0; i < byte_count; ++i) {
    out[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xffu);
  }
}

}  // namespace

void BinaryWriter::write_u8(uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

void BinaryWriter::write_u16(uint16_t value) { write_le(bytes_, value, sizeof(value)); }

void BinaryWriter::write_u32(uint32_t value) { write_le(bytes_, value, sizeof(value)); }

void BinaryWriter::write_u64(uint64_t value) { write_le(bytes_, value, sizeof(value)); }

void BinaryWriter::write_i32(int32_t value) { write_u32(static_cast<uint32_t>(value)); }

void BinaryWriter::write_i64(int64_t value) { write_u64(static_cast<uint64_t>(value)); }

void BinaryWriter::write_f32(float value) {
  uint32_t bits{};
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(value));
  write_u32(bits);
}

void BinaryWriter::write_bytes(std::span<const std::byte> bytes) {
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

void BinaryWriter::write_fixed_string(std::string_view text, size_t size) {
  ASSERT(text.size() <= size);
  const auto* begin = reinterpret_cast<const std::byte*>(text.data());
  bytes_.insert(bytes_.end(), begin, begin + text.size());
  bytes_.insert(bytes_.end(), size - text.size(), std::byte{0});
}

void BinaryWriter::align_to(size_t alignment) {
  ASSERT(alignment > 0);
  const size_t remainder = bytes_.size() % alignment;
  if (remainder != 0) {
    bytes_.insert(bytes_.end(), alignment - remainder, std::byte{0});
  }
}

void BinaryWriter::patch_u32(size_t offset, uint32_t value) {
  patch_le(bytes_, offset, value, sizeof(value));
}

void BinaryWriter::patch_u64(size_t offset, uint64_t value) {
  patch_le(bytes_, offset, value, sizeof(value));
}

}  // namespace teng::engine::content
