#pragma once

#include <vulkan/vulkan_core.h>

#include <queue>

#include "VMAWrapper.hpp"
#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

struct DeleteQueue {
  template <typename T>
  struct Entry {
    T obj;
    size_t frame_to_delete;
  };

  void init(VkDevice device, VmaAllocator allocator, size_t frames_in_flight) {
    device_ = device;
    allocator_ = allocator;
    frames_in_flight_ = frames_in_flight;
  }
  void flush(size_t frame_num);
  void set_curr_frame(size_t curr_frame) { curr_frame_ = curr_frame; }
  void enqueue(VkSemaphore entry) { semaphores_.emplace(entry, curr_frame_); }
  void enqueue(VkImageView entry) { image_views_.emplace(entry, curr_frame_); }
  void enqueue(VkPipeline entry) { pipelines_.emplace(entry, curr_frame_); }
  void enqueue(VkPipelineLayout entry) { pipeline_layouts_.emplace(entry, curr_frame_); }

  struct ImgEntry {
    VkImage image;
    VmaAllocation allocation;
  };
  void enqueue(ImgEntry entry) { images_.emplace(entry, curr_frame_); }

 private:
  VmaAllocator allocator_{};
  VkDevice device_{};
  size_t curr_frame_{};
  size_t frames_in_flight_{};
  std::queue<Entry<VkSemaphore>> semaphores_;
  std::queue<Entry<VkImageView>> image_views_;
  std::queue<Entry<VkPipeline>> pipelines_;
  std::queue<Entry<VkPipelineLayout>> pipeline_layouts_;
  std::queue<Entry<ImgEntry>> images_;
};

}  // namespace gfx::vk
}  // namespace TENG_NAMESPACE
