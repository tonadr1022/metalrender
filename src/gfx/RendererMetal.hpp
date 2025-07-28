#pragma once

class DeviceMetal;

class RendererMetal {
 public:
  struct CreateInfo {
    DeviceMetal* device;
  };
  explicit RendererMetal(const CreateInfo& cinfo);

 private:
  DeviceMetal* device_{};
};
