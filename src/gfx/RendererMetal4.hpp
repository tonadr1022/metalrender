#pragma once

#include <Metal/Metal.hpp>
#include <filesystem>
#include <functional>

#include "core/Math.hpp"  // IWYU pragma: keep
#include "gfx/Config.hpp"
#include "gfx/ModelInstance.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "hlsl/shared_indirect.h"

class MetalDevice;
class WindowApple;

namespace rhi {
class CmdEncoder;
}

namespace gfx {

struct RenderArgs {
  glm::mat4 view_mat;
  glm::vec3 camera_pos;
  bool draw_imgui;
};

struct MyMeshOld {
  rhi::BufferHandleHolder vertex_buf;
  rhi::BufferHandleHolder index_buf;
  size_t material_id;
  size_t vertex_count;
  size_t index_count;
};

struct MyMesh {
  rhi::BufferHandleHolder vertex_buf;
  rhi::BufferHandleHolder index_buf;
  size_t material_id;
  size_t vertex_count;
  size_t index_count;
};

class ScratchBufferPool {
 public:
  explicit ScratchBufferPool(rhi::Device* device) : device_(device) {}
  void reset(size_t frame_idx);
  rhi::BufferHandle alloc(size_t size);

 private:
  struct PerFrame {
    std::vector<rhi::BufferHandleHolder> entries;
    std::vector<rhi::BufferHandleHolder> in_use_entries;
  };
  PerFrame frames_[k_max_frames_in_flight];
  size_t frame_idx_{};

  rhi::Device* device_;
};

class RendererMetal4 {
 public:
  // TODO: cursed
  ModelInstance out_instance;
  ModelLoadResult out_result;
  std::vector<IndexedIndirectDrawCmd> cmds;
  std::vector<InstData> instance_datas;
  struct CreateInfo {
    MetalDevice* device;
    WindowApple* window;
    std::filesystem::path resource_dir;
    std::function<void()> render_imgui_callback;
  };
  void init(const CreateInfo& cinfo);
  void render(const RenderArgs& args);
  void on_imgui() {}
  void load_model();
  ScratchBufferPool& get_scratch_buffer_pool() { return scratch_buffer_pool_.value(); }

 private:
  MetalDevice* device_{};
  WindowApple* window_{};
  rhi::PipelineHandleHolder test2_pso_;
  rhi::BufferHandleHolder all_material_buf_;
  rhi::TextureHandleHolder depth_tex_;

  rhi::BufferHandleHolder indirect_cmd_buf_;
  rhi::BufferHandleHolder instance_data_buf_;
  rhi::BufferHandleHolder all_static_vertices_buf_;
  rhi::BufferHandleHolder all_static_indices_buf_;
  size_t draw_cmd_count_{};

  size_t frame_num_{};
  size_t curr_frame_idx_{};
  std::filesystem::path resource_dir_;
  std::vector<rhi::TextureHandleHolder> all_textures_;
  std::optional<ScratchBufferPool> scratch_buffer_pool_;

  gfx::RenderGraph rg_;

  struct GPUTexUpload {
    void* data;
    rhi::TextureHandleHolder tex;
    uint32_t bytes_per_row;
  };

  std::vector<GPUTexUpload> pending_texture_uploads_;

  std::vector<MyMesh> meshes_;
  void create_render_target_textures();
  void flush_pending_texture_uploads(rhi::CmdEncoder* enc);

  std::vector<rhi::SamplerHandleHolder> samplers_;
  [[nodiscard]] uint32_t get_bindless_idx(const rhi::BufferHandleHolder& buf) const;

  rhi::TextureHandleHolder default_white_tex_;
  void add_render_graph_passes(const RenderArgs& args);
};

}  // namespace gfx
