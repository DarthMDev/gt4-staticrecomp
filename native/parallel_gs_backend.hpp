// Headless paraLLEl-GS backend for the GT4Recomp GS front end (TASK-012, slice 162).
//
// This header is deliberately free of Vulkan, Granite and paraLLEl-GS types. Only
// parallel_gs_backend.cpp includes the Granite headers, so Granite's Vulkan header
// version never meets the PCSX2-tree headers used by the donor-derived runtime.
//
// Scope of this step: device creation, GIF transfers, privileged register mirroring
// by guest address, signals, transfer-FIFO readback, VRAM mapping and scanout readback.
// Presentation to a window, the GS front-end hooks and save states are later steps.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gt4::parallel_gs {

// Receives SIGNAL, FINISH and LABEL packets inline on the calling thread, as paraLLEl-GS
// does. Returning true asks the backend to flush before the caller continues.
class SignalHandler {
public:
    virtual ~SignalHandler() = default;
    virtual bool on_signal(std::uint64_t payload) = 0;
    virtual bool on_finish(std::uint64_t payload) = 0;
    virtual bool on_label(std::uint64_t payload) = 0;
};

struct ScanoutRequest {
    std::uint32_t phase = 0;
    bool force_progressive = true;
    bool anti_blur = true;
    // Requests the super-sampled high-resolution scanout. paraLLEl-GS ignores it below 4x SSAA.
    bool high_resolution = false;
    // Scale per axis when high_resolution is set: 2 (donor path, 4x SSAA or more), 3 or 4 (16x SSAA
    // only, progressive frames; field-aware frames fall back to 2). Slice 176.
    std::uint32_t high_resolution_scale = 2;
};

// Window presentation (slice 168, step 6). The swapchain belongs to Granite's device: paraLLEl-GS
// renders on that device, so its scanout image can be blitted to the swapchain without leaving the GPU.
enum class PresentMode { fifo, mailbox, immediate };

struct WindowTarget {
    void *window = nullptr; // HWND of the existing game window
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PresentMode mode = PresentMode::fifo;
};

struct PresentationRect {
    std::uint32_t left = 0, top = 0, width = 0, height = 0;
};

// Largest centred 16:9 rectangle inside a width x height surface, with the same arithmetic as
// gt4::vulkan::presentation_rect (native_vulkan.cpp, Aspect::widescreen) so both renderers place the
// image identically. Zero-sized surfaces give an empty rectangle.
constexpr PresentationRect widescreen_rect(std::uint32_t width, std::uint32_t height) {
    if (!width || !height)
        return {};
    const std::uint64_t by_height = std::uint64_t(height) * 16 / 9;
    const std::uint64_t by_width = std::uint64_t(width) * 9 / 16;
    const std::uint64_t fitted_width_wide = by_height < width ? by_height : width;
    const std::uint64_t fitted_height_wide = by_width < height ? by_width : height;
    const std::uint32_t fitted_width = static_cast<std::uint32_t>(fitted_width_wide ? fitted_width_wide : 1);
    const std::uint32_t fitted_height = static_cast<std::uint32_t>(fitted_height_wide ? fitted_height_wide : 1);
    return {(width - fitted_width) / 2, (height - fitted_height) / 2, fitted_width, fitted_height};
}

// Blit filter for the window: nearest for the default 1x path and for exact integer upscales of a
// high-resolution image, linear for any other scale (downscale into a small window, or non-integer).
enum class PresentFilter { nearest, linear };
constexpr PresentFilter present_filter(bool high_resolution, std::uint32_t image_width, std::uint32_t image_height,
                                       const PresentationRect &rect) {
    if (!high_resolution || !image_width || !image_height)
        return PresentFilter::nearest;
    const bool integer_upscale = rect.width >= image_width && rect.height >= image_height &&
                                 rect.width % image_width == 0 && rect.height % image_height == 0 &&
                                 rect.width / image_width == rect.height / image_height;
    return integer_upscale ? PresentFilter::nearest : PresentFilter::linear;
}

// Destination of the scanout inside the 16:9 rectangle. In 480i frame mode (FFMD=1) paraLLEl-GS's
// field-aware high-resolution scanout drops the last line on purpose (2 x 224 - 1 = 447 lines, upstream
// "avoid a one line flicker"). Stretching 447 lines over the full rectangle would scale the picture
// 0.2 percent taller than the 448-line 1x and 480p images; instead the image keeps the 448-line scale
// and the missing bottom line stays black. Even heights, and the 1x path, use the whole rectangle.
constexpr PresentationRect high_resolution_target(const PresentationRect &rect, std::uint32_t image_height,
                                                  bool high_resolution) {
    if (!high_resolution || !image_height || (image_height & 1u) == 0)
        return rect;
    PresentationRect target = rect;
    target.height = static_cast<std::uint32_t>((std::uint64_t(rect.height) * image_height + (image_height + 1) / 2) /
                                               (image_height + 1));
    return target;
}

struct ScanoutImage {
    // False when paraLLEl-GS produced no image, for example for SMODE1 = 0 ("Unknown video format").
    bool valid = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Scale per axis paraLLEl-GS actually used: 1 for the 1x image, otherwise the high-resolution scale
    // (it can be lower than requested, for example 2 for field-aware frames). Slice 176.
    std::uint32_t high_resolution_scale = 1;
    // Tightly packed RGBA8 rows, width * height * 4 bytes.
    std::vector<std::uint8_t> rgba;
};

class Backend {
public:
    Backend();
    ~Backend();
    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;

    // Creates the headless Granite context, device and paraLLEl-GS interface.
    // On failure returns false and explains why in `error`; the object stays unusable.
    bool init(std::string &error);
    bool initialised() const;

    // The handler is not owned; it must outlive the backend or be cleared first.
    void set_signal_handler(SignalHandler *handler);

    // Super-sampling factor 1, 2, 4, 8 or 16 (paraLLEl-GS X1..X16). Returns false when unsupported.
    // Scale 3 and 4 of the high-resolution scanout need 16 (ordered 4x4 grid), see the scanout request.
    bool set_super_sampling(std::uint32_t factor);

    // GIF packet stream for one path (1, 2 or 3); state persists between calls like the donor GIF.
    void gif_transfer(std::uint32_t path, const void *data, std::size_t bytes);

    // Mirrors a privileged register write into paraLLEl-GS's register state. Returns false, and
    // stores nothing, for addresses outside 0x12000000-0x12001FFF.
    bool write_privileged(std::uint32_t guest_address, std::uint64_t value);
    // Returns the mirrored privileged register value (0 if never written or out of range).
    std::uint64_t read_privileged(std::uint32_t guest_address) const;

    // Clears every GIF path's tag and loop state (GIF reset or GS reset).
    void reset_gif_paths();
    // True while path `path` (0..3) still expects packets of its current GIF tag.
    bool gif_busy(std::uint32_t path);
    // Resets paraLLEl-GS register and context state; VRAM is not touched.
    void reset_context();
    // Zeroes all 4 MiB of VRAM through paraLLEl-GS's page-tracked host mapping.
    void clear_vram();

    // Reads `words` 128-bit words of the transfer FIFO filled by a LOCAL->HOST transfer.
    // The buffer must hold words * 16 bytes.
    void read_transfer_fifo(void *destination, std::uint32_t words);

    // Maps VRAM bytes [offset, offset + size) for read. Returns nullptr when unavailable.
    const void *map_vram(std::size_t offset, std::size_t size);

    // Submits pending work without waiting for it.
    void flush();
    // Advances the Granite frame context; call once per emulated frame.
    void next_frame();
    // Blocks until all submitted GPU work has completed.
    void wait_idle();

    // Builds the scanout image from the mirrored privileged registers and copies it to host memory.
    ScanoutImage scanout(const ScanoutRequest &request);

    // Creates a Granite WSI swapchain on the window (FIFO unless `mode` says otherwise, UNORM,
    // transfer-destination images). Returns false and explains why in `error`.
    bool attach_window(const WindowTarget &target, std::string &error);
    bool has_window() const;
    // Records the new client size; the swapchain is rebuilt before the next present.
    void resize_window(std::uint32_t width, std::uint32_t height);
    // One presentation per guest VBlank: acquires a swapchain image (this also advances the Granite
    // frame context), builds the scanout for `phase`, clears the image to black and blits the scanout
    // into the 16:9 rectangle, then presents. Returns false when nothing was presented (no window,
    // zero-sized window, or no scanout image this frame).
    // `high_resolution` requests paraLLEl-GS's high-resolution scanout (2x per axis from the super
    // samples; needs 4x SSAA or more, otherwise paraLLEl-GS returns the 1x image). The 1x image is
    // blitted with nearest filtering (as the donor presenter); a high-resolution image uses nearest
    // only for an exact integer upscale and linear filtering otherwise (slice 169, present_filter).
    // `high_resolution_scale` (slice 176) is 2, 3 or 4 and applies only when `high_resolution` is set.
    bool present(std::uint32_t phase, bool high_resolution = false, std::uint32_t high_resolution_scale = 2);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gt4::parallel_gs
