#include "engine/content/BinaryReader.hpp"

#include <cstring>
#include <limits>

namespace teng::engine::content {

namespace {

[[nodiscard]] Result<void> ensure_available(size_t size, size_t position, size_t byte_count) {
  if (position > size || byte_count > size - position) {
    return make_unexpected("binary read is out of bounds");
  }
  return {};
}

}  // namespace

Result<void> BinaryReader::seek(size_t position) {
  if (position > bytes_.size()) {
    return make_unexpected("binary seek is out of bounds");
  }
  position_ = position;
  return {};
}

Result<void> BinaryReader::skip(size_t byte_count) {
  Result<void> available = ensure_available(bytes_.size(), position_, byte_count);
  REQUIRED_OR_RETURN(available);
  position_ += byte_count;
  return {};
}

Result<std::span<const std::byte>> BinaryReader::read_bytes(size_t byte_count) {
  Result<void> available = ensure_available(bytes_.size(), position_, byte_count);
  REQUIRED_OR_RETURN(available);
  const std::span<const std::byte> out = bytes_.subspan(position_, byte_count);
  position_ += byte_count;
  return out;
}

Result<uint8_t> BinaryReader::read_u8() {
  Result<std::span<const std::byte>> bytes = read_bytes(sizeof(uint8_t));
  REQUIRED_OR_RETURN(bytes);
  return static_cast<uint8_t>((*bytes)[0]);
}

Result<uint16_t> BinaryReader::read_u16() {
  Result<std::span<const std::byte>> bytes = read_bytes(sizeof(uint16_t));
  REQUIRED_OR_RETURN(bytes);
  uint16_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint16_t>(static_cast<uint8_t>((*bytes)[i])) << (i * 8u);
  }
  return value;
}

Result<uint32_t> BinaryReader::read_u32() {
  Result<std::span<const std::byte>> bytes = read_bytes(sizeof(uint32_t));
  REQUIRED_OR_RETURN(bytes);
  uint32_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint32_t>(static_cast<uint8_t>((*bytes)[i])) << (i * 8u);
  }
  return value;
}

Result<uint64_t> BinaryReader::read_u64() {
  Result<std::span<const std::byte>> bytes = read_bytes(sizeof(uint64_t));
  REQUIRED_OR_RETURN(bytes);
  uint64_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint64_t>(static_cast<uint8_t>((*bytes)[i])) << (i * 8u);
  }
  return value;
}

Result<int32_t> BinaryReader::read_i32() {
  Result<uint32_t> value = read_u32();
  REQUIRED_OR_RETURN(value);
  return static_cast<int32_t>(*value);
}

Result<int64_t> BinaryReader::read_i64() {
  Result<uint64_t> value = read_u64();
  REQUIRED_OR_RETURN(value);
  return static_cast<int64_t>(*value);
}

Result<float> BinaryReader::read_f32() {
  Result<uint32_t> bits = read_u32();
  REQUIRED_OR_RETURN(bits);
  float value{};
  static_assert(sizeof(value) == sizeof(*bits));
  std::memcpy(&value, &*bits, sizeof(value));
  return value;
}

uint8_t BinaryReader::read_u8_unchecked() { return static_cast<uint8_t>(bytes_[position_++]); }

uint16_t BinaryReader::read_u16_unchecked() {
  uint16_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint16_t>(static_cast<uint8_t>(bytes_[position_++])) << (i * 8u);
  }
  return value;
}

uint32_t BinaryReader::read_u32_unchecked() {
  uint32_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes_[position_++])) << (i * 8u);
  }
  return value;
}

uint64_t BinaryReader::read_u64_unchecked() {
  uint64_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes_[position_++])) << (i * 8u);
  }
  return value;
}

int32_t BinaryReader::read_i32_unchecked() { return static_cast<int32_t>(read_u32_unchecked()); }

int64_t BinaryReader::read_i64_unchecked() { return static_cast<int64_t>(read_u64_unchecked()); }

float BinaryReader::read_f32_unchecked() { return static_cast<float>(read_u32_unchecked()); }

Result<std::string> BinaryReader::read_fixed_string(size_t size) {
  Result<std::span<const std::byte>> bytes = read_bytes(size);
  REQUIRED_OR_RETURN(bytes);
  std::string text;
  text.reserve(size);
  for (const std::byte byte : *bytes) {
    const char c = static_cast<char>(byte);
    if (c == '\0') {
      break;
    }
    text.push_back(c);
  }
  return text;
}

Result<std::span<const std::byte>> checked_subspan(std::span<const std::byte> bytes,
                                                   uint64_t offset, uint64_t size) {
  if (offset > std::numeric_limits<size_t>::max() || size > std::numeric_limits<size_t>::max()) {
    return make_unexpected("binary section range is too large");
  }
  const auto local_offset = static_cast<size_t>(offset);
  const auto local_size = static_cast<size_t>(size);
  Result<void> available = ensure_available(bytes.size(), local_offset, local_size);
  REQUIRED_OR_RETURN(available);
  return bytes.subspan(local_offset, local_size);
}

}  // namespace teng::engine::content
