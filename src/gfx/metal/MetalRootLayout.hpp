#pragma once

#include <Metal/MTLGPUAddress.hpp>
#include <Metal/Metal.hpp>

#include "gfx/rhi/RootLayout.hpp"

#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>

#include "core/Config.hpp"
#include "core/Util.hpp"

namespace TENG_NAMESPACE {

namespace gfx::mtl {

struct RootLayout {
  uint32_t constants[20];
  uint32_t first_instance;
  uint32_t vertex_offset;
  MTL::GPUAddress root_cbvs[rhi::ROOT_CBV_COUNT]{};
  MTL::GPUAddress resource_table_ptr{};
  MTL::GPUAddress sampler_table_ptr{};
};

// static_assert(sizeof(RootLayout) == 128);

struct ResourceTable {
  IRDescriptorTableEntry cbvs[rhi::DESCRIPTOR_TABLE_CBV_COUNT];
  IRDescriptorTableEntry srvs[ARRAY_SIZE(rhi::DescriptorBindingTable::SRV)];
  IRDescriptorTableEntry uavs[ARRAY_SIZE(rhi::DescriptorBindingTable::UAV)];
};

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE
