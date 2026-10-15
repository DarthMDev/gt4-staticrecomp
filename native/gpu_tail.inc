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

// ---- CoreGS 1.1 present-path post passes (slice 182). Shader: native/post/present.wgsl (entry "post").
// Each op is one dispatch; the parameter record layout is the one the shader reads (P_* constants).
constexpr std::uint32_t kPostCopy = 0, kPostWeave = 1, kPostBob = 2, kPostBlendTap = 3, kPostAdaptive = 4,
                        kPostHistoryCopy = 5, kPostFxaa = 6;
constexpr std::uint32_t kPostScratch = 0, kPostTemp = 1;
constexpr float kAdaptiveThreshold = 0.08f; // normalised per channel (spec 13.4)

struct PostHistoryState {
    std::uint32_t frame_words = 0; // words per history slot (the presented frame)
    bool has_output = false, has_even = false, has_odd = false;
};
PostHistoryState g_post_state;
std::uint32_t g_post_field = 0; // parity of the next interlaced field, toggled per interlaced presentation

std::uint32_t float_bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// Grows the history (three slots) and the temp image on demand; a new frame size resets the state.
void ensure_post_capacity(std::uint64_t frame_words, std::uint64_t output_words) {
    if (g_post_state.frame_words != frame_words)
        g_post_state = PostHistoryState{static_cast<std::uint32_t>(frame_words), false, false, false};
    if (g_post_history_words < 3 * frame_words) {
        wait_idle();
        g_vulkan.destroy_buffer(g_post_history);
        g_post_history = g_vulkan.create_buffer(std::max<std::uint64_t>(256, 3 * frame_words * 4));
        g_post_history_words = static_cast<std::uint32_t>(3 * frame_words);
        rebuild_group();
    }
    const std::uint64_t temp_words = std::max(frame_words, output_words);
    if (g_post_temp_words < temp_words) {
        wait_idle();
        g_vulkan.destroy_buffer(g_post_temp);
        g_post_temp = g_vulkan.create_buffer(std::max<std::uint64_t>(256, temp_words * 4));
        g_post_temp_words = static_cast<std::uint32_t>(temp_words);
        rebuild_group();
    }
}

void push_post_pass(std::uint32_t op, std::uint32_t input, std::uint32_t output, std::uint32_t width, std::uint32_t height,
                    std::uint32_t out_width, std::uint32_t out_height, std::uint32_t field, std::uint32_t shift,
                    std::uint32_t flags, std::uint32_t slot) {
    const std::uint32_t parameters = push_params({op, width, height, input, output, field, shift, flags,
                                                  float_bits(kAdaptiveThreshold), out_width, out_height,
                                                  width * height, slot});
    g_commands.push_back({kPost, (out_width + 7) / 8, (out_height + 7) / 8, parameters});
}

bool post_active(const PostSettings& settings, bool interlaced) {
    const bool deinterlace = interlaced && settings.deinterlace != PostDeinterlace::off;
    return deinterlace || settings.fxaa;
}

// Appends the post passes for the frame in scratch and returns the buffer that holds the result.
// Deinterlace runs only for really interlaced output; automatic picks adaptive then, off otherwise.
std::uint32_t run_post_passes(const PostSettings& settings, std::uint32_t width, std::uint32_t height, bool interlaced,
                              std::uint32_t field, std::uint32_t& out_width, std::uint32_t& out_height) {
    PostDeinterlace mode = settings.deinterlace;
    if (mode == PostDeinterlace::automatic) mode = PostDeinterlace::adaptive;
    if (!interlaced) mode = PostDeinterlace::off;
    std::uint32_t current = kPostScratch;
    const std::uint32_t flags = (g_post_state.has_output ? 1u : 0u) | (g_post_state.has_even ? 2u : 0u) |
                                (g_post_state.has_odd ? 4u : 0u);
    // Bob shifts by SCALE * (1 - field) rows (spec 13.4).
    const std::uint32_t bob_shift = g_scale * (1u - field);
    switch (mode) {
    case PostDeinterlace::off:
    case PostDeinterlace::automatic:
        break;
    case PostDeinterlace::weave:
        push_post_pass(kPostWeave, kPostScratch, kPostTemp, width, height, width, height, field, 0, flags, 0);
        g_post_state.has_output = true;
        current = kPostTemp;
        break;
    case PostDeinterlace::bob:
        push_post_pass(kPostBob, kPostScratch, kPostTemp, width, height, width, height, field, bob_shift, flags, 0);
        current = kPostTemp;
        break;
    case PostDeinterlace::blend:
        push_post_pass(kPostWeave, kPostScratch, kPostTemp, width, height, width, height, field, 0, flags, 0);
        g_post_state.has_output = true;
        push_post_pass(kPostBlendTap, kPostTemp, kPostScratch, width, height, width, height, field, 0, flags, 0);
        current = kPostScratch;
        break;
    case PostDeinterlace::adaptive:
        push_post_pass(kPostAdaptive, kPostScratch, kPostTemp, width, height, width, height, field, 0, flags, 0);
        push_post_pass(kPostHistoryCopy, kPostScratch, kPostTemp, width, height, width, height, field, 0, flags, field);
        g_post_state.has_output = true;
        if (field == 0) g_post_state.has_even = true;
        else g_post_state.has_odd = true;
        current = kPostTemp;
        break;
    }
    if (settings.fxaa) {
        const std::uint32_t other = current == kPostScratch ? kPostTemp : kPostScratch;
        push_post_pass(kPostFxaa, current, other, width, height, width, height, field, 0, flags, 0);
        current = other;
    }
    out_width = width;
    out_height = height;
    return current;
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
    const std::uint32_t width = source.width * g_scale, height = source.height * g_scale;
    if (!post_active(g_post_settings, source.halved))
        return submit(width, height);
    const std::uint32_t field = g_post_field;
    if (source.halved) g_post_field ^= 1u;
    ensure_post_capacity(static_cast<std::uint64_t>(width) * height, static_cast<std::uint64_t>(width) * height);
    std::uint32_t out_width = 0, out_height = 0;
    const std::uint32_t result = run_post_passes(g_post_settings, width, height, source.halved, field, out_width, out_height);
    return submit(out_width, out_height, result == kPostScratch ? g_scratch : g_post_temp);
}

// Test entry (native/post/post_test.hpp): one frame through the post passes; state is kept between calls.
bool post_frame_for_test(const std::vector<std::uint32_t>& pixels, std::uint32_t width, std::uint32_t height,
                         bool interlaced, std::uint32_t field, const PostSettings& settings, std::vector<std::uint32_t>& result) {
    const std::uint64_t frame = static_cast<std::uint64_t>(width) * height;
    if (!width || !height || pixels.size() != frame || frame > g_copy_words)
        throw std::runtime_error("post test frame has the wrong size");
    wait_idle();
    g_vulkan.write(g_scratch, 0, pixels.data(), frame * 4);
    ensure_post_capacity(frame, frame);
    std::uint32_t out_width = 0, out_height = 0;
    const std::uint32_t which = run_post_passes(settings, width, height, interlaced, field & 1u, out_width, out_height);
    submit();
    wait_idle();
    result.resize(frame);
    read_back(which == kPostScratch ? g_scratch : g_post_temp, frame * 4, result.data());
    return true;
}

void post_reset_for_test() {
    g_post_state = PostHistoryState{};
    g_post_field = 0;
}

double busy_ms() { throw std::runtime_error("Native Vulkan GPU busy time is not measured"); }
