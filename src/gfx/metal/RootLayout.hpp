#pragma once

#include <Metal/MTLGPUAddress.hpp>
#include <Metal/Metal.hpp>
#include <cstddef>
#include <cstdint>

#include "core/Util.hpp"
#include "gfx/RendererTypes.hpp"

#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>

constexpr size_t ROOT_CBV_COUNT = 0;
constexpr size_t DESCRIPTOR_TABLE_CBV_COUNT = 12;
constexpr size_t TOTAL_CBV_BINDINGS = ROOT_CBV_COUNT + DESCRIPTOR_TABLE_CBV_COUNT;
constexpr size_t TOTAL_SRV_BINDINGS = 12;
constexpr size_t TOTAL_UAV_BINDINGS = 12;

struct RootLayout {
  union Constants {
    struct {
      uint32_t constants[20];
      uint32_t draw_id;
      uint32_t base_vertex;
    } gfx_constants;
    uint32_t constants[22];
  } constants;
  // MTL::GPUAddress root_cbvs[ROOT_CBV_COUNT]{};
  MTL::GPUAddress resource_table_ptr{};
  MTL::GPUAddress sampler_table_ptr{};
};

struct DescriptorBindingTable {
  rhi::BufferHandle CBV[TOTAL_CBV_BINDINGS] = {};
  rhi::TextureHandle SRV[TOTAL_SRV_BINDINGS] = {};
  int SRV_subresources[TOTAL_SRV_BINDINGS] = {};
  size_t UAV[TOTAL_UAV_BINDINGS] = {};
};

struct ResourceTable {
  IRDescriptorTableEntry cbvs[DESCRIPTOR_TABLE_CBV_COUNT];
  IRDescriptorTableEntry srvs[ARRAY_SIZE(DescriptorBindingTable::SRV)];
  IRDescriptorTableEntry uavs[ARRAY_SIZE(DescriptorBindingTable::UAV)];
};
