// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from docs/hw-renderer-spec (TASK-027 M4, chapters 00 reference mode, 03.1 to 03.4, 03.7,
// 03.10, 12.3), clean room (decision 0069).
//
// The GPU side of the write-back reference mode. Each chunk is drawn synchronously: upload the
// frame and depth words and the texture, draw each primitive with its own scissor and a barrier
// between draws (storage image ordering, chapter 08.8 barrier policy), read the frame and depth
// words back. Vulkan entry points are called through pointers (see hw_gs_vulkan_loader.hpp).

// Vulkan create-info structs are initialised with sType only (pNext defaults to null by the spec).
#pragma clang diagnostic ignored "-Wmissing-field-initializers"

#include "hw_gs_writeback_gpu.hpp"

#include "hw_gs_vulkan_device_table.hpp"
#include "../shaders/hw_shader_blobs.hpp"

#include <algorithm>
#include <cstring>

namespace {
std::uint32_t FloatToBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
} // namespace

namespace hwgs::gpu {
namespace {

constexpr std::uint32_t kSurfaceSize = static_cast<std::uint32_t>(kGpuSurface);  // 2048
constexpr std::uint32_t kTextureMax = kGpuMaxTextureSide;                       // texels per axis
constexpr VkDeviceSize kFrameBytes = static_cast<VkDeviceSize>(kSurfaceSize) * kSurfaceSize * 4;
// Staging, vertex and uniform memory are rings (IN-342): chunks are recorded into one command buffer and
// submitted when a readback needs the result, a ring is full, or the caller flushes (each VBlank). The staging
// ring holds at least one chunk of the largest size (two full surfaces and a full texture).
constexpr VkDeviceSize kStagingBytes = 2 * kFrameBytes + static_cast<VkDeviceSize>(kTextureMax) * kTextureMax * 4 +
                                       (static_cast<VkDeviceSize>(32) << 20);
constexpr VkDeviceSize kReadbackBytes = 2 * kFrameBytes;
constexpr VkDeviceSize kVertexBytes = static_cast<VkDeviceSize>(kGpuMaxVertices) * sizeof(GpuVertex);
constexpr VkDeviceSize kUniformBytes = 256;                   // one pixel-state block
constexpr VkDeviceSize kUniformRingBytes = static_cast<VkDeviceSize>(4) << 20;  // 16,384 blocks at 256 B
constexpr std::uint32_t kPushBytes = 28;  // hw_vertex.vert PushConstants

const char *result_text(VkResult result) {
  switch (result) {
  case VK_SUCCESS: return "VK_SUCCESS";
  case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
  default: return "VkResult error";
  }
}

bool check(VkResult result, const char *what, std::string &error) {
  if (result == VK_SUCCESS) return true;
  error = std::string(what) + " failed: " + result_text(result);
  return false;
}

} // namespace

struct GpuDrawer::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memory{};

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;

  VkShaderModule vertexModule = VK_NULL_HANDLE;
  VkShaderModule fragmentModule = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkPipeline pipelines[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};

  struct Resource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceSize size = 0;
  };
  struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  };

  Resource staging;
  Resource readback;
  Resource vertices;
  Resource uniforms;
  Image frame;
  Image depth;
  Image texels;

  bool ready = false;
  GpuStats stats;

  // Recording state of the rings (IN-342).
  bool recording = false;        // the command buffer is open
  bool inFlight = false;         // a submission may still read the rings
  VkDeviceSize stagingUsed = 0;  // bytes
  VkDeviceSize vertexUsed = 0;   // vertices
  VkDeviceSize uniformUsed = 0;  // bytes
  VkDeviceSize uniformStride = kUniformBytes;

  bool submit(bool wait, std::string &error);
  bool wait_idle(std::string &error);
  bool begin_recording(std::string &error);

  // Chooses a memory type with all `wanted` flags; `preferred` flags are tried first.
  bool memory_type(std::uint32_t typeBits, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags preferred,
                   std::uint32_t &chosen) const {
    for (int pass = 0; pass < 2; ++pass) {
      const VkMemoryPropertyFlags required = pass == 0 ? (wanted | preferred) : wanted;
      for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if (!(typeBits & (1u << i))) continue;
        if ((memory.memoryTypes[i].propertyFlags & required) == required) {
          chosen = i;
          return true;
        }
      }
      if (preferred == 0) break;
    }
    return false;
  }

  bool make_buffer(Resource &resource, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags wanted,
                   VkMemoryPropertyFlags preferred, bool map, std::string &error) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(gt4_hwd_vkCreateBuffer(device, &info, nullptr, &resource.buffer), "vkCreateBuffer", error)) return false;
    VkMemoryRequirements requirements{};
    gt4_hwd_vkGetBufferMemoryRequirements(device, resource.buffer, &requirements);
    std::uint32_t type = 0;
    if (!memory_type(requirements.memoryTypeBits, wanted, preferred, type)) {
      error = "no host-visible memory type for a HWR buffer";
      return false;
    }
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (!check(gt4_hwd_vkAllocateMemory(device, &allocate, nullptr, &resource.memory), "vkAllocateMemory", error)) return false;
    if (!check(gt4_hwd_vkBindBufferMemory(device, resource.buffer, resource.memory, 0), "vkBindBufferMemory", error)) return false;
    resource.size = size;
    if (map) {
      if (!check(gt4_hwd_vkMapMemory(device, resource.memory, 0, size, 0, &resource.mapped), "vkMapMemory", error)) return false;
    }
    return true;
  }

  bool make_image(Image &image, std::uint32_t width, std::uint32_t height, std::string &error) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R32_UINT;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!check(gt4_hwd_vkCreateImage(device, &info, nullptr, &image.image), "vkCreateImage", error)) return false;
    VkMemoryRequirements requirements{};
    gt4_hwd_vkGetImageMemoryRequirements(device, image.image, &requirements);
    std::uint32_t type = 0;
    if (!memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, type)) {
      error = "no device-local memory type for a HWR image";
      return false;
    }
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (!check(gt4_hwd_vkAllocateMemory(device, &allocate, nullptr, &image.memory), "vkAllocateMemory", error)) return false;
    if (!check(gt4_hwd_vkBindImageMemory(device, image.image, image.memory, 0), "vkBindImageMemory", error)) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R32_UINT;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return check(gt4_hwd_vkCreateImageView(device, &view, nullptr, &image.view), "vkCreateImageView", error);
  }

  VkShaderModule make_module(const std::uint32_t *words, std::size_t count, std::string &error) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = count * sizeof(std::uint32_t);
    info.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    if (!check(gt4_hwd_vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule", error)) {
      return VK_NULL_HANDLE;
    }
    return module;
  }

  VkPipeline make_pipeline(VkPrimitiveTopology topology, std::string &error) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(GpuVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[7]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};      // S, T
    attributes[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT, 8};      // R, G, B, A
    attributes[2] = {2, 0, VK_FORMAT_R32_SFLOAT, 12};        // Q
    attributes[3] = {3, 0, VK_FORMAT_R16G16_UINT, 16};       // X, Y
    attributes[4] = {4, 0, VK_FORMAT_R32_UINT, 20};          // Z
    attributes[5] = {5, 0, VK_FORMAT_R16G16_UINT, 24};       // U, V
    attributes[6] = {6, 0, VK_FORMAT_R8_UNORM, 28};          // F
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 7;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = topology;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 0;  // no colour attachment: the fragment stage writes storage images

    VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;
    info.renderPass = renderPass;
    info.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(gt4_hwd_vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "vkCreateGraphicsPipelines", error);
    return pipeline;
  }

  // Records a whole-image layout change UNDEFINED -> GENERAL for the three images, once at start.
  bool make_images_general(std::string &error) {
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(gt4_hw_vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer", error)) return false;
    VkImageMemoryBarrier barriers[3]{};
    VkImage images[3] = {frame.image, depth.image, texels.image};
    for (int i = 0; i < 3; ++i) {
      barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
      barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[i].image = images[i];
      barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                  VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    gt4_hw_vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                                nullptr, 0, nullptr, 3, barriers);
    if (!check(gt4_hw_vkEndCommandBuffer(cmd), "vkEndCommandBuffer", error)) return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (!check(gt4_hw_vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit", error)) return false;
    gt4_hw_vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    gt4_hw_vkResetFences(device, 1, &fence);
    return true;
  }

  void destroy() {
    if (recording && device) {
      std::string ignored;
      submit(true, ignored);  // recorded work completes before the resources go
    }
    if (device) gt4_hw_vkDeviceWaitIdle(device);
    if (device) {
      for (VkPipeline &pipeline : pipelines) {
        if (pipeline) gt4_hwd_vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
      }
      if (framebuffer) gt4_hwd_vkDestroyFramebuffer(device, framebuffer, nullptr);
      if (renderPass) gt4_hwd_vkDestroyRenderPass(device, renderPass, nullptr);
      if (descriptorPool) gt4_hwd_vkDestroyDescriptorPool(device, descriptorPool, nullptr);
      if (pipelineLayout) gt4_hwd_vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      if (setLayout) gt4_hwd_vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
      if (vertexModule) gt4_hwd_vkDestroyShaderModule(device, vertexModule, nullptr);
      if (fragmentModule) gt4_hwd_vkDestroyShaderModule(device, fragmentModule, nullptr);
      for (Image *image : {&frame, &depth, &texels}) {
        if (image->view) gt4_hwd_vkDestroyImageView(device, image->view, nullptr);
        if (image->image) gt4_hwd_vkDestroyImage(device, image->image, nullptr);
        if (image->memory) gt4_hwd_vkFreeMemory(device, image->memory, nullptr);
      }
      for (Resource *resource : {&staging, &readback, &vertices, &uniforms}) {
        if (resource->mapped) gt4_hwd_vkUnmapMemory(device, resource->memory);
        if (resource->buffer) gt4_hwd_vkDestroyBuffer(device, resource->buffer, nullptr);
        if (resource->memory) gt4_hwd_vkFreeMemory(device, resource->memory, nullptr);
      }
      if (fence) gt4_hw_vkDestroyFence(device, fence, nullptr);
      if (pool) gt4_hw_vkDestroyCommandPool(device, pool, nullptr);
    }
    pipelineLayout = VK_NULL_HANDLE;
    setLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;
    framebuffer = VK_NULL_HANDLE;
    vertexModule = VK_NULL_HANDLE;
    fragmentModule = VK_NULL_HANDLE;
    fence = VK_NULL_HANDLE;
    pool = VK_NULL_HANDLE;
    frame = Image{};
    depth = Image{};
    texels = Image{};
    staging = Resource{};
    readback = Resource{};
    vertices = Resource{};
    uniforms = Resource{};
    ready = false;
  }
};

GpuDrawer::GpuDrawer() : impl_(std::make_unique<Impl>()) {}

GpuDrawer::~GpuDrawer() {
  if (impl_) impl_->destroy();
}

bool GpuDrawer::Ready() const { return impl_->ready; }

const GpuStats &GpuDrawer::Stats() const { return impl_->stats; }

bool GpuDrawer::Init(const VulkanHandles &handles, std::string &error) {
  Impl &s = *impl_;
  if (s.ready) return true;
  s.instance = static_cast<VkInstance>(handles.instance);
  s.physical = static_cast<VkPhysicalDevice>(handles.physical);
  s.device = static_cast<VkDevice>(handles.device);
  s.queue = static_cast<VkQueue>(handles.queue);
  if (!s.instance || !s.physical || !s.device || !s.queue) {
    error = "GpuDrawer::Init needs instance, physical device, device and queue";
    return false;
  }
  std::vector<std::string> missing;
  if (!gt4::hw::vulkan_device::load_device_entry_points(s.instance, missing)) {
    error = "missing Vulkan entry points:";
    for (const std::string &name : missing) error += " " + name;
    return false;
  }
  gt4_hwd_vkGetPhysicalDeviceMemoryProperties(s.physical, &s.memory);

  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = handles.queueFamily;
  if (!check(gt4_hw_vkCreateCommandPool(s.device, &poolInfo, nullptr, &s.pool), "vkCreateCommandPool", error)) return false;
  VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocate.commandPool = s.pool;
  allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate.commandBufferCount = 1;
  if (!check(gt4_hw_vkAllocateCommandBuffers(s.device, &allocate, &s.cmd), "vkAllocateCommandBuffers", error)) return false;
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (!check(gt4_hw_vkCreateFence(s.device, &fenceInfo, nullptr, &s.fence), "vkCreateFence", error)) return false;

  s.vertexModule = s.make_module(hwgs::shaders::hw_vertex_smooth_words, hwgs::shaders::hw_vertex_smooth_word_count, error);
  s.fragmentModule = s.make_module(hwgs::shaders::hw_pixel_uber_smooth_words,
                                   hwgs::shaders::hw_pixel_uber_smooth_word_count, error);
  if (!s.vertexModule || !s.fragmentModule) return false;

  VkDescriptorSetLayoutBinding bindings[4]{};
  bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  setInfo.bindingCount = 4;
  setInfo.pBindings = bindings;
  if (!check(gt4_hwd_vkCreateDescriptorSetLayout(s.device, &setInfo, nullptr, &s.setLayout), "vkCreateDescriptorSetLayout", error)) return false;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push.size = kPushBytes;
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &s.setLayout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &push;
  if (!check(gt4_hwd_vkCreatePipelineLayout(s.device, &layoutInfo, nullptr, &s.pipelineLayout), "vkCreatePipelineLayout", error)) return false;

  VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}};
  VkDescriptorPoolCreateInfo poolSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolSetInfo.maxSets = 1;
  poolSetInfo.poolSizeCount = 2;
  poolSetInfo.pPoolSizes = sizes;
  if (!check(gt4_hwd_vkCreateDescriptorPool(s.device, &poolSetInfo, nullptr, &s.descriptorPool), "vkCreateDescriptorPool", error)) return false;
  VkDescriptorSetAllocateInfo setAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  setAllocate.descriptorPool = s.descriptorPool;
  setAllocate.descriptorSetCount = 1;
  setAllocate.pSetLayouts = &s.setLayout;
  if (!check(gt4_hwd_vkAllocateDescriptorSets(s.device, &setAllocate, &s.descriptorSet), "vkAllocateDescriptorSets", error)) return false;

  if (!s.make_buffer(s.staging, kStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, true, error)) return false;
  if (!s.make_buffer(s.readback, kReadbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true, error)) return false;
  if (!s.make_buffer(s.vertices, kVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, true, error)) return false;
  {
    VkPhysicalDeviceProperties properties{};
    gt4_hw_vkGetPhysicalDeviceProperties(s.physical, &properties);
    const VkDeviceSize align = std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, 1);
    s.uniformStride = (kUniformBytes + align - 1) / align * align;
  }
  if (!s.make_buffer(s.uniforms, kUniformRingBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, true, error)) return false;

  if (!s.make_image(s.frame, kSurfaceSize, kSurfaceSize, error)) return false;
  if (!s.make_image(s.depth, kSurfaceSize, kSurfaceSize, error)) return false;
  if (!s.make_image(s.texels, kTextureMax, kTextureMax, error)) return false;

  VkDescriptorBufferInfo uniformInfo{s.uniforms.buffer, 0, kUniformBytes};
  VkDescriptorImageInfo imageInfos[3] = {
      {VK_NULL_HANDLE, s.frame.view, VK_IMAGE_LAYOUT_GENERAL},
      {VK_NULL_HANDLE, s.depth.view, VK_IMAGE_LAYOUT_GENERAL},
      {VK_NULL_HANDLE, s.texels.view, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[4]{};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = s.descriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  writes[0].pBufferInfo = &uniformInfo;
  for (int i = 0; i < 3; ++i) {
    writes[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i + 1].dstSet = s.descriptorSet;
    writes[i + 1].dstBinding = static_cast<std::uint32_t>(i + 1);
    writes[i + 1].descriptorCount = 1;
    writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[i + 1].pImageInfo = &imageInfos[i];
  }
  gt4_hwd_vkUpdateDescriptorSets(s.device, 4, writes, 0, nullptr);

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  passInfo.subpassCount = 1;
  passInfo.pSubpasses = &subpass;
  if (!check(gt4_hwd_vkCreateRenderPass(s.device, &passInfo, nullptr, &s.renderPass), "vkCreateRenderPass", error)) return false;
  VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbInfo.renderPass = s.renderPass;
  fbInfo.attachmentCount = 0;
  fbInfo.width = kSurfaceSize;
  fbInfo.height = kSurfaceSize;
  fbInfo.layers = 1;
  if (!check(gt4_hwd_vkCreateFramebuffer(s.device, &fbInfo, nullptr, &s.framebuffer), "vkCreateFramebuffer", error)) return false;

  s.pipelines[0] = s.make_pipeline(VK_PRIMITIVE_TOPOLOGY_POINT_LIST, error);
  s.pipelines[1] = s.make_pipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, error);
  s.pipelines[2] = s.make_pipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, error);
  if (!s.pipelines[0] || !s.pipelines[1] || !s.pipelines[2]) return false;

  if (!s.make_images_general(error)) return false;
  s.ready = true;
  return true;
}

// Submits the recorded commands. With `wait`, blocks until the GPU finished them and the rings are free again;
// without, the fence is waited for before the next recording reuses the rings (the GPU works while the CPU
// prepares the next frame).
bool GpuDrawer::Impl::submit(bool wait, std::string &error) {
  if (recording) {
    if (!check(gt4_hw_vkEndCommandBuffer(cmd), "vkEndCommandBuffer", error)) return false;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    gt4_hw_vkResetFences(device, 1, &fence);
    if (!check(gt4_hw_vkQueueSubmit(queue, 1, &submitInfo, fence), "vkQueueSubmit", error)) return false;
    recording = false;
    inFlight = true;
    ++stats.submissions;
  }
  if (wait) return wait_idle(error);
  return true;
}

bool GpuDrawer::Impl::wait_idle(std::string &error) {
  if (inFlight) {
    if (!check(gt4_hw_vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences", error)) return false;
    inFlight = false;
  }
  stagingUsed = 0;
  vertexUsed = 0;
  uniformUsed = 0;
  return true;
}

bool GpuDrawer::Impl::begin_recording(std::string &error) {
  if (recording) return true;
  if (!wait_idle(error)) return false;  // the rings may still be read by the previous submission
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!check(gt4_hw_vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer", error)) return false;
  recording = true;
  return true;
}

bool GpuDrawer::Flush(bool wait, std::string &error) { return impl_->submit(wait, error); }

bool GpuDrawer::Execute(GpuChunk &chunk, std::string &error) {
  Impl &s = *impl_;
  if (!s.ready) {
    error = "GpuDrawer::Execute before Init";
    return false;
  }
  const bool anyTransfer = chunk.uploadFrame || chunk.uploadDepth || chunk.readbackFrame || chunk.readbackDepth;
  if (chunk.draws.empty() && !anyTransfer) return true;
  if (chunk.vertices.size() > kGpuMaxVertices) {
    error = "chunk exceeds the vertex buffer (the caller must split it)";
    return false;
  }
  const std::int32_t width = chunk.rectX1 - chunk.rectX0 + 1;
  const std::int32_t height = chunk.rectY1 - chunk.rectY0 + 1;
  if (width <= 0 || height <= 0 || chunk.rectX0 < 0 || chunk.rectY0 < 0 || chunk.rectX1 >= kGpuSurface ||
      chunk.rectY1 >= kGpuSurface) {
    error = "chunk rectangle is outside the 2048 x 2048 surface";
    return false;
  }
  const std::size_t words = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (((chunk.uploadFrame || chunk.readbackFrame) && chunk.frameWords.size() != words) ||
      ((chunk.uploadDepth || chunk.readbackDepth) && chunk.depthWords.size() != words)) {
    error = "frame or depth word count does not match the rectangle";
    return false;
  }
  if (chunk.hasTexture && (chunk.texWidth == 0 || chunk.texHeight == 0 || chunk.texWidth > kTextureMax ||
                           chunk.texHeight > kTextureMax ||
                           chunk.texels.size() != static_cast<std::size_t>(chunk.texWidth) * chunk.texHeight)) {
    error = "texture size is outside the texel image";
    return false;
  }

  // Ring space this chunk needs (IN-342): the staging bytes of its uploads, its vertices and one uniform slot.
  const VkDeviceSize frameBytes = static_cast<VkDeviceSize>(words) * 4;
  const VkDeviceSize textureBytes = chunk.hasTexture ? static_cast<VkDeviceSize>(chunk.texels.size()) * 4 : 0;
  const VkDeviceSize stagingNeeded =
      (chunk.uploadFrame ? frameBytes : 0) + (chunk.uploadDepth ? frameBytes : 0) + textureBytes;
  const VkDeviceSize vertexNeeded = chunk.vertices.size();
  const bool drawing = !chunk.draws.empty();
  if (s.stagingUsed + stagingNeeded > kStagingBytes || s.vertexUsed + vertexNeeded > kGpuMaxVertices ||
      s.uniformUsed + s.uniformStride > kUniformRingBytes) {
    if (!s.submit(true, error)) return false;
  }
  if (!s.begin_recording(error)) return false;

  auto *stagingBase = static_cast<std::uint8_t *>(s.staging.mapped);
  const VkDeviceSize frameOffset = s.stagingUsed;
  if (chunk.uploadFrame) {
    std::memcpy(stagingBase + s.stagingUsed, chunk.frameWords.data(), frameBytes);
    s.stagingUsed += frameBytes;
  }
  const VkDeviceSize depthOffset = s.stagingUsed;
  if (chunk.uploadDepth) {
    std::memcpy(stagingBase + s.stagingUsed, chunk.depthWords.data(), frameBytes);
    s.stagingUsed += frameBytes;
  }
  const VkDeviceSize textureOffset = s.stagingUsed;
  if (chunk.hasTexture) {
    std::memcpy(stagingBase + s.stagingUsed, chunk.texels.data(), textureBytes);
    s.stagingUsed += textureBytes;
  }
  const VkDeviceSize vertexOffsetBytes = s.vertexUsed * sizeof(GpuVertex);
  if (!chunk.vertices.empty()) {
    std::memcpy(static_cast<std::uint8_t *>(s.vertices.mapped) + vertexOffsetBytes, chunk.vertices.data(),
                chunk.vertices.size() * sizeof(GpuVertex));
    s.vertexUsed += vertexNeeded;
  }
  const std::uint32_t uniformOffset = static_cast<std::uint32_t>(s.uniformUsed);
  if (drawing) {
    std::uint32_t uniform[64] = {};
    static_assert(sizeof(chunk.state.fields) <= sizeof(uniform), "uniform copy");
    std::memcpy(uniform, chunk.state.fields, sizeof(chunk.state.fields));
    std::memcpy(static_cast<std::uint8_t *>(s.uniforms.mapped) + uniformOffset, uniform, sizeof(uniform));
    s.uniformUsed += s.uniformStride;
  }

  const std::uint32_t zFormat = chunk.state.fields[gpufield::Zformat];
  const std::uint32_t zMax = zFormat == 1 ? 0xFFFFFFu : zFormat == 2 ? 0xFFFFu : 0xFFFFFFFFu;
  // PushConstants of hw_vertex.vert: viewport (vec2), scale, scale_offset (1.0 at 1x), offset X and
  // Y (0: the batch vertices already carry XYOFFSET), z_max.
  const float viewportSize[2] = {static_cast<float>(kSurfaceSize), static_cast<float>(kSurfaceSize)};
  std::uint32_t pushWords[7];
  std::memcpy(pushWords, viewportSize, sizeof(viewportSize));
  pushWords[2] = FloatToBits(1.0f);
  pushWords[3] = FloatToBits(1.0f);
  pushWords[4] = 0;
  pushWords[5] = 0;
  pushWords[6] = zMax;

  // Earlier chunks of this or a previous submission wrote the frame, depth and texel images (copies and shader
  // writes): make those writes visible and finish their reads before this chunk copies or draws.
  VkMemoryBarrier previousWork{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  previousWork.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  previousWork.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_TRANSFER_WRITE_BIT;
  gt4_hw_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                              &previousWork, 0, nullptr, 0, nullptr);
  s.stats.barriers++;

  VkBufferImageCopy frameCopy{};
  frameCopy.bufferOffset = frameOffset;
  frameCopy.bufferRowLength = static_cast<std::uint32_t>(width);
  frameCopy.bufferImageHeight = static_cast<std::uint32_t>(height);
  frameCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  frameCopy.imageOffset = {chunk.rectX0, chunk.rectY0, 0};
  frameCopy.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  VkBufferImageCopy depthCopy = frameCopy;
  depthCopy.bufferOffset = depthOffset;
  const bool anyUpload = chunk.uploadFrame || chunk.uploadDepth || chunk.hasTexture;
  if (chunk.uploadFrame) {
    gt4_hwd_vkCmdCopyBufferToImage(s.cmd, s.staging.buffer, s.frame.image, VK_IMAGE_LAYOUT_GENERAL, 1, &frameCopy);
  }
  if (chunk.uploadDepth) {
    gt4_hwd_vkCmdCopyBufferToImage(s.cmd, s.staging.buffer, s.depth.image, VK_IMAGE_LAYOUT_GENERAL, 1, &depthCopy);
  }
  if (chunk.hasTexture) {
    VkBufferImageCopy texCopy{};
    texCopy.bufferOffset = textureOffset;
    texCopy.bufferRowLength = chunk.texWidth;
    texCopy.bufferImageHeight = chunk.texHeight;
    texCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    texCopy.imageExtent = {chunk.texWidth, chunk.texHeight, 1};
    gt4_hwd_vkCmdCopyBufferToImage(s.cmd, s.staging.buffer, s.texels.image, VK_IMAGE_LAYOUT_GENERAL, 1, &texCopy);
  }
  if (anyUpload) {
    VkMemoryBarrier copyBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    gt4_hw_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                                &copyBarrier, 0, nullptr, 0, nullptr);
    s.stats.barriers++;
  }

  if (drawing) {
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = s.renderPass;
    pass.framebuffer = s.framebuffer;
    pass.renderArea = {{0, 0}, {kSurfaceSize, kSurfaceSize}};
    gt4_hwd_vkCmdBeginRenderPass(s.cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    gt4_hwd_vkCmdBindDescriptorSets(s.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0, 1, &s.descriptorSet, 1,
                                    &uniformOffset);
    gt4_hwd_vkCmdPushConstants(s.cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, kPushBytes, pushWords);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(kSurfaceSize), static_cast<float>(kSurfaceSize), 0.0f, 1.0f};
    gt4_hwd_vkCmdSetViewport(s.cmd, 0, 1, &viewport);
    VkDeviceSize vertexBufferOffset = vertexOffsetBytes;
    gt4_hwd_vkCmdBindVertexBuffers(s.cmd, 0, 1, &s.vertices.buffer, &vertexBufferOffset);

    bool anyDraw = false;
    for (std::size_t i = 0; i < chunk.draws.size(); ++i) {
      const GpuDraw &draw = chunk.draws[i];
      if (draw.count == 0 || draw.first + draw.count > chunk.vertices.size()) continue;
      const std::int32_t x0 = std::max<std::int32_t>(0, draw.x0);
      const std::int32_t y0 = std::max<std::int32_t>(0, draw.y0);
      const std::int32_t x1 = std::min<std::int32_t>(kGpuSurface - 1, draw.x1);
      const std::int32_t y1 = std::min<std::int32_t>(kGpuSurface - 1, draw.y1);
      if (x1 < x0 || y1 < y0) continue;
      if (anyDraw) {
        // Storage images: the next draw reads what this draw wrote (no implicit ordering in Vulkan).
        VkMemoryBarrier between{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        between.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        between.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        gt4_hw_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                    0, 1, &between, 0, nullptr, 0, nullptr);
        s.stats.barriers++;
      }
      VkRect2D scissor{{x0, y0}, {static_cast<std::uint32_t>(x1 - x0 + 1), static_cast<std::uint32_t>(y1 - y0 + 1)}};
      gt4_hwd_vkCmdSetScissor(s.cmd, 0, 1, &scissor);
      const std::size_t pipelineIndex = draw.topology == GpuTopology::Points  ? 0
                                        : draw.topology == GpuTopology::Lines ? 1
                                                                              : 2;
      gt4_hwd_vkCmdBindPipeline(s.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelines[pipelineIndex]);
      gt4_hwd_vkCmdDraw(s.cmd, draw.count, 1, draw.first, 0);
      anyDraw = true;
      s.stats.draws++;
    }
    gt4_hwd_vkCmdEndRenderPass(s.cmd);
  }

  s.stats.executions++;
  s.stats.vertices += chunk.vertices.size();
  s.stats.uploadBytes += stagingNeeded + chunk.vertices.size() * sizeof(GpuVertex);

  if (!(chunk.readbackFrame || chunk.readbackDepth)) return true;  // recorded; submitted later (IN-342)

  // Readback: make the shader and copy writes visible to the copies, copy, then submit and wait.
  VkMemoryBarrier toReadback{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  toReadback.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  toReadback.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  gt4_hw_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &toReadback, 0, nullptr, 0, nullptr);
  VkBufferImageCopy frameReadback = frameCopy;
  frameReadback.bufferOffset = 0;
  VkBufferImageCopy depthReadback = frameCopy;
  depthReadback.bufferOffset = frameBytes;
  if (chunk.readbackFrame) {
    gt4_hwd_vkCmdCopyImageToBuffer(s.cmd, s.frame.image, VK_IMAGE_LAYOUT_GENERAL, s.readback.buffer, 1, &frameReadback);
  }
  if (chunk.readbackDepth) {
    gt4_hwd_vkCmdCopyImageToBuffer(s.cmd, s.depth.image, VK_IMAGE_LAYOUT_GENERAL, s.readback.buffer, 1,
                                   &depthReadback);
  }
  VkMemoryBarrier readbackDone{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  readbackDone.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  readbackDone.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  gt4_hw_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &readbackDone,
                              0, nullptr, 0, nullptr);
  s.stats.barriers += 2;
  if (!s.submit(true, error)) return false;

  const auto *readback = static_cast<const std::uint8_t *>(s.readback.mapped);
  if (chunk.readbackFrame) std::memcpy(chunk.frameWords.data(), readback, frameBytes);
  if (chunk.readbackDepth) std::memcpy(chunk.depthWords.data(), readback + frameBytes, frameBytes);
  s.stats.readbackBytes += frameBytes * ((chunk.readbackFrame ? 1 : 0) + (chunk.readbackDepth ? 1 : 0));
  return true;
}

} // namespace hwgs::gpu
