#include "DeviceMetal.hpp"

#include <Metal/Metal.hpp>

void DeviceMetal::init() { device_ = MTL::CreateSystemDefaultDevice(); }

void DeviceMetal::shutdown() { device_->release(); }

std::unique_ptr<RHIDevice> create_metal_device() { return std::make_unique<DeviceMetal>(); }
