#include "DrawBatch.hpp"

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "hlsl/default_vertex.h"
#include "hlsl/shared_mesh_data.h"

namespace TENG_NAMESPACE {

namespace gfx {

// TODO: coalesce buffer descs

GeometryBatch::GeometryBatch(GeometryBatchType type, rhi::Device& device,
                             BufferCopyMgr& buffer_copier, const CreateInfo& cinfo)
    : vertex_buf(device, buffer_copier,
                 {
                     .size = cinfo.initial_vertex_capacity * sizeof(DefaultVertex),
                     // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                     .name = "vertex buf",
                 },
                 sizeof(DefaultVertex)),
      index_buf(device, buffer_copier,

                {
                    .size = cinfo.initial_index_capacity * sizeof(rhi::DefaultIndexT),
                    // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                    .name = "index buf",
                },
                sizeof(rhi::DefaultIndexT)),
      meshlet_buf(device, buffer_copier,

                  {
                      .size = cinfo.initial_meshlet_capacity * sizeof(Meshlet),
                      // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                      .name = "meshlet buf",
                  },
                  sizeof(Meshlet)),
      mesh_buf(device, buffer_copier,

               {
                   .size = cinfo.initial_mesh_capacity * sizeof(MeshData),
                   // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                   .name = "mesh buf",
               },
               sizeof(MeshData)),
      meshlet_triangles_buf(device, buffer_copier,

                            {
                                .size = cinfo.initial_meshlet_triangle_capacity * sizeof(uint8_t),
                                // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                                .name = "meshlet_triangles_buf",
                            },
                            sizeof(uint8_t)),
      meshlet_vertices_buf(device, buffer_copier,

                           {
                               .size = cinfo.initial_meshlet_vertex_capacity * sizeof(uint32_t),
                               // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                               .name = "meshlet_vertices_buf",
                           },
                           sizeof(uint32_t)),
      type(type) {
  // out_draw_count_buf_ = device.create_buf_h({
  //     .storage_mode = rhi::StorageMode::GPUOnly,
  //     .usage = rhi::BufferUsage_Storage,
  //     .size = sizeof(uint32_t) * 3,
  //     .name = "out_draw_count_buf",
  // });
}

GeometryBatch::Stats GeometryBatch::get_stats() const {
  return {
      .vertex_count = vertex_buf.allocated_element_count(),
      .index_count = index_buf.allocated_element_count(),
      .meshlet_count = meshlet_buf.allocated_element_count(),
      .meshlet_triangle_count = meshlet_triangles_buf.allocated_element_count(),
      .meshlet_vertex_count = meshlet_vertices_buf.allocated_element_count(),
  };
}

// void DrawBatch::ensure_task_cmd_buf_space(rhi::Device&, size_t) {
// if (element_count == 0) {
//   return;
// }
// auto required_size = element_count * sizeof(TaskCmd);
// if (task_cmd_buf_.is_valid() && device.get_buf(task_cmd_buf_)->desc().size >= required_size) {
//   return;
// }
// task_cmd_buf_ = device.create_buf_h({
//     .storage_mode = rhi::StorageMode::GPUOnly,
//     .size = required_size,
//     .name = "task_cmd_buf",
// });
// }

}  // namespace gfx

}  // namespace TENG_NAMESPACE
