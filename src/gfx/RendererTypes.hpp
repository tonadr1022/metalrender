#pragma once
#include "core/Handle.hpp"

struct ModelGPUResources;
struct ModelInstanceGPUResources;

using ModelGPUHandle = GenerationalHandle<ModelGPUResources>;
using ModelInstanceGPUHandle = GenerationalHandle<ModelInstanceGPUResources>;
