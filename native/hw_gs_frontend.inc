// TASK-027 M0b: HWR front end, selected by GT4_RENDERER=hw (chapter 12.2).
// Spliced after the paraLLEl-GS front end by scripts/prepare_external_vulkan.py --renderer-hooks.
// In this mode neither the donor Vulkan renderer nor the paraLLEl-GS backend is created.
// M0 scope: the backend is started and presents black; the GS data path is still a stub.

std::uint64_t g_hw_vblanks = 0;

// Created on first use and never destroyed: the Vulkan device must not be torn down at exit.
gt4::hw::Backend &hw_backend() {
    static gt4::hw::Backend *backend = [] {
        auto *created = new gt4::hw::Backend;
        const gt4::hw::Options options = process_hw_options();
        std::string error;
        if (!created->init(options, error)) {
            std::fprintf(stderr, "[hw] HWR start failed: %s\n", error.c_str());
            std::exit(EXIT_FAILURE);
        }
        std::fprintf(stderr, "[hw] GT4_RENDERER=hw: HWR backend started, donor Vulkan renderer not created\n");
        for (const std::string &line : gt4::hw::describe_options(options))
            std::fprintf(stderr, "[hw] %s\n", line.c_str());
        return created;
    }();
    return *backend;
}

void hw_reset() {
    gt4::hw::Backend &backend = hw_backend();
    backend.reset_context();
    backend.reset_gif_paths();
    backend.clear_vram();
}

void hw_reset_gif() { hw_backend().reset_gif_paths(); }

void hw_gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
    hw_backend().gif_transfer(static_cast<std::uint32_t>(path), data, static_cast<std::size_t>(qwc) * 16);
}

bool hw_gif_busy(int path) { return hw_backend().gif_busy(static_cast<std::uint32_t>(path)); }

void hw_read_image(std::uint8_t *data, std::uint32_t qwords) { hw_backend().read_transfer_fifo(data, qwords); }

void hw_wait() { hw_backend().wait_idle(); }

// Once per guest VBlank. Headless runs advance the HWR frame here; with a window, present() does.
void hw_vblank() {
    gt4::hw::Backend &backend = hw_backend();
    ++g_hw_vblanks;
    backend.flush();
    if (!backend.has_window())
        backend.next_frame();
}

bool hw_present() {
    hw_backend().present(g_odd_field ? gt4::hw::PresentPhase::field_odd : gt4::hw::PresentPhase::field_even);
    return true;
}

bool hw_attach_window(std::uint64_t window) {
    std::string error;
    if (!hw_backend().attach_window(reinterpret_cast<void *>(window), error)) {
        std::fprintf(stderr, "[hw] window presentation refused: %s\n", error.c_str());
        return false;
    }
    std::fprintf(stderr, "[hw] window output through the HWR swapchain (black frames at M0)\n");
    return true;
}

void hw_resize_window(std::uint32_t width, std::uint32_t height) { hw_backend().resize_window(width, height); }

void hw_mirror_privileged(std::uint32_t address, std::uint64_t value) {
    if (!hw_backend().write_privileged(address, value))
        std::fprintf(stderr, "[hw] privileged write 0x%08X is outside the HWR range; not mirrored\n", address);
}

// M0: HWR does not yet write GS memory back, so the donor's copy of GS memory is cleared and the donor's
// own decode yields the black frame at the size the display registers describe.
void hw_sync_memory() {
    init(); // swizzle tables for read_pixel; builds once, then returns
    // M4: the shadow local memory is the GS memory (write-back mode keeps it coherent after each batch).
    const std::uint8_t *shadow = hw_backend().map_vram(0, kMemorySize);
    if (shadow)
        std::memcpy(g_memory, shadow, kMemorySize);
    else
        std::memset(g_memory, 0, kMemorySize);
}

bool hw_options_dump_scanout() {
    static const bool scanout = process_hw_options().dump_source == gt4::hw::DumpSource::scanout;
    return scanout;
}

bool hw_scanout_frame(std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height) {
    gt4::hw::ScanoutRequest request;
    request.phase = g_odd_field ? gt4::hw::PresentPhase::field_odd : gt4::hw::PresentPhase::field_even;
    if (!hw_backend().scanout(request))
        return false; // M0: no image, so no frame is dumped for this display
    width = request.width;
    height = request.height;
    rgba = request.pixels;
    return true;
}
