// MIT License. Copyright (c) 2026 Joao Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room (decision 0069).
//
// HWR replay tool (M6): replays a recorded GS stream through the GPU write-back renderer and writes one
// PPM per frame, so the GPU path can be compared with the CPU reference on exactly the same input.
// Chapters 14.3 (replay) and 12.9 (frame dumps); the record formats are the ones cpuref_replay reads.
//
// Usage: hw_writeback_replay <capture> <out-dir> [--every N]
// Exit 77 means "no Vulkan device with graphics and compute" (the caller reports it as skipped).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hw_gs_vulkan_loader.hpp"
#include "hw_gs_vulkan_device_table.hpp"
#include "writeback/hw_gs_writeback.hpp"
#include "../cpuref/cpuref_replay.hpp"

namespace {

using namespace hwgs;

struct HeadlessDevice {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;

    bool Create(std::string& error) {
        if (!gt4::hw::vulkan_loader::load_loader(error)) return false;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "hw_writeback_replay";
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        if (gt4_hw_vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
            error = "vkCreateInstance failed";
            return false;
        }
        std::vector<std::string> missing;
        if (!gt4::hw::vulkan_loader::load_headless_entry_points(instance, missing)) {
            error = "instance entry points missing:";
            for (const std::string& name : missing) error += " " + name;
            return false;
        }
        std::uint32_t count = 0;
        gt4_hw_vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        gt4_hw_vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties props{};
            gt4_hw_vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.apiVersion < VK_API_VERSION_1_2) continue;
            std::uint32_t families = 0;
            gt4_hw_vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> queueProps(families);
            gt4_hw_vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queueProps.data());
            for (std::uint32_t f = 0; f < families; ++f) {
                const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
                if ((queueProps[f].queueFlags & wanted) != wanted) continue;
                physical = candidate;
                family = f;
                break;
            }
            if (physical) break;
        }
        if (!physical) {
            error = "no Vulkan 1.2 device with graphics and compute";
            return false;
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        if (gt4_hw_vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
            error = "vkCreateDevice failed";
            return false;
        }
        gt4_hw_vkGetDeviceQueue(device, family, 0, &queue);
        return gt4::hw::vulkan_device::load_device_entry_points(instance, missing);
    }
};

// The GPU write-back renderer as a replay target: EndFrame flushes the frame and composes the display.
class WritebackTarget final : public cpuref::ReplayTarget {
public:
    explicit WritebackTarget(writeback::Renderer& renderer) : renderer_(renderer) {}

    bool GifTransfer(std::uint32_t path, const std::vector<std::uint8_t>& bytes) override {
        return renderer_.GifTransfer(path, bytes.data(), bytes.size());
    }
    bool WritePrivileged(std::uint32_t address, std::uint64_t value) override {
        return renderer_.WritePrivileged(address, value);
    }
    std::vector<std::uint8_t> ReadFifo(std::uint32_t qwords) override {
        std::vector<std::uint8_t> data(static_cast<std::size_t>(qwords) * 16u);
        const std::size_t filled = renderer_.ReadFifo(data.data(), data.size());
        data.resize(filled);
        return data;
    }
    cpuref::FrameImage EndFrame() override {
        renderer_.Flush();
        writeback::FrameOutput out;
        renderer_.ComposeFrame(out);
        cpuref::FrameImage frame;
        frame.valid = out.valid && out.width > 0 && out.height > 0;
        if (!frame.valid) return frame;
        frame.image = display::Image(out.width, out.height);
        for (std::uint32_t y = 0; y < out.height; ++y) {
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const std::uint32_t pixel = out.pixels[static_cast<std::size_t>(y) * out.width + x];
                display::Rgba8& dst = frame.image.at(x, y);
                dst.r = static_cast<std::uint8_t>(pixel & 0xFFu);
                dst.g = static_cast<std::uint8_t>((pixel >> 8) & 0xFFu);
                dst.b = static_cast<std::uint8_t>((pixel >> 16) & 0xFFu);
                dst.a = static_cast<std::uint8_t>((pixel >> 24) & 0xFFu);
            }
        }
        return frame;
    }

    void SetDisplayField(bool odd) override { oddField_ = odd; }
    bool DisplayField() const override { return oddField_; }
    void ResetGs(bool clearVram) override {
        renderer_.ResetGs();
        if (clearVram) renderer_.ClearVram();
    }
    void ResetGif(std::uint32_t pathMask) override { renderer_.GifReset(pathMask); }

private:
    writeback::Renderer& renderer_;
    bool oddField_ = false;
};

bool WritePpm(const std::string& path, const cpuref::FrameImage& frame) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P6\n%u %u\n255\n", frame.image.width, frame.image.height);
    for (std::uint32_t y = 0; y < frame.image.height; ++y) {
        for (std::uint32_t x = 0; x < frame.image.width; ++x) {
            const display::Rgba8& pixel = frame.image.at(x, y);
            const unsigned char rgb[3] = {pixel.r, pixel.g, pixel.b};
            std::fwrite(rgb, 1, 3, file);
        }
    }
    std::fclose(file);
    return true;
}

bool ReadFile(const char* path, std::vector<std::uint8_t>& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        out.resize(static_cast<std::size_t>(size));
        const std::size_t read = std::fread(out.data(), 1, out.size(), file);
        out.resize(read);
    }
    std::fclose(file);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: hw_writeback_replay <capture> <out-dir> [--every N]\n");
        return 2;
    }
    const std::string capturePath = argv[1];
    const std::string outDir = argv[2];
    std::uint64_t every = 1;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--every") == 0) {
            const long value = std::strtol(argv[i + 1], nullptr, 10);
            if (value > 0) every = static_cast<std::uint64_t>(value);
        }
    }

    std::vector<std::uint8_t> stream;
    if (!ReadFile(capturePath.c_str(), stream) || stream.empty()) {
        std::fprintf(stderr, "cannot read capture: %s\n", capturePath.c_str());
        return 2;
    }

    std::printf("capture=%s bytes=%llu magic=%.8s v2=%d\n", capturePath.c_str(),
                static_cast<unsigned long long>(stream.size()),
                stream.size() >= 8 ? reinterpret_cast<const char*>(stream.data()) : "",
                stream.size() >= 8 && std::memcmp(stream.data(), "GSSTREAM", 8) == 0 ? 1 : 0);
    HeadlessDevice headless;
    std::string error;
    if (!headless.Create(error)) {
        std::fprintf(stderr, "skipped: %s\n", error.c_str());
        return 77;
    }
    writeback::Renderer renderer;
    gpu::VulkanHandles handles;
    handles.instance = headless.instance;
    handles.physical = headless.physical;
    handles.device = headless.device;
    handles.queue = headless.queue;
    handles.queueFamily = headless.family;
    if (!renderer.Init(handles, error)) {
        std::fprintf(stderr, "GPU drawer init failed: %s\n", error.c_str());
        return 1;
    }

    WritebackTarget target(renderer);
    cpuref::ReplaySummary summary;
    std::uint64_t written = 0;
    const auto onFrame = [&](std::uint64_t frame, const cpuref::FrameImage& image) {
        if (!image.valid) return;
        if (every > 1 && frame % every != 0) return;
        char name[64];
        std::snprintf(name, sizeof(name), "frame_%05llu.ppm", static_cast<unsigned long long>(frame));
        if (WritePpm(outDir + "\\" + name, image)) ++written;
    };
    const bool ok = cpuref::ReplayCapture(stream, target, summary, onFrame, error);

    std::printf("records=%llu gif_packets=%llu gif_bytes=%llu privileged=%llu fifo_reads=%llu frames=%llu "
                "images=%llu rejected_gif=%llu resets=%llu\n",
                static_cast<unsigned long long>(summary.records),
                static_cast<unsigned long long>(summary.gifPackets),
                static_cast<unsigned long long>(summary.gifBytes),
                static_cast<unsigned long long>(summary.privileged),
                static_cast<unsigned long long>(summary.fifoReads),
                static_cast<unsigned long long>(summary.frames),
                static_cast<unsigned long long>(summary.framesWithImage),
                static_cast<unsigned long long>(summary.rejectedGifPackets),
                static_cast<unsigned long long>(summary.resets));
    const writeback::Counters& counters = renderer.GetCounters();
    std::printf("batches=%llu primitives=%llu skipped_texture=%llu empty=%llu textures=%llu gpu_draws=%llu "
                "writeback_pixels=%llu\n",
                static_cast<unsigned long long>(counters.batches),
                static_cast<unsigned long long>(counters.primitives),
                static_cast<unsigned long long>(counters.primitivesSkippedTexture),
                static_cast<unsigned long long>(counters.batchesEmpty),
                static_cast<unsigned long long>(counters.texturesDecoded),
                static_cast<unsigned long long>(counters.gpuDraws),
                static_cast<unsigned long long>(counters.writebackPixels));
    std::printf("images_written=%llu\n", static_cast<unsigned long long>(written));
    if (!ok) {
        std::fprintf(stderr, "replay failed: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
