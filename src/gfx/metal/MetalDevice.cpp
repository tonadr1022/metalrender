#include "MetalDevice.hpp"

#include <Metal/Metal.hpp>

MetalDevice::MetalDevice() : device_(MTL::CreateSystemDefaultDevice()) {}

void MetalDevice::shutdown() { device_->release(); }

std::unique_ptr<MetalDevice> create_metal_device() { return std::make_unique<MetalDevice>(); }
