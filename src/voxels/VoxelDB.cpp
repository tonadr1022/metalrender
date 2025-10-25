#include "VoxelDB.hpp"

#include <stb_image/stb_image.h>

#include <fstream>
#include <sstream>

#include "VoxelRenderer.hpp"
#include "core/Logger.hpp"
#include "imgui.h"

namespace vox {

void VoxelDB::save(const std::filesystem::path& path) {
  std::ofstream file(path);
  if (file.is_open()) {
    for (size_t i = 0; i < block_datas_.size(); i++) {
      const BlockData& b = block_datas_[i];
      const BlockTextureData& tex_data = block_texture_datas_[i];
      file << b.id << ' ' << b.name << ' ' << b.color.x << ' ' << b.color.y << ' ' << b.color.z
           << ' ' << tex_data.albedo_texname << '\n';
    }
  }
}

void VoxelDB::load(const std::filesystem::path& blocks_filepath) {
  {  // block data
    std::ifstream file(blocks_filepath);
    block_datas_.clear();
    if (file.is_open()) {
      std::string line;
      while (std::getline(file, line)) {
        BlockData b;
        BlockTextureData texture_data;
        std::stringstream toks{line};
        toks >> b.id >> b.name >> b.color.x >> b.color.y >> b.color.z >>
            texture_data.albedo_texname;
        block_datas_.emplace_back(std::move(b));
        block_texture_datas_.emplace_back(std::move(texture_data));
      }
    } else {
      LCRITICAL("Failed to load block data file: {}", blocks_filepath.string());
    }
  }
}

void VoxelDB::draw_imgui_edit_ui() {
  for (BlockData& b : block_datas_) {
    ImGui::ColorEdit3(b.name.c_str(), &b.color.x);
  }
}

void VoxelDB::populate_tex_arr_indices(
    const std::unordered_map<std::string, uint32_t>& name_to_array_layer) {
  for (size_t i = 0; i < block_texture_datas_.size(); i++) {
    auto& tex_data = block_texture_datas_[i];
    auto& b = block_datas_[i];
    auto it = name_to_array_layer.find(tex_data.albedo_texname);
    if (it != name_to_array_layer.end()) {
      for (auto& i : b.albedo_tex_idx) {
        i = it->second;
      }
    } else {
      LERROR("missing albedo texture for block: {}", b.name);
    }
    auto normal_it = name_to_array_layer.find(tex_data.albedo_texname.stem().string() + "_n" +
                                              tex_data.albedo_texname.extension().string());
    if (normal_it != name_to_array_layer.end()) {
      for (auto& i : b.normal_tex_idx) {
        i = normal_it->second;
      }
    } else {
      LERROR("missing normal texture for block: {}", b.name);
    }
  }
}

}  // namespace vox
