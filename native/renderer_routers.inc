// TASK-027 M0b: the neutral entry points of chapter 12.2. Every alt_* hook in gs.cpp, gif.cpp and hw.cpp
// calls one of these, and each routes to the paraLLEl-GS or the HWR front end by the selected renderer.
// The hooks test alt_active() first, so these run only for parallel and hw.

void alt_reset() {
    if (hw_active()) hw_reset();
    else parallel_reset();
}

void alt_reset_gif() {
    if (hw_active()) hw_reset_gif();
    else parallel_reset_gif();
}

void alt_gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
    if (hw_active()) hw_gif_transfer(path, data, qwc);
    else parallel_gif_transfer(path, data, qwc);
}

bool alt_gif_busy(int path) { return hw_active() ? hw_gif_busy(path) : parallel_gif_busy(path); }

void alt_read_image(std::uint8_t *data, std::uint32_t qwords) {
    if (hw_active()) hw_read_image(data, qwords);
    else parallel_read_image(data, qwords);
}

void alt_wait() {
    if (hw_active()) hw_wait();
    else parallel_wait();
}

void alt_vblank() {
    if (hw_active()) hw_vblank();
    else parallel_vblank();
}

bool alt_present() { return hw_active() ? hw_present() : parallel_present(); }

// kind 3 is Win32; the donor presenter reports other kinds as unsupported.
bool alt_attach_window(int kind, std::uint64_t window, std::uint32_t width, std::uint32_t height) {
    if (kind != 3) return false;
    return hw_active() ? hw_attach_window(window) : parallel_attach_window(window, width, height);
}

void alt_resize_window(std::uint32_t width, std::uint32_t height) {
    if (hw_active()) hw_resize_window(width, height);
    else parallel_resize_window(width, height);
}

void alt_mirror_privileged(std::uint32_t address, std::uint64_t value) {
    if (hw_active()) hw_mirror_privileged(address, value);
    else parallel_mirror_privileged(address, value);
}

void alt_sync_memory() {
    if (hw_active()) hw_sync_memory();
    else parallel_sync_memory();
}

// GT4_PGS_DUMP_SOURCE for paraLLEl-GS, GT4_HW_DUMP_SOURCE for HWR (chapter 12.9).
bool alt_dump_scanout() {
    if (hw_active()) return hw_options_dump_scanout();
    return parallel_dump_scanout();
}

bool alt_scanout_frame(std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height) {
    return hw_active() ? hw_scanout_frame(rgba, width, height) : parallel_scanout_frame(rgba, width, height);
}
