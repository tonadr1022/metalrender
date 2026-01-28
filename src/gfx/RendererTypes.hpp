#pragma once

#include "core/Handle.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep

struct ModelGPUResources;
struct ModelInstanceGPUResources;

using ModelGPUHandle = GenerationalHandle<ModelGPUResources>;
using ModelInstanceGPUHandle = GenerationalHandle<ModelInstanceGPUResources>;

using UntypedDeleterFuncPtr = void (*)(void*);

struct ModelInstance;
using ModelHandle = GenerationalHandle<ModelInstance>;
