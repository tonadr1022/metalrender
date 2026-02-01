#pragma once

#include <cstdint>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

enum class QueueType : uint8_t {
  Graphics,
  Compute,
  Copy,
  Count,
};

inline const char* to_string(QueueType type) {
  switch (type) {
    case QueueType::Graphics:
      return "Graphics";
    case QueueType::Compute:
      return "Compute";
    case QueueType::Copy:
      return "Copy";
    default:
      return "Unknown";
  }
}

}  // namespace rhi

}  // namespace TENG_NAMESPACE
