#pragma once

class MetalDevice;

class RendererMetal {
 public:
  struct CreateInfo {
    MetalDevice* device;
  };
  explicit RendererMetal(const CreateInfo& cinfo);

 private:
  [[maybe_unused]] MetalDevice* device_{};
};
