#pragma once

#include <filesystem>
#include <vector>

#include "core/Config.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep

namespace TENG_NAMESPACE {

namespace vox {

class Renderer;

struct BlockTextureData {
  std::filesystem::path albedo_texname;
};

struct BlockData {
  std::string name;
  glm::vec3 color;
  uint32_t albedo_tex_idx[6] = {k_invalid_id};
  uint32_t normal_tex_idx[6] = {k_invalid_id};
  uint32_t id{k_invalid_id};
  constexpr static uint32_t k_invalid_id{UINT32_MAX};
};

class VoxelDB {
 public:
  void load(const std::filesystem::path& blocks_filepath);
  void save(const std::filesystem::path& path);
  void populate_tex_arr_indices(
      const std::unordered_map<std::string, uint32_t>& name_to_array_layer);
  void draw_imgui_edit_ui();
  [[nodiscard]] const std::vector<BlockData>& get_block_datas() const { return block_datas_; }
  [[nodiscard]] const std::vector<BlockTextureData>& get_block_texture_datas() const {
    return block_texture_datas_;
  }

 private:
  std::vector<BlockData> block_datas_;
  std::vector<BlockTextureData> block_texture_datas_;
};

}  // namespace vox

}  // namespace TENG_NAMESPACE
