#pragma once

#include <Metal/Metal.hpp>
#include <utility>

#include "gfx/rhi/QueryPool.hpp"
namespace MTL4 {
class CounterHeap;
}

class MetalQueryPool : public rhi::QueryPool {
 public:
  explicit MetalQueryPool(NS::SharedPtr<MTL4::CounterHeap> heap) : heap_(std::move(heap)) {}
  MetalQueryPool() = default;

  NS::SharedPtr<MTL4::CounterHeap> heap_;
};
