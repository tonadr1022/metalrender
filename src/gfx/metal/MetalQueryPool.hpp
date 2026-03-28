#pragma once

#include <Metal/Metal.hpp>
#include <utility>

#include "core/Config.hpp"
#include "gfx/rhi/QueryPool.hpp"

namespace MTL4 {
class CounterHeap;
}

namespace TENG_NAMESPACE {
namespace gfx::mtl {

class QueryPool : public rhi::QueryPool {
 public:
  explicit QueryPool(NS::SharedPtr<MTL4::CounterHeap> heap) : heap_(std::move(heap)) {}
  QueryPool() = default;

  NS::SharedPtr<MTL4::CounterHeap> heap_;
};

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE