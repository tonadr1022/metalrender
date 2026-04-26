#pragma once

#include <span>

#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/BufferSuballoc.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/Texture.hpp"
#include "glm/ext/vector_int2.hpp"

namespace teng::gfx {

class InstanceMgr;
class RenderGraph;
struct GeometryBatch;

namespace rhi {
class CmdEncoder;
class Device;
}  // namespace rhi

// Mirrors `encode_meshlet_mesh_draw_pass` in DrawPassSceneBindings.cpp; uses explicit task flags
// so callers can compose main, CSM, and future shadow passes without renderer CVars.
void encode_meshlet_test_draw_pass(
    bool reverse_z, bool late_pass, uint32_t meshlet_task_flags, rhi::Device* device,
    RenderGraph& rg, const GeometryBatch& batch, rhi::BufferHandle materials_buf,
    const BufferSuballoc& globals_cb, const BufferSuballoc& view_cb, const BufferSuballoc& cull_cb,
    rhi::TextureHandle depth_pyramid_tex, glm::ivec2 viewport_dims, RGResourceId meshlet_vis_rg,
    RGResourceId meshlet_stats_rg, RGResourceId task_cmd_rg, rhi::BufferHandle indirect_buf,
    InstanceMgr& inst_mgr, std::span<const rhi::PipelineHandleHolder> psos, rhi::CmdEncoder* enc);

}  // namespace teng::gfx
