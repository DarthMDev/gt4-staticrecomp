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
    // Requests the 4x+ super-sampled scanout. paraLLEl-GS ignores it below 4x SSAA.
    bool high_resolution = false;
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

struct ScanoutImage {
    // False when paraLLEl-GS produced no image, for example for SMODE1 = 0 ("Unknown video format").
    bool valid = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
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
    // frame context), builds the 1x scanout for `phase`, clears the image to black and blits the
    // scanout into the 16:9 rectangle with nearest filtering, then presents. Returns false when
    // nothing was presented (no window, zero-sized window, or no scanout image this frame).
    bool present(std::uint32_t phase);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gt4::parallel_gs
