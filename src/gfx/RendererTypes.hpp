#pragma once

#include "core/Handle.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

struct ModelGPUResources;
struct ModelInstanceGPUResources;

using ModelGPUHandle = GenerationalHandle<ModelGPUResources>;
using ModelInstanceGPUHandle = GenerationalHandle<ModelInstanceGPUResources>;

using UntypedDeleterFuncPtr = void (*)(void*);

struct ModelInstance;
using ModelHandle = GenerationalHandle<ModelInstance>;

} // namespace TENG_NAMESPACE
