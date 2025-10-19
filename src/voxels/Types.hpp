#pragma once

#include "core/Handle.hpp"
#include "core/Math.hpp"

class Chunk;
using ChunkKey = glm::ivec3;
using ChunkHandle = GenerationalHandle<Chunk>;
