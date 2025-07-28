#include "DeviceMetal.hpp"

#include <Metal/Metal.hpp>

void DeviceMetal::init() { device_ = MTL::CreateSystemDefaultDevice(); }

void DeviceMetal::shutdown() { device_->release(); }
