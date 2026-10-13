#include "native_vulkan.hpp"

#include "perf_probe.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gt4::vulkan {

Aspect parse_aspect(const char *value) {
  if (!value || std::strcmp(value, "16:9") == 0)
    return Aspect::widescreen;
  if (std::strcmp(value, "4:3") == 0)
    return Aspect::standard;
  throw std::runtime_error("GT4_ASPECT must be 16:9 or 4:3");
}

PresentationRect presentation_rect(std::uint32_t width, std::uint32_t height,
                                   Aspect aspect) {
  if (!width || !height)
    return {0, 0, 0, 0};
  const std::uint64_t numerator = aspect == Aspect::widescreen ? 16 : 4;
  const std::uint64_t denominator = aspect == Aspect::widescreen ? 9 : 3;
  const auto fitted_width = static_cast<std::uint32_t>(std::max<std::uint64_t>(
      1, std::min<std::uint64_t>(width, height * numerator / denominator)));
  const auto fitted_height = static_cast<std::uint32_t>(std::max<std::uint64_t>(
      1, std::min<std::uint64_t>(height, width * denominator / numerator)));
  return {(width - fitted_width) / 2, (height - fitted_height) / 2,
          fitted_width, fitted_height};
}

namespace {

// Only the Vulkan entry points used by this backend are resolved from the
// loader.
// clang-format off
#define VULKAN_FUNCTIONS(X) \
  X(vkCreateInstance) \
  X(vkEnumeratePhysicalDevices) \
  X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceMemoryProperties) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkCreateDevice) \
  X(vkCreateBuffer) \
  X(vkGetBufferMemoryRequirements) \
  X(vkAllocateMemory) \
  X(vkBindBufferMemory) \
  X(vkMapMemory) \
  X(vkDestroyBuffer) \
  X(vkFreeMemory) \
  X(vkCreateCommandPool) \
  X(vkAllocateCommandBuffers) \
  X(vkResetCommandBuffer) \
  X(vkEndCommandBuffer) \
  X(vkQueueSubmit) \
  X(vkQueueWaitIdle) \
  X(vkCreateFence) \
  X(vkResetFences) \
  X(vkCreateDescriptorSetLayout) \
  X(vkCreatePipelineLayout) \
  X(vkCreateDescriptorPool) \
  X(vkUpdateDescriptorSets) \
  X(vkCreateShaderModule) \
  X(vkDestroyShaderModule) \
  X(vkCmdBindPipeline) \
  X(vkCmdBindDescriptorSets) \
  X(vkCmdDispatch) \
  X(vkCreateWin32SurfaceKHR) \
  X(vkGetPhysicalDeviceSurfaceSupportKHR) \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
  X(vkCreateSwapchainKHR) \
  X(vkGetSwapchainImagesKHR) \
  X(vkQueuePresentKHR) \
  X(vkCreateImage) \
  X(vkBindImageMemory) \
  X(vkCmdCopyBufferToImage) \
  X(vkCmdBlitImage) \
  X(vkCmdClearColorImage) \
  X(vkGetPhysicalDeviceFeatures2) \
  X(vkDestroyFence) \
  X(vkDestroySemaphore) \
  X(vkDestroyPipeline) \
  X(vkDestroyDescriptorPool) \
  X(vkDestroyPipelineLayout) \
  X(vkDestroyDescriptorSetLayout) \
  X(vkDestroyCommandPool) \
  X(vkDestroySurfaceKHR) \
  X(vkDestroyDevice) \
  X(vkDestroyInstance) \
  X(vkUnmapMemory) \
  X(vkDestroySwapchainKHR) \
  X(vkGetPhysicalDeviceFeatures) \
  X(vkGetDeviceQueue) \
  X(vkAllocateDescriptorSets) \
  X(vkCreateComputePipelines) \
  X(vkBeginCommandBuffer) \
  X(vkWaitForFences) \
  X(vkCmdPipelineBarrier) \
  X(vkCreateSemaphore) \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  X(vkGetPhysicalDeviceFormatProperties) \
  X(vkDestroyImage) \
  X(vkGetImageMemoryRequirements) \
  X(vkAcquireNextImageKHR)
// clang-format on

#define DECLARE_FUNCTION(name) PFN_##name name = nullptr;
VULKAN_FUNCTIONS(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

void check(VkResult result, const char *operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) +
                             " failed: VkResult=" + std::to_string(result));
  }
}

std::uint32_t memory_type(const VkPhysicalDeviceMemoryProperties &properties,
                          std::uint32_t allowed,
                          VkMemoryPropertyFlags required) {
  for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((allowed & (1u << index)) &&
        (properties.memoryTypes[index].propertyFlags & required) == required)
      return index;
  }
  throw std::runtime_error("No Vulkan memory type satisfies requested flags");
}

} // namespace

Context::~Context() {
  if (queue_ && vkQueueWaitIdle)
    vkQueueWaitIdle(queue_);
  if (device_) {
    for (auto *buffer : buffers_) {
      if (buffer->mapped)
        vkUnmapMemory(device_, buffer->memory);
      if (buffer->handle)
        vkDestroyBuffer(device_, buffer->handle, nullptr);
      if (buffer->memory)
        vkFreeMemory(device_, buffer->memory, nullptr);
      delete buffer;
    }
    destroy_image();
    if (swapchain_)
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (acquired_)
      vkDestroySemaphore(device_, acquired_, nullptr);
    if (rendered_)
      vkDestroySemaphore(device_, rendered_, nullptr);
    if (present_fence_)
      vkDestroyFence(device_, present_fence_, nullptr);
    for (auto pipeline : pipelines_)
      if (pipeline)
        vkDestroyPipeline(device_, pipeline, nullptr);
    if (descriptor_pool_)
      vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    if (pipeline_layout_)
      vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (descriptor_layout_)
      vkDestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
    if (fence_)
      vkDestroyFence(device_, fence_, nullptr);
    if (command_pool_)
      vkDestroyCommandPool(device_, command_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (surface_)
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_ && vkDestroyInstance)
    vkDestroyInstance(instance_, nullptr);
  if (loader_)
    FreeLibrary(loader_);
}

void Context::initialize(const std::filesystem::path &shaders) {
  aspect_ = parse_aspect(std::getenv("GT4_ASPECT"));
  loader_ =
      LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!loader_)
    throw std::runtime_error("Vulkan loader unavailable in System32");
  const auto get_instance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(loader_, "vkGetInstanceProcAddr"));
  if (!get_instance)
    throw std::runtime_error("Vulkan loader lacks vkGetInstanceProcAddr");
  vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
      get_instance(nullptr, "vkCreateInstance"));
  if (!vkCreateInstance)
    throw std::runtime_error("Vulkan loader lacks vkCreateInstance");
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "GT4 native Vulkan pilot";
  application.apiVersion = VK_API_VERSION_1_2;
  const char *extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface",
                              "VK_KHR_get_surface_capabilities2",
                              "VK_KHR_surface_maintenance1"};
  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &application;
  instance_info.enabledExtensionCount = 4;
  instance_info.ppEnabledExtensionNames = extensions;
  check(vkCreateInstance(&instance_info, nullptr, &instance_),
        "vkCreateInstance");
#define LOAD_FUNCTION(name)                                                    \
  name = reinterpret_cast<PFN_##name>(get_instance(instance_, #name));
  VULKAN_FUNCTIONS(LOAD_FUNCTION)
#undef LOAD_FUNCTION
  std::uint32_t count = 0;
  check(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
        "enumerate GPUs");
  std::vector<VkPhysicalDevice> devices(count);
  check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
        "enumerate GPUs");
  for (const auto candidate : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    if (!physical_ ||
        properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      physical_ = candidate;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      break;
  }
  if (!physical_)
    throw std::runtime_error("No Vulkan GPU available");
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_, &properties);
  limits_ = properties.limits;
  if (properties.apiVersion < VK_API_VERSION_1_2 ||
      properties.limits.maxPerStageDescriptorStorageBuffers < 7 ||
      properties.limits.maxComputeWorkGroupInvocations < 256 ||
      256 % properties.limits.minUniformBufferOffsetAlignment != 0)
    throw std::runtime_error("GPU limits do not support the GS compute layout");
  std::fprintf(
      stderr,
      "[vulkan] device=%s api=%u.%u.%u; direct compute, scale=1, synchronous\n",
      properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion),
      VK_VERSION_MINOR(properties.apiVersion),
      VK_VERSION_PATCH(properties.apiVersion));
  vkGetPhysicalDeviceMemoryProperties(physical_, &memory_properties_);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, families.data());
  bool found = false;
  for (std::uint32_t index = 0; index < count; ++index) {
    if ((families[index].queueFlags &
         (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
      queue_family_ = index;
      found = true;
      break;
    }
  }
  if (!found)
    throw std::runtime_error("No Vulkan graphics/compute queue");
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = queue_family_;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkPhysicalDeviceFeatures available{};
  vkGetPhysicalDeviceFeatures(physical_, &available);
  VkPhysicalDeviceFeatures features{};
  features.robustBufferAccess = available.robustBufferAccess;
  VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{};
  maintenance.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
  VkPhysicalDeviceFeatures2 queried{};
  queried.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  queried.pNext = &maintenance;
  vkGetPhysicalDeviceFeatures2(physical_, &queried);
  if (!maintenance.swapchainMaintenance1)
    throw std::runtime_error(
        "Vulkan pilot requires maintenance1 present fences");
  const char *device_extensions[] = {"VK_KHR_swapchain",
                                     "VK_KHR_swapchain_maintenance1"};
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &features;
  device_info.enabledExtensionCount = 2;
  device_info.pNext = &maintenance;
  device_info.ppEnabledExtensionNames = device_extensions;
  check(vkCreateDevice(physical_, &device_info, nullptr, &device_),
        "vkCreateDevice");
#define REQUIRE_FUNCTION(name)                                                 \
  if (!name)                                                                   \
    throw std::runtime_error("Vulkan entry point unavailable: " #name);
  VULKAN_FUNCTIONS(REQUIRE_FUNCTION)
#undef REQUIRE_FUNCTION
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.queueFamilyIndex = queue_family_;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
        "command pool");
  VkCommandBufferAllocateInfo commands{};
  commands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commands.commandPool = command_pool_;
  commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commands.commandBufferCount = 1;
  check(vkAllocateCommandBuffers(device_, &commands, &command_),
        "command buffer");
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  check(vkCreateFence(device_, &fence_info, nullptr, &fence_),
        "submission fence");
  std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
  for (std::uint32_t index = 0; index < bindings.size(); ++index) {
    bindings[index].binding = index;
    bindings[index].descriptorCount = 1;
    bindings[index].descriptorType =
        index == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                   : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
  layout_info.pBindings = bindings.data();
  check(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr,
                                    &descriptor_layout_),
        "descriptor layout");
  VkPipelineLayoutCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_info.setLayoutCount = 1;
  pipeline_info.pSetLayouts = &descriptor_layout_;
  check(vkCreatePipelineLayout(device_, &pipeline_info, nullptr,
                               &pipeline_layout_),
        "pipeline layout");
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}};
  VkDescriptorPoolCreateInfo descriptors{};
  descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptors.maxSets = 1;
  descriptors.poolSizeCount = 2;
  descriptors.pPoolSizes = sizes;
  check(
      vkCreateDescriptorPool(device_, &descriptors, nullptr, &descriptor_pool_),
      "descriptor pool");
  VkDescriptorSetAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocation.descriptorPool = descriptor_pool_;
  allocation.descriptorSetCount = 1;
  allocation.pSetLayouts = &descriptor_layout_;
  check(vkAllocateDescriptorSets(device_, &allocation, &descriptor_set_),
        "descriptor set");
  const char *entries[] = {"upload",    "copy_read", "copy_write",
                           "load_clut", "display",   "raster",
                           "resolve",   "snapshot",  "decode"};
  for (std::size_t index = 0; index < pipelines_.size(); ++index) {
    std::ifstream input(shaders / (std::string(entries[index]) + ".spv"),
                        std::ios::binary | std::ios::ate);
    if (!input)
      throw std::runtime_error("Missing SPIR-V shader: " +
                               std::string(entries[index]));
    const auto bytes = static_cast<std::size_t>(input.tellg());
    if (bytes < 20 || bytes % 4)
      throw std::runtime_error("Invalid SPIR-V size");
    std::vector<std::uint32_t> words(bytes / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(words.data()), bytes) ||
        words[0] != 0x07230203u)
      throw std::runtime_error("Invalid SPIR-V content");
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = bytes;
    module_info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_, &module_info, nullptr, &module),
          "shader module");
    VkComputePipelineCreateInfo compute{};
    compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute.stage.module = module;
    compute.stage.pName = entries[index];
    compute.layout = pipeline_layout_;
    const auto result = vkCreateComputePipelines(
        device_, VK_NULL_HANDLE, 1, &compute, nullptr, &pipelines_[index]);
    vkDestroyShaderModule(device_, module, nullptr);
    check(result, entries[index]);
  }
}

Buffer *Context::create_buffer(std::uint64_t bytes) {
  if (!bytes)
    throw std::runtime_error("Cannot allocate zero-byte Vulkan buffer");
  auto *buffer = new Buffer;
  buffers_.push_back(buffer);
  buffer->size = bytes;
  VkBufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size = bytes;
  info.usage =
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  check(vkCreateBuffer(device_, &info, nullptr, &buffer->handle), "buffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, buffer->handle, &requirements);
  const auto host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  auto type =
      memory_type(memory_properties_, requirements.memoryTypeBits, host_flags);
  for (std::uint32_t index = 0; index < memory_properties_.memoryTypeCount;
       ++index) {
    if ((requirements.memoryTypeBits & (1u << index)) &&
        (memory_properties_.memoryTypes[index].propertyFlags &
         (host_flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ==
            (host_flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      type = index;
      break;
    }
  }
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = type;
  check(vkAllocateMemory(device_, &allocation, nullptr, &buffer->memory),
        "buffer memory");
  check(vkBindBufferMemory(device_, buffer->handle, buffer->memory, 0),
        "bind buffer");
  check(vkMapMemory(device_, buffer->memory, 0, bytes, 0, &buffer->mapped),
        "map coherent buffer");
  std::memset(buffer->mapped, 0, static_cast<std::size_t>(bytes));
  return buffer;
}

void Context::destroy_buffer(Buffer *buffer) {
  if (!buffer)
    return;
  wait_idle();
  if (buffer->mapped)
    vkUnmapMemory(device_, buffer->memory);
  vkDestroyBuffer(device_, buffer->handle, nullptr);
  vkFreeMemory(device_, buffer->memory, nullptr);
  std::erase(buffers_, buffer);
  delete buffer;
}

void Context::write(Buffer *buffer, std::uint64_t offset, const void *source,
                    std::uint64_t bytes) {
  if (!buffer || offset > buffer->size || bytes > buffer->size - offset)
    throw std::runtime_error("Vulkan buffer write outside allocation");
  if (bytes) {
    gt4::perf::Scope upload(gt4::perf::Stage::gs_upload);
    std::memcpy(static_cast<std::uint8_t *>(buffer->mapped) + offset, source,
                static_cast<std::size_t>(bytes));
  }
}

void Context::read(Buffer *buffer, std::uint64_t offset, void *destination,
                   std::uint64_t bytes) {
  if (!buffer || offset > buffer->size || bytes > buffer->size - offset)
    throw std::runtime_error("Vulkan buffer read outside allocation");
  if (bytes)
    std::memcpy(destination,
                static_cast<std::uint8_t *>(buffer->mapped) + offset,
                static_cast<std::size_t>(bytes));
}

void Context::bind_buffers(const std::array<Buffer *, 8> &buffers) {
  parameter_bytes_ = buffers[2] ? buffers[2]->size : 0;
  std::array<VkDescriptorBufferInfo, 8> information{};
  std::array<VkWriteDescriptorSet, 8> writes{};
  for (std::uint32_t index = 0; index < buffers.size(); ++index) {
    if (!buffers[index] || buffers[index]->size < 256 ||
        (index != 2 && buffers[index]->size > limits_.maxStorageBufferRange))
      throw std::runtime_error("GS descriptor buffer exceeds Vulkan limits");
    information[index].buffer = buffers[index]->handle;
    information[index].range = index == 2 ? 256 : buffers[index]->size;
    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[index].dstSet = descriptor_set_;
    writes[index].dstBinding = index;
    writes[index].descriptorCount = 1;
    writes[index].pBufferInfo = &information[index];
    writes[index].descriptorType =
        index == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                   : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
                         writes.data(), 0, nullptr);
}

void Context::begin_commands() {
  check(vkResetCommandBuffer(command_, 0), "reset command buffer");
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(command_, &begin), "begin command buffer");
}

void Context::finish_commands(VkSemaphore wait, VkSemaphore signal) {
  check(vkEndCommandBuffer(command_), "end command buffer");
  check(vkResetFences(device_, 1, &fence_), "reset fence");
  const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_;
  if (wait) {
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &wait;
    submit.pWaitDstStageMask = &stage;
  }
  if (signal) {
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signal;
  }
  check(vkQueueSubmit(queue_, 1, &submit, fence_), "queue submit");
  const auto wait_begin = gt4::perf::now_us();
  check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX),
        "wait submission fence");
  // The batch fence signals when the GPU finished this submission, so this is
  // the host blocked time that also measures GPU execution of the batch.
  gt4::perf::Probe::shared().note_wait(gt4::perf::Stage::gs_submit_wait,
                                       gt4::perf::now_us() - wait_begin);
}

void Context::memory_barrier(VkPipelineStageFlags source,
                             VkPipelineStageFlags destination,
                             VkAccessFlags writes, VkAccessFlags reads) {
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = writes;
  barrier.dstAccessMask = reads;
  vkCmdPipelineBarrier(command_, source, destination, 0, 1, &barrier, 0,
                       nullptr, 0, nullptr);
}

void Context::execute(const Dispatch *commands, std::size_t count) {
  if (!count)
    return;
  gt4::perf::Scope execute(gt4::perf::Stage::gs_execute);
  begin_commands();
  memory_barrier(VK_PIPELINE_STAGE_HOST_BIT |
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                     VK_ACCESS_SHADER_WRITE_BIT);
  for (std::size_t index = 0; index < count; ++index) {
    const auto &dispatch = commands[index];
    if (dispatch.pipeline >= pipelines_.size() ||
        dispatch.x > limits_.maxComputeWorkGroupCount[0] ||
        dispatch.y > limits_.maxComputeWorkGroupCount[1] ||
        static_cast<std::uint64_t>(dispatch.parameter) * 256 > UINT32_MAX ||
        static_cast<std::uint64_t>(dispatch.parameter) * 256 + 256 >
            parameter_bytes_)
      throw std::runtime_error(
          "Invalid GS Vulkan dispatch: pipeline=" +
          std::to_string(dispatch.pipeline) + " x=" +
          std::to_string(dispatch.x) + " y=" + std::to_string(dispatch.y) +
          " parameter=" + std::to_string(dispatch.parameter) +
          " parameter_bytes=" + std::to_string(parameter_bytes_));
    // Vulkan permits zero workgroups; fully clipped batches do no shader work.
    if ((!dispatch.x || !dispatch.y) && zero_dispatches_++ == 0)
      std::fprintf(stderr,
                   "[vulkan] first zero-work dispatch pipeline=%u x=%u y=%u "
                   "parameter=%u\n",
                   dispatch.pipeline, dispatch.x, dispatch.y,
                   dispatch.parameter);
    const std::uint32_t offset = dispatch.parameter * 256;
    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipelines_[dispatch.pipeline]);
    vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout_, 0, 1, &descriptor_set_, 1,
                            &offset);
    vkCmdDispatch(command_, dispatch.x, dispatch.y, 1);
    gt4::perf::Probe::shared().add_dispatch(
        static_cast<std::uint64_t>(dispatch.x) * dispatch.y);
    memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  }
  memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_HOST_READ_BIT);
  finish_commands();
}

void Context::wait_idle() { check(vkQueueWaitIdle(queue_), "queue idle"); }

void Context::attach_window(HWND window, std::uint32_t width,
                            std::uint32_t height) {
  if (!window || window_)
    throw std::runtime_error("Invalid or already attached Win32 window");
  window_ = window;
  window_width_ = width;
  window_height_ = height;
  VkWin32SurfaceCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  info.hinstance = GetModuleHandleW(nullptr);
  info.hwnd = window;
  check(vkCreateWin32SurfaceKHR(instance_, &info, nullptr, &surface_),
        "Win32 Vulkan surface");
  VkBool32 supported = VK_FALSE;
  check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queue_family_, surface_,
                                             &supported),
        "surface support");
  if (!supported)
    throw std::runtime_error(
        "Selected GPU queue cannot present to this Win32 surface");
  VkSemaphoreCreateInfo semaphore{};
  semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  check(vkCreateSemaphore(device_, &semaphore, nullptr, &acquired_),
        "acquire semaphore");
  check(vkCreateSemaphore(device_, &semaphore, nullptr, &rendered_),
        "present semaphore");
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  check(vkCreateFence(device_, &fence_info, nullptr, &present_fence_),
        "presentation fence");
  rebuild_swapchain();
}

void Context::resize(std::uint32_t width, std::uint32_t height) {
  window_width_ = width;
  window_height_ = height;
  resized_ = true;
}

void Context::rebuild_swapchain() {
  if (!window_width_ || !window_height_)
    return;
  wait_idle();
  VkSurfaceCapabilitiesKHR capabilities{};
  check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_,
                                                  &capabilities),
        "surface capabilities");
  if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      !(capabilities.supportedCompositeAlpha &
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
    throw std::runtime_error(
        "Win32 surface does not support opaque transfer presentation");
  std::uint32_t count = 0;
  check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count,
                                             nullptr),
        "surface formats");
  std::vector<VkSurfaceFormatKHR> formats(count);
  check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count,
                                             formats.data()),
        "surface formats");
  auto format = VK_FORMAT_UNDEFINED;
  for (const auto &candidate : formats) {
    if (candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
        (candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
         candidate.format == VK_FORMAT_R8G8B8A8_UNORM)) {
      format = candidate.format;
      break;
    }
  }
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    format = VK_FORMAT_B8G8R8A8_UNORM;
  if (format == VK_FORMAT_UNDEFINED)
    throw std::runtime_error(
        "No UNORM swapchain format; do not add implicit sRGB conversion");
  VkFormatProperties source_features{}, destination_features{};
  vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM,
                                      &source_features);
  vkGetPhysicalDeviceFormatProperties(physical_, format, &destination_features);
  if (!(source_features.optimalTilingFeatures &
        VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !(destination_features.optimalTilingFeatures &
        VK_FORMAT_FEATURE_BLIT_DST_BIT))
    throw std::runtime_error(
        "GPU formats do not support the native presentation blit");
  extent_ = capabilities.currentExtent;
  if (extent_.width == UINT32_MAX) {
    extent_.width = std::clamp(window_width_, capabilities.minImageExtent.width,
                               capabilities.maxImageExtent.width);
    extent_.height =
        std::clamp(window_height_, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }
  if (!extent_.width || !extent_.height) {
    resized_ = true;
    return;
  }
  std::uint32_t images = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount)
    images = std::min(images, capabilities.maxImageCount);
  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface_;
  info.minImageCount = images;
  info.imageFormat = format;
  info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = capabilities.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  info.oldSwapchain = swapchain_;
  VkSwapchainKHR next = VK_NULL_HANDLE;
  check(vkCreateSwapchainKHR(device_, &info, nullptr, &next),
        "create swapchain");
  if (swapchain_)
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = next;
  resized_ = false;
  std::fprintf(stderr,
               "[vulkan] Win32 swapchain %ux%u, format=%d; GPU blit, no "
               "temporal field blend\n",
               extent_.width, extent_.height, static_cast<int>(format));
}

void Context::destroy_image() {
  if (display_image_)
    vkDestroyImage(device_, display_image_, nullptr);
  if (display_memory_)
    vkFreeMemory(device_, display_memory_, nullptr);
  display_image_ = VK_NULL_HANDLE;
  display_memory_ = VK_NULL_HANDLE;
}

void Context::image_barrier(VkImage image, VkImageLayout old_layout,
                            VkImageLayout new_layout,
                            VkPipelineStageFlags source,
                            VkPipelineStageFlags destination,
                            VkAccessFlags writes, VkAccessFlags reads) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcAccessMask = writes;
  barrier.dstAccessMask = reads;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(command_, source, destination, 0, 0, nullptr, 0, nullptr,
                       1, &barrier);
}

bool Context::present(Buffer *rgba, std::uint32_t width, std::uint32_t height) {
  if (!window_ || !window_width_ || !window_height_)
    return false;
  gt4::perf::Scope presenting(gt4::perf::Stage::present);
  if (resized_ || !swapchain_)
    rebuild_swapchain();
  if (!extent_.width || !extent_.height)
    return false;
  if (!rgba || !width || !height ||
      static_cast<std::uint64_t>(width) * height * 4 > rgba->size)
    throw std::runtime_error("Invalid native Vulkan frame size");
  if (width != image_width_ || height != image_height_) {
    wait_idle();
    destroy_image();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    check(vkCreateImage(device_, &info, nullptr, &display_image_),
          "display image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, display_image_, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        memory_type(memory_properties_, requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &allocation, nullptr, &display_memory_),
          "display image memory");
    check(vkBindImageMemory(device_, display_image_, display_memory_, 0),
          "bind display image");
    image_width_ = width;
    image_height_ = height;
  }
  std::uint32_t selected = 0;
  const auto acquire_begin = gt4::perf::now_us();
  auto acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                        acquired_, VK_NULL_HANDLE, &selected);
  // With FIFO presentation this waits for a swapchain image, i.e. for the
  // previous frame to reach the display.
  gt4::perf::Probe::shared().note_wait(gt4::perf::Stage::acquire_wait,
                                       gt4::perf::now_us() - acquire_begin);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    resized_ = true;
    return false;
  }
  if (acquired != VK_SUBOPTIMAL_KHR)
    check(acquired, "acquire swapchain image");
  std::uint32_t count = 0;
  check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr),
        "swapchain images");
  std::vector<VkImage> images(count);
  check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data()),
        "swapchain images");
  if (selected >= images.size())
    throw std::runtime_error("Invalid acquired swapchain image index");
  begin_commands();
  memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                     VK_PIPELINE_STAGE_HOST_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT);
  // Contents are replaced completely; UNDEFINED deliberately discards the
  // previous frame.
  image_barrier(
      display_image_, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(command_, rgba->handle, display_image_,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  image_barrier(display_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  const VkImage target = images[selected];
  image_barrier(
      target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
      VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(command_, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       &black, 1, &range);
  memory_barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  const auto destination =
      presentation_rect(extent_.width, extent_.height, aspect_);
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {static_cast<int>(width), static_cast<int>(height), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[0] = {static_cast<int>(destination.left),
                        static_cast<int>(destination.top), 0};
  blit.dstOffsets[1] = {static_cast<int>(destination.left + destination.width),
                        static_cast<int>(destination.top + destination.height),
                        1};
  vkCmdBlitImage(command_, display_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 VK_FILTER_NEAREST);
  image_barrier(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0);
  finish_commands(acquired_, rendered_);
  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &rendered_;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain_;
  present_info.pImageIndices = &selected;
  VkSwapchainPresentFenceInfoKHR completion{};
  completion.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
  completion.swapchainCount = 1;
  completion.pFences = &present_fence_;
  present_info.pNext = &completion;
  check(vkResetFences(device_, 1, &present_fence_), "reset present fence");
  const auto result = vkQueuePresentKHR(queue_, &present_info);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
      result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_ERROR_SURFACE_LOST_KHR)
    check(result, "present submission");
  // shortcut: wait for actual presentation lifetime, qualify overlap later.
  const auto present_begin = gt4::perf::now_us();
  check(vkWaitForFences(device_, 1, &present_fence_, VK_TRUE, UINT64_MAX),
        "wait presentation completion");
  // The present fence signals when the image actually reached the display, so
  // this wait is the vblank/refresh quantization of one presentation.
  gt4::perf::Probe::shared().note_wait(gt4::perf::Stage::present_wait,
                                       gt4::perf::now_us() - present_begin);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      acquired == VK_SUBOPTIMAL_KHR)
    resized_ = true;
  else
    check(result, "present frame");
  ++presentations_;
  gt4::perf::Probe::shared().add_presentation(static_cast<std::uint64_t>(width) *
                                               height * 4ull);
  return true;
}

} // namespace gt4::vulkan
