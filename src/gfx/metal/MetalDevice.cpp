#include "MetalDevice.hpp"

#include <Metal/Metal.hpp>

void MetalDevice::init() {
  device_ = MTL::CreateSystemDefaultDevice();
  ar_pool_ = NS::AutoreleasePool::alloc()->init();
}
void MetalDevice::shutdown() {
  ar_pool_->release();
  device_->release();
}

std::unique_ptr<MetalDevice> create_metal_device() { return std::make_unique<MetalDevice>(); }
