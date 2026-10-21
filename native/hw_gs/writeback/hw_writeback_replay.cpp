// MIT License. Copyright (c) 2026 Joao Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room (decision 0069).
//
// HWR replay tool (M6): replays a recorded GS stream through the GPU write-back renderer and writes one
// PPM per frame, so the GPU path can be compared with the CPU reference on exactly the same input.
// Chapters 14.3 (replay) and 12.9 (frame dumps); the record formats are the ones cpuref_replay reads.
//
// Usage: hw_writeback_replay <capture> <out-dir> [--every N] [--slm-frame N] [--trace-frame N]
//        [--trace-rect x0,y0,x1,y1] [--slm-hashes <file>] [--compose reference|presentation]
// --compose presentation writes the image the window would show (13.3 field offset, 13.4 deinterlace with
// the default `auto`) instead of the reference image (12.9 `vram`, the CPU reference's image).
// --slm-hashes writes "<frame> <16 hex digits>" per frame (the FNV-1a hash of the 4 MiB local memory),
// the same format as hw_cpuref_replay, so the two paths can be compared in memory on every frame.
// --trace-frame prints every batch of that frame (state, rectangle, vertices) whose rectangle overlaps
// --trace-rect (default: everything), so a pixel difference can be tied to the batch that drew it.
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
#include "writeback/hw_writeback_replay_target.hpp"

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
    std::uint64_t slmFrame = UINT64_MAX;  // the frame whose raw shadow local memory is dumped
    std::uint64_t traceFrame = UINT64_MAX;  // the frame whose batches are printed
    std::string slmHashPath;                // per-frame local memory hashes
    writeback::ComposeKind composeKind = writeback::ComposeKind::Reference;
    PixelRect traceRect{0, 0, 2047, 2047};
    for (int i = 3; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--every") == 0) {
            const long value = std::strtol(argv[i + 1], nullptr, 10);
            if (value > 0) every = static_cast<std::uint64_t>(value);
        } else if (std::strcmp(argv[i], "--slm-frame") == 0) {
            slmFrame = static_cast<std::uint64_t>(std::strtoull(argv[i + 1], nullptr, 10));
        } else if (std::strcmp(argv[i], "--compose") == 0) {
            if (std::strcmp(argv[i + 1], "presentation") == 0) {
                composeKind = writeback::ComposeKind::Presentation;
            } else if (std::strcmp(argv[i + 1], "reference") != 0) {
                std::fprintf(stderr, "--compose takes reference or presentation\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--slm-hashes") == 0) {
            slmHashPath = argv[i + 1];
        } else if (std::strcmp(argv[i], "--trace-frame") == 0) {
            traceFrame = static_cast<std::uint64_t>(std::strtoull(argv[i + 1], nullptr, 10));
        } else if (std::strcmp(argv[i], "--trace-rect") == 0) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (std::sscanf(argv[i + 1], "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4) traceRect = PixelRect{x0, y0, x1, y1};
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

    // Frame counter for the trace: EndFrame of frame N is the N-th call (0-based), as in onFrame.
    std::uint64_t currentFrame = 0;
    std::uint64_t traceBatch = 0;
    if (traceFrame != UINT64_MAX) {
        renderer.SetBatchObserver([&](const Batch& batch, const writeback::BatchTranslation& t) {
            if (currentFrame != traceFrame) return;
            const std::uint64_t index = traceBatch++;
            if (!RectsOverlap(batch.drawRect, traceRect)) return;
            const BatchState& st = batch.state;
            std::printf("trace frame=%llu batch=%llu prims=%zu verts=%zu prim=0x%llx frame=0x%llx zbuf=0x%llx "
                        "tex0=0x%llx tex1=0x%llx clamp=0x%llx alpha=0x%llx test=0x%llx scissor=0x%llx "
                        "rect=%d,%d,%d,%d draws=%zu skipped=%d\n",
                        static_cast<unsigned long long>(currentFrame), static_cast<unsigned long long>(index),
                        batch.prims.size(), batch.vertices.size(), static_cast<unsigned long long>(st.effectivePrim),
                        static_cast<unsigned long long>(st.ctx.frame), static_cast<unsigned long long>(st.ctx.zbuf),
                        static_cast<unsigned long long>(st.ctx.tex0), static_cast<unsigned long long>(st.ctx.tex1),
                        static_cast<unsigned long long>(st.ctx.clamp), static_cast<unsigned long long>(st.ctx.alpha),
                        static_cast<unsigned long long>(st.ctx.test), static_cast<unsigned long long>(st.ctx.scissor),
                        batch.drawRect.x0, batch.drawRect.y0, batch.drawRect.x1, batch.drawRect.y1, t.draws.size(),
                        t.skippedTexture ? 1 : 0);
            const std::size_t shown = batch.vertices.size() < 8 ? batch.vertices.size() : 8;
            for (std::size_t v = 0; v < shown; ++v) {
                const Vertex& x = batch.vertices[v];
                std::printf("  v%zu xy4=%d,%d z=%u rgba=%u,%u,%u,%u fog=%u stq=%.9g,%.9g,%.9g uv=%u,%u\n", v, x.x4, x.y4,
                            x.z, x.r, x.g, x.b, x.a, x.fog, x.s, x.t, x.q, x.u, x.v);
            }
        });
    }

    std::FILE* slmHashes = nullptr;
    if (!slmHashPath.empty()) {
        slmHashes = std::fopen(slmHashPath.c_str(), "w");
        if (!slmHashes) {
            std::fprintf(stderr, "cannot create %s\n", slmHashPath.c_str());
            return 2;
        }
    }

    writeback::WritebackReplayTarget target(renderer, composeKind);
    cpuref::ReplaySummary summary;
    std::uint64_t written = 0;
    const auto onFrame = [&](std::uint64_t frame, const cpuref::FrameImage& image) {
        currentFrame = frame + 1;
        traceBatch = 0;
        if (slmHashes) {
            std::fprintf(slmHashes, "%llu %016llx\n", static_cast<unsigned long long>(frame),
                         static_cast<unsigned long long>(cpuref::HashLocalMemory(renderer.Slm(), 4u * 1024u * 1024u)));
        }
        if (!image.valid) return;
        if (every > 1 && frame % every != 0) return;
        char name[64];
        std::snprintf(name, sizeof(name), "frame_%05llu.ppm", static_cast<unsigned long long>(frame));
        if (WritePpm(outDir + "\\" + name, image)) ++written;
        if (frame == slmFrame) {
            char slmName[64];
            std::snprintf(slmName, sizeof(slmName), "slm_%05llu.bin", static_cast<unsigned long long>(frame));
            std::FILE* slm = std::fopen((outDir + "\\" + slmName).c_str(), "wb");
            if (slm) {
                std::fwrite(renderer.Slm(), 1, 4u * 1024u * 1024u, slm);
                std::fclose(slm);
            }
        }
    };
    const bool ok = cpuref::ReplayCapture(stream, target, summary, onFrame, error);

    if (slmHashes) std::fclose(slmHashes);
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
                "writeback_pixels=%llu writeback_depth_words=%llu fifo_overruns=%llu failed=%d\n",
                static_cast<unsigned long long>(counters.batches),
                static_cast<unsigned long long>(counters.primitives),
                static_cast<unsigned long long>(counters.primitivesSkippedTexture),
                static_cast<unsigned long long>(counters.batchesEmpty),
                static_cast<unsigned long long>(counters.texturesDecoded),
                static_cast<unsigned long long>(counters.gpuDraws),
                static_cast<unsigned long long>(counters.writebackPixels),
                static_cast<unsigned long long>(counters.writebackDepthWords),
                static_cast<unsigned long long>(counters.fifoOverruns), renderer.Failed() ? 1 : 0);
    std::printf("images_written=%llu\n", static_cast<unsigned long long>(written));
    if (renderer.Failed()) {
        std::fprintf(stderr, "GPU write-back renderer failed: %s\n", renderer.LastError().c_str());
        return 1;
    }
    if (!ok) {
        std::fprintf(stderr, "replay failed: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
