void flush() {
    if (g_mode == Mode::off) return;
    flush_all();
    submit();
}

void wait_idle() { flush(); g_vulkan.wait_idle(); }

// At scale > 1 each VRAM word holds SCALE x SCALE sub-samples. Guest-visible bytes are the
// first sub-sample of every word, produced by the resolve pass into g_scratch.
void download(std::uint8_t* memory, std::uint32_t offset, std::uint32_t bytes) {
    if (offset > kMemorySize || bytes > kMemorySize - offset) throw std::runtime_error("GS download outside VRAM");
    flush();
    if (g_scale == 1) {
        read_back(g_vram, bytes, memory + offset, offset);
        return;
    }
    dispatch_linear(kResolve, bytes / 4, push_params({offset / 4}));
    submit();
    read_back(g_scratch, bytes, memory + offset, offset);
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
    scale = g_scale;
    vram.resize(static_cast<std::size_t>(kMemorySize / 4) * g_scale * g_scale);
    read_back(g_vram, vram.size() * 4, vram.data());
    clut.resize(kClutEntries);
    read_back(g_clut, kClutEntries * 4, clut.data(), static_cast<std::uint64_t>(g_clut_slot) * kClutEntries * 4);
}

// Restores a checkpoint taken at any supported scale. A checkpoint at another scale is expanded
// by replicating each word into every sub-sample (the donor conversion).
void write_state(const std::vector<std::uint32_t>& vram, std::uint32_t scale, const std::vector<std::uint32_t>& clut) {
    const std::size_t words = kMemorySize / 4;
    const std::uint32_t from = scale * scale, to = g_scale * g_scale;
    if (scale < 1 || scale > kMaxScale || vram.size() != words * from || clut.size() != kClutEntries)
        throw std::runtime_error("Unsupported native Vulkan checkpoint GPU shape");
    wait_idle();
    if (from == to) {
        g_vulkan.write(g_vram, 0, vram.data(), vram.size() * 4);
    } else {
        std::vector<std::uint32_t> layers(words * to);
        for (std::size_t w = 0; w < words; ++w)
            for (std::uint32_t k = 0; k < to; ++k) layers[w * to + k] = vram[w * from];
        g_vulkan.write(g_vram, 0, layers.data(), layers.size() * 4);
    }
    g_vulkan.write(g_clut, static_cast<std::uint64_t>(g_clut_slot) * kClutEntries * 4, clut.data(), kClutEntries * 4);
    std::fill(std::begin(g_rendered), std::end(g_rendered), from == to && to > 1);
}

void stats() {
    std::fprintf(stderr, "[vulkan] %llu submits, dispatches:", static_cast<unsigned long long>(g_submits));
    for (int index = 0; index < kPipelines; ++index)
        std::fprintf(stderr, " %s=%llu", kEntryPoints[index], static_cast<unsigned long long>(g_dispatches[index]));
    std::fprintf(stderr, "\n");
    std::fprintf(stderr, "[vulkan] Win32 presentations=%llu\n", static_cast<unsigned long long>(g_vulkan.presentations()));
}

void check_display_source(const DisplaySource& source) {
    const std::uint64_t scaled_width = static_cast<std::uint64_t>(source.width) * g_scale;
    const std::uint64_t scaled_height = static_cast<std::uint64_t>(source.height) * g_scale;
    if (!source.width || !source.height || scaled_width * scaled_height > g_copy_words)
        throw std::runtime_error("GS display exceeds scratch capacity");
}

// Frame dumps are 1x by default: each output pixel is the rounded average of its SCALE x SCALE
// sub-samples. GT4_DUMP_NATIVE_SIZE=1 returns the scaled image instead (owner inspection).
bool display(const DisplaySource& source, std::vector<std::uint32_t>& rgba, std::uint32_t& width, std::uint32_t& height) {
    flush_all();
    check_display_source(source);
    const auto parameters = push_params({source.fbp, source.fbw, source.psm, source.dbx, source.dby,
                                        source.width, source.height, source.halved ? 1u : 0u});
    g_commands.push_back({kDisplay, (source.width * g_scale + 7) / 8, (source.height * g_scale + 7) / 8, parameters});
    submit();
    const std::uint32_t scaled_width = source.width * g_scale, scaled_height = source.height * g_scale;
    std::vector<std::uint32_t> samples(static_cast<std::size_t>(scaled_width) * scaled_height);
    read_back(g_scratch, samples.size() * 4, samples.data());
    const char* native_size = std::getenv("GT4_DUMP_NATIVE_SIZE");
    if (g_scale == 1 || (native_size && std::strcmp(native_size, "1") == 0)) {
        width = scaled_width;
        height = scaled_height;
        rgba = std::move(samples);
        return true;
    }
    width = source.width;
    height = source.height;
    rgba.assign(static_cast<std::size_t>(width) * height, 0);
    const std::uint32_t count = g_scale * g_scale;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t sums[3] = {0, 0, 0};
            for (std::uint32_t j = 0; j < g_scale; ++j)
                for (std::uint32_t i = 0; i < g_scale; ++i) {
                    const std::uint32_t p = samples[static_cast<std::size_t>(y * g_scale + j) * scaled_width + x * g_scale + i];
                    sums[0] += p & 0xFFu;
                    sums[1] += (p >> 8) & 0xFFu;
                    sums[2] += (p >> 16) & 0xFFu;
                }
            const std::uint32_t r = (sums[0] + count / 2) / count;
            const std::uint32_t g = (sums[1] + count / 2) / count;
            const std::uint32_t b = (sums[2] + count / 2) / count;
            rgba[static_cast<std::size_t>(y) * width + x] = 0xFF000000u | r | (g << 8) | (b << 16);
        }
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
void set_widescreen(bool enabled) {
    g_vulkan.set_aspect(enabled ? gt4::vulkan::Aspect::widescreen : gt4::vulkan::Aspect::standard);
    std::fprintf(stderr, "[vulkan] presentation aspect %s (match GT4 Screen Ratio option)\n", enabled ? "16:9" : "4:3");
}

bool present(const DisplaySource& source) {
    if (!has_surface()) return false;
    flush_all();
    check_display_source(source);
    static std::uint32_t previous_width = 0, previous_height = 0;
    static bool previous_halved = false;
    if (source.width != previous_width || source.height != previous_height || source.halved != previous_halved) {
        std::fprintf(stderr, "[vulkan] guest display %ux%u, half-height source=%u, presented at %ux%u\n", source.width, source.height,
                     source.halved ? 1u : 0u, source.width * g_scale, source.height * g_scale);
        previous_width = source.width;
        previous_height = source.height;
        previous_halved = source.halved;
    }
    const auto parameters = push_params({source.fbp, source.fbw, source.psm, source.dbx, source.dby,
                                        source.width, source.height, source.halved ? 1u : 0u});
    g_commands.push_back({kDisplay, (source.width * g_scale + 7) / 8, (source.height * g_scale + 7) / 8, parameters});
    return submit(source.width * g_scale, source.height * g_scale);
}

double busy_ms() { throw std::runtime_error("Native Vulkan GPU busy time is not measured"); }
