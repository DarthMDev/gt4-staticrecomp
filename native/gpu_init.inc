// PS2_SCALE selects internal resolution: each pixel holds SCALE x SCALE GS sub-samples.
// The parser is shared with the window host (native/coregs_options.hpp), so both read it the same way.
// The SCALE override is folded into the SPIR-V (tools/wgsl_spirv), so each scale has its own
// shader directory: scale 1 is the base directory, scale N is a sibling "<base>-sN".
constexpr std::uint32_t kMaxScale = gt4::coregs::kMaxScale;

std::uint32_t parse_scale(const char* text) { return gt4::coregs::parse_scale(text); }

std::filesystem::path shader_directory_for_scale(std::uint32_t scale) {
    const std::filesystem::path base(GT4_VULKAN_SHADER_DIR);
    if (scale == 1) return base;
    return base.parent_path() / (base.filename().string() + "-s" + std::to_string(scale));
}

// CoreGS 1.1 presentation options (launcher and env table in docs/BUILD.md, slice 182).
// Deinterlace is active only when the source is really interlaced (the donor's halved read); auto = adaptive
// then, off for progressive output. The filter is nearest at scale 1 and linear when scaled, unless set.
PostSettings g_post_settings;

PostDeinterlace parse_deinterlace(const char* text) {
    if (!text) return PostDeinterlace::automatic;
    const std::string value(text);
    if (value == "auto") return PostDeinterlace::automatic;
    if (value == "off") return PostDeinterlace::off;
    if (value == "weave") return PostDeinterlace::weave;
    if (value == "bob") return PostDeinterlace::bob;
    if (value == "blend") return PostDeinterlace::blend;
    if (value == "adaptive") return PostDeinterlace::adaptive;
    throw std::runtime_error("GT4_DEINTERLACE must be auto, off, weave, bob, blend or adaptive, got: " + value);
}

PostFilter parse_present_filter(const char* text) {
    if (!text) return PostFilter::automatic;
    const std::string value(text);
    if (value == "nearest") return PostFilter::nearest;
    if (value == "linear") return PostFilter::linear;
    if (value == "fsr") return PostFilter::fsr;
    throw std::runtime_error("GT4_PRESENT_FILTER must be nearest, linear or fsr, got: " + value);
}

PostSettings read_post_settings() {
    PostSettings settings;
    settings.deinterlace = parse_deinterlace(std::getenv("GT4_DEINTERLACE"));
    const char* aa = std::getenv("GT4_AA");
    if (aa && std::strcmp(aa, "fxaa") == 0) settings.fxaa = true;
    else if (aa && std::strcmp(aa, "off") != 0) throw std::runtime_error(std::string("GT4_AA must be off or fxaa, got: ") + aa);
    settings.filter = parse_present_filter(std::getenv("GT4_PRESENT_FILTER"));
    settings.sharpness = gt4::coregs::parse_fsr_sharpness_stops(std::getenv("GT4_FSR_SHARPNESS"));
    return settings;
}

// Linear sampling of the presented image: automatic is linear only when scaled. FSR presents 1:1 (nearest).
bool present_linear(const PostSettings& settings, std::uint32_t scale) {
    if (settings.filter == PostFilter::linear) return true;
    if (settings.filter == PostFilter::automatic) return scale > 1;
    return false;
}

bool init(Mode requested) {
    if (requested == Mode::off) throw std::runtime_error("Native Vulkan pilot requires GPU rendering");
    g_scale = parse_scale(std::getenv("PS2_SCALE"));
    const std::uint32_t samples = g_scale * g_scale;
    if (std::getenv("PS2_GPU_PROFILE")) throw std::runtime_error("Native Vulkan GPU timestamps are not implemented");
    g_copy_words = 2048u * 2048u * samples;
    g_field_words = (1u << 19) * samples;
    g_vulkan.initialize(shader_directory_for_scale(g_scale));
    g_post_settings = read_post_settings();
    g_vulkan.set_linear_present(present_linear(g_post_settings, g_scale));
    // Post-pass buffers start small and grow with the first presentation that needs them (see gpu_tail.inc).
    g_post_history = g_vulkan.create_buffer(256);
    g_post_temp = g_vulkan.create_buffer(256);
    g_post_history_words = 1;
    g_post_temp_words = 1;
    g_tiles_per_row = static_cast<int>((2048u * g_scale + 15u) / 16u);
    g_tile_count.assign(static_cast<std::size_t>(g_tiles_per_row) * g_tiles_per_row, 0);
    g_tile_fill.assign(static_cast<std::size_t>(g_tiles_per_row) * g_tiles_per_row, 0);
    g_vram = g_vulkan.create_buffer(static_cast<std::uint64_t>(kMemorySize) * samples);
    std::vector<std::uint32_t> tables;
    swizzle_tables(tables);
    g_tables = g_vulkan.create_buffer(tables.size() * 4);
    g_vulkan.write(g_tables, 0, tables.data(), tables.size() * 4);
    g_clut = g_vulkan.create_buffer(static_cast<std::uint64_t>(kClutSlots) * kClutEntries * 4);
    g_scratch = g_vulkan.create_buffer((static_cast<std::uint64_t>(g_copy_words) + 2ull * g_field_words) * 4);
    g_cache = g_vulkan.create_buffer(static_cast<std::uint64_t>(kCacheWords) * 4);
    g_decoded = g_vulkan.create_buffer(static_cast<std::uint64_t>(kDecodedWords) * 4);
    g_cache_copy.assign(kCacheWords, 0);
    if (const char* decode = std::getenv("PS2_DECODE")) g_decode = std::strcmp(decode, "0") != 0;
    rebuild_group();
    g_mode = requested;
    std::fprintf(stderr, "[vulkan] GS mode=%s; CPU raster=%s; internal scale=%ux (%ux%u sub-samples per pixel)\n",
                 requested == Mode::primary ? "primary" : "mirror",
                 requested == Mode::primary ? "disabled" : "enabled for comparison", g_scale, g_scale, g_scale);
    return true;
}
