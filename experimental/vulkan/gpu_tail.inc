void flush() {
    if (g_mode == Mode::off) return;
    flush_all();
    submit();
}

void wait_idle() { flush(); g_vulkan.wait_idle(); }

void download(std::uint8_t* memory, std::uint32_t offset, std::uint32_t bytes) {
    if (offset > kMemorySize || bytes > kMemorySize - offset) throw std::runtime_error("GS download outside VRAM");
    flush();
    read_back(g_vram, bytes, memory + offset, offset);
}

std::uint32_t g_completed_serial = 0;
bool g_has_completed_download = false;

bool download_async(std::uint8_t* memory, std::uint32_t offset, std::uint32_t bytes, std::uint32_t serial) {
    // shortcut: completed readback is synchronous; add fences per request before overlapping GPU work.
    download(memory, offset, bytes);
    g_completed_serial = serial;
    g_has_completed_download = true;
    return true;
}

bool finish_download(std::uint32_t serial) {
    if (!g_has_completed_download || serial != g_completed_serial) return false;
    g_has_completed_download = false;
    return true;
}

void read_state(std::vector<std::uint32_t>& vram, std::uint32_t& scale, std::vector<std::uint32_t>& clut) {
    flush();
    scale = 1;
    vram.resize(kMemorySize / 4);
    read_back(g_vram, kMemorySize, vram.data());
    clut.resize(kClutEntries);
    read_back(g_clut, kClutEntries * 4, clut.data(), static_cast<std::uint64_t>(g_clut_slot) * kClutEntries * 4);
}

void write_state(const std::vector<std::uint32_t>& vram, std::uint32_t scale, const std::vector<std::uint32_t>& clut) {
    if (scale != 1 || vram.size() != kMemorySize / 4 || clut.size() != kClutEntries)
        throw std::runtime_error("Unsupported native Vulkan checkpoint GPU shape");
    wait_idle();
    g_vulkan.write(g_vram, 0, vram.data(), kMemorySize);
    g_vulkan.write(g_clut, static_cast<std::uint64_t>(g_clut_slot) * kClutEntries * 4, clut.data(), kClutEntries * 4);
}

void stats() {
    std::fprintf(stderr, "[vulkan] %llu submits, dispatches:", static_cast<unsigned long long>(g_submits));
    for (int index = 0; index < kPipelines; ++index)
        std::fprintf(stderr, " %s=%llu", kEntryPoints[index], static_cast<unsigned long long>(g_dispatches[index]));
    std::fprintf(stderr, "\n");
    std::fprintf(stderr, "[vulkan] Win32 presentations=%llu\n", static_cast<unsigned long long>(g_vulkan.presentations()));
}

bool display(const DisplaySource& source, std::vector<std::uint32_t>& rgba, std::uint32_t& width, std::uint32_t& height) {
    flush_all();
    if (!source.width || !source.height || static_cast<std::uint64_t>(source.width) * source.height > g_copy_words)
        throw std::runtime_error("GS display exceeds scratch capacity");
    const auto parameters = push_params({source.fbp, source.fbw, source.psm, source.dbx, source.dby,
                                        source.width, source.height, source.halved ? 1u : 0u});
    g_commands.push_back({kDisplay, (source.width + 7) / 8, (source.height + 7) / 8, parameters});
    submit();
    width = source.width;
    height = source.height;
    rgba.resize(static_cast<std::size_t>(width) * height);
    read_back(g_scratch, rgba.size() * 4, rgba.data());
    return true;
}

bool display_async(const DisplaySource& source, std::vector<std::uint32_t>& rgba, std::uint32_t& width, std::uint32_t& height) {
    return display(source, rgba, width, height);
}

bool attach_surface(const SurfaceTarget& target) {
    if (target.kind != SurfaceTarget::Win32) throw std::runtime_error("Native Vulkan supports only Win32 surfaces");
    g_vulkan.attach_window(reinterpret_cast<HWND>(target.window), target.width, target.height);
    return true;
}

void resize_surface(std::uint32_t width, std::uint32_t height) { g_vulkan.resize(width, height); }
bool has_surface() { return g_vulkan.has_window(); }

bool present(const DisplaySource& source) {
    if (!has_surface()) return false;
    flush_all();
    if (!source.width || !source.height || static_cast<std::uint64_t>(source.width) * source.height > g_copy_words)
        throw std::runtime_error("GS present exceeds scratch capacity");
    const auto parameters = push_params({source.fbp, source.fbw, source.psm, source.dbx, source.dby,
                                        source.width, source.height, source.halved ? 1u : 0u});
    g_commands.push_back({kDisplay, (source.width + 7) / 8, (source.height + 7) / 8, parameters});
    submit();
    return g_vulkan.present(g_scratch, source.width, source.height);
}

double busy_ms() { throw std::runtime_error("Native Vulkan GPU busy time is not measured"); }
