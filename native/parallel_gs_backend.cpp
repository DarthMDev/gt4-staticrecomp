// Headless paraLLEl-GS backend (TASK-012, slice 162). See parallel_gs_backend.hpp.
//
// Only this translation unit includes Granite and paraLLEl-GS headers. The Vulkan
// handles stay inside Impl, so the public header is usable from any other unit.

// getenv, fopen and sscanf are used as specified; silence the MSVC CRT deprecation notices.
#define _CRT_SECURE_NO_WARNINGS
#include "parallel_gs_backend.hpp"

#include "context.hpp"
#include "device.hpp"
#include "gs_interface.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4::parallel_gs {
namespace {

using ParallelGS::GSInterface;
using ParallelGS::GSOptions;
using ParallelGS::SignalInterface;
using ParallelGS::SuperSampling;
using ParallelGS::VSyncInfo;
using Vulkan::BufferCreateInfo;
using Vulkan::BufferDomain;
using Vulkan::Context;
using Vulkan::Device;
using Vulkan::Fence;

constexpr std::uint32_t kPrivilegedBase = 0x12000000u;
constexpr std::uint32_t kPrivilegedLimit = 0x12002000u; // exclusive
constexpr std::uint32_t kFrameContexts = 4;

// Forwards paraLLEl-GS signals to the optional handler installed by the caller.
class SignalBridge final : public SignalInterface {
public:
    SignalHandler *handler = nullptr;

    bool on_signal(std::uint64_t payload) override { return handler ? handler->on_signal(payload) : false; }
    bool on_finish(std::uint64_t payload) override { return handler ? handler->on_finish(payload) : false; }
    bool on_label(std::uint64_t payload) override { return handler ? handler->on_label(payload) : false; }
};

SuperSampling super_sampling_from_factor(std::uint32_t factor, bool &supported) {
    supported = true;
    switch (factor) {
    case 1: return SuperSampling::X1;
    case 2: return SuperSampling::X2;
    case 4: return SuperSampling::X4;
    case 8: return SuperSampling::X8;
    case 16: return SuperSampling::X16;
    default: supported = false; return SuperSampling::X1;
    }
}

} // namespace

// Optional raw stream capture for offline analysis (slice 167). GT4_PGS_CAPTURE names the output
// file; GT4_PGS_CAPTURE_FRAMES=first:last limits it to that range of next_frame() counts (default:
// every frame). Records, little-endian: 'G' u8 path, u32 bytes, data; 'P' u32 address, u64 value;
// 'R' u32 qwords (LOCAL->HOST FIFO read); 'V' u64 frame number (written at next_frame()).
class StreamCapture {
public:
    void open_from_environment() {
        if (const char *directory = std::getenv("GT4_PGS_VRAM_DUMP"))
            vram_directory = directory;
        const char *path = std::getenv("GT4_PGS_CAPTURE");
        if (!path || !*path)
            return;
        file = std::fopen(path, "wb");
        if (!file) {
            std::fprintf(stderr, "[pgs] GT4_PGS_CAPTURE=%s cannot be opened; capture off\n", path);
            return;
        }
        if (const char *range = std::getenv("GT4_PGS_CAPTURE_FRAMES")) {
            unsigned long long first_value = 0, last_value = 0;
            if (std::sscanf(range, "%llu:%llu", &first_value, &last_value) != 2 || last_value < first_value) {
                std::fprintf(stderr, "[pgs] GT4_PGS_CAPTURE_FRAMES=\"%s\" is not first:last; refusing to start\n", range);
                std::exit(EXIT_FAILURE);
            }
            first = first_value;
            last = last_value;
        }
        std::fprintf(stderr, "[pgs] capturing the GS stream to %s (frames %llu to %llu)\n", path,
                     static_cast<unsigned long long>(first), static_cast<unsigned long long>(last));
    }
    bool active() const { return file && frame >= first && frame <= last; }
    void gif(std::uint32_t path, const void *data, std::size_t bytes) {
        if (!active())
            return;
        const std::uint8_t path_byte = static_cast<std::uint8_t>(path);
        const std::uint32_t size = static_cast<std::uint32_t>(bytes);
        std::fputc('G', file);
        std::fwrite(&path_byte, 1, 1, file);
        std::fwrite(&size, 4, 1, file);
        std::fwrite(data, 1, bytes, file);
    }
    void privileged(std::uint32_t address, std::uint64_t value) {
        if (!active())
            return;
        std::fputc('P', file);
        std::fwrite(&address, 4, 1, file);
        std::fwrite(&value, 8, 1, file);
    }
    void fifo_read(std::uint32_t words) {
        if (!active())
            return;
        std::fputc('R', file);
        std::fwrite(&words, 4, 1, file);
    }
    // GT4_PGS_VRAM_DUMP=<directory>: every whole-VRAM map (the display dump path) is also written to
    // <directory>/vram_<frame>.bin, 4 MiB in paraLLEl-GS (GS local memory) byte order.
    void vram(const void *data, std::size_t size) {
        if (vram_directory.empty() || !data)
            return;
        const std::string path = vram_directory + "/vram_" + std::to_string(frame) + ".bin";
        if (std::FILE *dump = std::fopen(path.c_str(), "wb")) {
            std::fwrite(data, 1, size, dump);
            std::fclose(dump);
        }
    }
    void next_frame() {
        if (active()) {
            std::fputc('V', file);
            std::fwrite(&frame, 8, 1, file);
            std::fflush(file);
        }
        ++frame;
    }

private:
    std::FILE *file = nullptr;
    std::uint64_t first = 0;
    std::uint64_t last = ~0ull;
    std::uint64_t frame = 0;
    std::string vram_directory;
};

struct Backend::Impl {
    Context context;
    Device device;
    GSInterface gs;
    SignalBridge bridge;
    StreamCapture capture;
    bool ready = false;
};

Backend::Backend() : impl(std::make_unique<Impl>()) {}

Backend::~Backend() {
    if (impl && impl->ready)
        impl->device.wait_idle();
}

bool Backend::init(std::string &error) {
    if (impl->ready) {
        error = "backend already initialised";
        return false;
    }
    if (!Context::init_loader(nullptr)) {
        error = "Vulkan loader unavailable";
        return false;
    }
    impl->context.set_num_thread_indices(1);
    const auto features = Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
                                   Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
                                   Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT;
    if (!impl->context.init_instance_and_device(nullptr, 0, nullptr, 0, features)) {
        error = "Granite context initialisation failed (no suitable Vulkan device)";
        return false;
    }
    impl->device.set_context(impl->context);
    impl->device.init_frame_contexts(kFrameContexts);

    GSOptions options = {};
    options.super_sampling = SuperSampling::X1;
    options.dynamic_super_sampling = true;
    if (!impl->gs.init(&impl->device, options)) {
        error = "paraLLEl-GS rejected this device (feature check failed; see the log above)";
        return false;
    }
    impl->gs.set_signal_interface(&impl->bridge);
    impl->capture.open_from_environment();
    impl->ready = true;
    return true;
}

bool Backend::initialised() const { return impl->ready; }

void Backend::set_signal_handler(SignalHandler *handler) { impl->bridge.handler = handler; }

bool Backend::set_super_sampling(std::uint32_t factor) {
    bool supported = false;
    const SuperSampling rate = super_sampling_from_factor(factor, supported);
    if (!supported || !impl->ready)
        return false;
    // Ordered grid and super-sampled textures stay off, matching the spike's measured configuration.
    impl->gs.set_super_sampling_rate(rate, true, false);
    return true;
}

void Backend::gif_transfer(std::uint32_t path, const void *data, std::size_t bytes) {
    impl->capture.gif(path, data, bytes);
    impl->gs.gif_transfer(path, data, bytes);
}

bool Backend::write_privileged(std::uint32_t guest_address, std::uint64_t value) {
    if (guest_address < kPrivilegedBase || guest_address >= kPrivilegedLimit)
        return false;
    impl->capture.privileged(guest_address, value);
    auto &priv = impl->gs.get_priv_register_state();
    const std::uint32_t index = (guest_address & 0xFFFu) >> 3;
    if (guest_address & 0x1000u)
        priv.qwords_hi[index] = value;
    else
        priv.qwords_lo[index] = value;
    return true;
}

std::uint64_t Backend::read_privileged(std::uint32_t guest_address) const {
    if (guest_address < kPrivilegedBase || guest_address >= kPrivilegedLimit)
        return 0;
    const auto &priv = impl->gs.get_priv_register_state();
    const std::uint32_t index = (guest_address & 0xFFFu) >> 3;
    return (guest_address & 0x1000u) ? priv.qwords_hi[index] : priv.qwords_lo[index];
}

void Backend::reset_gif_paths() {
    for (std::uint32_t path = 0; path < 4; ++path)
        impl->gs.get_gif_path(path) = ParallelGS::GIFPath{};
}

bool Backend::gif_busy(std::uint32_t path) {
    const auto &state = impl->gs.get_gif_path(path);
    return state.loop != state.tag.NLOOP;
}

void Backend::reset_context() { impl->gs.reset_context_state(); }

void Backend::clear_vram() {
    constexpr std::size_t kVramBytes = 4u << 20;
    void *vram = impl->gs.map_vram_write(0, kVramBytes);
    std::memset(vram, 0, kVramBytes);
    impl->gs.end_vram_write(0, kVramBytes);
}

void Backend::read_transfer_fifo(void *destination, std::uint32_t words) {
    impl->capture.fifo_read(words);
    impl->gs.read_transfer_fifo(destination, words);
}

const void *Backend::map_vram(std::size_t offset, std::size_t size) {
    const void *mapped = impl->gs.map_vram_read(offset, size);
    if (offset == 0 && size == (4u << 20))
        impl->capture.vram(mapped, size);
    return mapped;
}

void Backend::flush() { impl->gs.flush(); }

void Backend::next_frame() {
    impl->capture.next_frame();
    impl->device.next_frame_context();
}

void Backend::wait_idle() { impl->device.wait_idle(); }

ScanoutImage Backend::scanout(const ScanoutRequest &request) {
    ScanoutImage frame;
    VSyncInfo info = {};
    info.phase = request.phase;
    info.dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
    info.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    info.force_progressive = request.force_progressive;
    info.anti_blur = request.anti_blur;
    info.adapt_to_internal_horizontal_resolution = true;
    info.high_resolution_scanout = request.high_resolution;

    impl->gs.flush();
    const auto result = impl->gs.vsync(info);
    if (!result.image)
        return frame;

    frame.width = result.image->get_width();
    frame.height = result.image->get_height();
    BufferCreateInfo buffer_info = {};
    buffer_info.domain = BufferDomain::CachedHost;
    buffer_info.size = VkDeviceSize(frame.width) * frame.height * 4;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    auto buffer = impl->device.create_buffer(buffer_info);
    auto cmd = impl->device.request_command_buffer();
    cmd->copy_image_to_buffer(*buffer, *result.image, 0, {0, 0, 0}, {frame.width, frame.height, 1}, 0, 0,
                              {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                 VK_ACCESS_HOST_READ_BIT);
    Fence fence;
    impl->device.submit(cmd, &fence);
    fence->wait();
    const auto *mapped = static_cast<const std::uint8_t *>(
        impl->device.map_host_buffer(*buffer, Vulkan::MEMORY_ACCESS_READ_BIT));
    frame.rgba.assign(mapped, mapped + buffer_info.size);
    impl->device.unmap_host_buffer(*buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
    frame.valid = true;
    return frame;
}

} // namespace gt4::parallel_gs
