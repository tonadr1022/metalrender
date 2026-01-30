#pragma once

#include "gfx/rhi/QueryPool.hpp"
namespace MTL4 {
class CounterHeap;
}

class MetalQueryPool : public rhi::QueryPool {
 public:
  explicit MetalQueryPool(MTL4::CounterHeap* heap) : heap_(heap) {}
  MetalQueryPool() = default;

  MTL4::CounterHeap* heap_;
};
