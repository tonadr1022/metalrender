#pragma once

#include <cstdint>

#include "gfx/BackedGPUAllocator.hpp"
#include "offsetAllocator.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace rhi {
class Device;
}

namespace gfx {

struct BufferCopyMgr;

enum class GeometryBatchType : uint8_t {
  Static,
  Count,
};

struct GeometryBatch {
  struct CreateInfo {
    uint32_t initial_vertex_capacity;
    uint32_t initial_index_capacity;
    uint32_t initial_meshlet_capacity;
    uint32_t initial_mesh_capacity;
    uint32_t initial_meshlet_triangle_capacity;
    uint32_t initial_meshlet_vertex_capacity;
  };

  struct Stats {
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t meshlet_count;
    uint32_t meshlet_triangle_count;
    uint32_t meshlet_vertex_count;
  };

  [[nodiscard]] Stats get_stats() const;

  // void ensure_task_cmd_buf_space(rhi::Device& device, size_t element_count);

  GeometryBatch(GeometryBatchType type, rhi::Device& device, BufferCopyMgr& buffer_copier,
                const CreateInfo& cinfo);

  struct Alloc {
    OffsetAllocator::Allocation vertex_alloc;
    OffsetAllocator::Allocation index_alloc;
    OffsetAllocator::Allocation meshlet_alloc;
    OffsetAllocator::Allocation mesh_alloc;
    OffsetAllocator::Allocation meshlet_triangles_alloc;
    OffsetAllocator::Allocation meshlet_vertices_alloc;
  };

  void free(const Alloc& alloc) {
    // if (alloc.mesh_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
    //   mesh_buf.free(alloc.mesh_alloc);
    // }
    if (alloc.meshlet_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_buf.free(alloc.meshlet_alloc);
    }
    if (alloc.vertex_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      vertex_buf.free(alloc.vertex_alloc);
    }
    if (alloc.index_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      index_buf.free(alloc.index_alloc);
    }
    if (alloc.meshlet_triangles_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_triangles_buf.free(alloc.meshlet_triangles_alloc);
    }
    if (alloc.meshlet_vertices_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_vertices_buf.free(alloc.meshlet_vertices_alloc);
    }
  }

  BackedGPUAllocator vertex_buf;
  BackedGPUAllocator index_buf;
  BackedGPUAllocator meshlet_buf;
  BackedGPUAllocator mesh_buf;
  BackedGPUAllocator meshlet_triangles_buf;
  BackedGPUAllocator meshlet_vertices_buf;
  // rhi::BufferHandleHolder task_cmd_buf_;
  // rhi::BufferHandleHolder out_draw_count_buf_;
  const GeometryBatchType type;
  uint32_t task_cmd_count{};
};
}  // namespace gfx

} // namespace TENG_NAMESPACE
