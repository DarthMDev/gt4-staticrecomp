#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace gt4::vulkan {

enum class Aspect { widescreen, standard };

struct PresentationRect {
  std::uint32_t left, top, width, height;
};

Aspect parse_aspect(const char *value);
PresentationRect presentation_rect(std::uint32_t width, std::uint32_t height,
                                   Aspect aspect);

struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void *mapped = nullptr;
  VkDeviceSize size = 0;
};

struct Dispatch {
  std::uint32_t pipeline, x, y, parameter;
};

// One queue and completed submissions keep host writes and buffer lifetimes
// explicit.
class Context {
public:
  ~Context();
  void initialize(const std::filesystem::path &shaders);
  Buffer *create_buffer(std::uint64_t bytes);
  void destroy_buffer(Buffer *buffer);
  void write(Buffer *buffer, std::uint64_t offset, const void *source,
             std::uint64_t bytes);
  void read(Buffer *buffer, std::uint64_t offset, void *destination,
            std::uint64_t bytes);
  void bind_buffers(const std::array<Buffer *, 8> &buffers);
  void execute(const Dispatch *commands, std::size_t count);
  void wait_idle();
  void attach_window(HWND window, std::uint32_t width, std::uint32_t height);
  void resize(std::uint32_t width, std::uint32_t height);
  void set_aspect(Aspect aspect) { aspect_ = aspect; }
  bool present(Buffer *rgba, std::uint32_t width, std::uint32_t height);
  bool has_window() const { return window_ != nullptr; }
  std::uint64_t presentations() const { return presentations_; }

private:
  void begin_commands();
  void finish_commands(VkSemaphore wait = VK_NULL_HANDLE,
                       VkSemaphore signal = VK_NULL_HANDLE);
  void memory_barrier(VkPipelineStageFlags source,
                      VkPipelineStageFlags destination, VkAccessFlags writes,
                      VkAccessFlags reads);
  void rebuild_swapchain();
  void destroy_image();
  void image_barrier(VkImage image, VkImageLayout old_layout,
                     VkImageLayout new_layout, VkPipelineStageFlags source,
                     VkPipelineStageFlags destination, VkAccessFlags writes,
                     VkAccessFlags reads);

  HMODULE loader_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queue_family_ = 0;
  VkPhysicalDeviceMemoryProperties memory_properties_{};
  VkPhysicalDeviceLimits limits_{};
  std::vector<Buffer *> buffers_;
  std::uint64_t parameter_bytes_ = 0;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
  std::array<VkPipeline, 9> pipelines_{};
  HWND window_ = nullptr;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkSemaphore acquired_ = VK_NULL_HANDLE, rendered_ = VK_NULL_HANDLE;
  VkFence present_fence_ = VK_NULL_HANDLE;
  VkImage display_image_ = VK_NULL_HANDLE;
  VkDeviceMemory display_memory_ = VK_NULL_HANDLE;
  std::uint32_t image_width_ = 0, image_height_ = 0;
  std::uint32_t window_width_ = 0, window_height_ = 0;
  VkExtent2D extent_{};
  Aspect aspect_ = Aspect::widescreen;
  bool resized_ = false;
  std::uint64_t presentations_ = 0;
  std::uint64_t zero_dispatches_ = 0;
};

} // namespace gt4::vulkan
