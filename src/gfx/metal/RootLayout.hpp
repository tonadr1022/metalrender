#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {

constexpr uint32_t ROOT_CBV_COUNT = 3;
constexpr uint32_t DESCRIPTOR_TABLE_CBV_COUNT = 9;
constexpr uint32_t TOTAL_CBV_BINDINGS = ROOT_CBV_COUNT + DESCRIPTOR_TABLE_CBV_COUNT;
constexpr uint32_t TOTAL_SRV_BINDINGS = 12;
constexpr uint32_t TOTAL_UAV_BINDINGS = 12;

struct DescriptorBindingTable {
  rhi::BufferHandle CBV[TOTAL_CBV_BINDINGS] = {};
  int CBV_offsets[TOTAL_CBV_BINDINGS] = {};
  // uint64_t version of rhi::BufferHandle/TextureHandle
  uint64_t SRV[TOTAL_SRV_BINDINGS] = {};
  // -2 == buffer, -1 == texture, >= 0 == texture view;
  int SRV_subresources[TOTAL_SRV_BINDINGS] = {};
  static constexpr int k_tex_resource = -1;
  static constexpr int k_buffer_resource = -2;
  int SRV_offsets[TOTAL_SRV_BINDINGS] = {};
  uint64_t UAV[TOTAL_UAV_BINDINGS] = {};
  // -2 == buffer, -1 == texture, >= 0 == texture view;
  int UAV_subresources[TOTAL_SRV_BINDINGS] = {};
  int UAV_offsets[TOTAL_SRV_BINDINGS] = {};
};

}  // namespace TENG_NAMESPACE
