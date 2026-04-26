#pragma once

#include <array>
#include <vector>

#include "core/Config.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

enum class DrawCullPhase : uint8_t { Early = 0, Late = 1, Count = 2 };

struct TaskCmdBufRgIdsByAlphaMask {
  std::array<RGResourceId, AlphaMaskType::Count> ids{};

  RGResourceId& operator[](AlphaMaskType t) { return ids[static_cast<size_t>(t)]; }
  const RGResourceId& operator[](AlphaMaskType t) const { return ids[static_cast<size_t>(t)]; }
};

struct TaskCmdBufRgIdsPerView {
  std::array<TaskCmdBufRgIdsByAlphaMask, static_cast<size_t>(DrawCullPhase::Count)> by_phase{};

  [[nodiscard]] TaskCmdBufRgIdsByAlphaMask& phase(DrawCullPhase p) {
    return by_phase[static_cast<size_t>(p)];
  }
  [[nodiscard]] const TaskCmdBufRgIdsByAlphaMask& phase(DrawCullPhase p) const {
    return by_phase[static_cast<size_t>(p)];
  }
};

using TaskCmdBufRgTable = std::vector<TaskCmdBufRgIdsPerView>;

}  // namespace gfx

}  // namespace TENG_NAMESPACE
