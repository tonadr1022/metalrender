#include "VulkanDeleteQueue.hpp"

#include <volk.h>

namespace TENG_NAMESPACE {

namespace gfx::vk {

void DeleteQueue::flush(size_t frame_num) {
  auto flush_queue = [&](auto& queue, auto&& destroy_fn) {
    while (!queue.empty()) {
      auto& front = queue.front();
      if (front.frame_to_delete + frames_in_flight_ <= frame_num) {
        destroy_fn(front.obj);
        queue.pop();
      } else {
        break;
      }
    }
  };

  flush_queue(semaphores_, [this](VkSemaphore sem) { vkDestroySemaphore(device_, sem, nullptr); });
  flush_queue(image_views_,
              [this](VkImageView view) { vkDestroyImageView(device_, view, nullptr); });
  flush_queue(pipelines_,
              [this](VkPipeline pipeline) { vkDestroyPipeline(device_, pipeline, nullptr); });
  flush_queue(pipeline_layouts_, [this](VkPipelineLayout pipeline_layout) {
    vkDestroyPipelineLayout(device_, pipeline_layout, nullptr);
  });
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
