#pragma once

class RHIDevice;

class Window {
 public:
  virtual ~Window() = default;
  [[nodiscard]] virtual bool should_close() const = 0;
  virtual void poll_events() = 0;
  virtual void init(RHIDevice* device) = 0;
  virtual void shutdown() = 0;

 private:
};
