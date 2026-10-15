// PS2_SCALE selects internal resolution: each pixel holds SCALE x SCALE GS sub-samples.
// The SCALE override is folded into the SPIR-V (tools/wgsl_spirv), so each scale has its own
// shader directory: scale 1 is the base directory, scale N is a sibling "<base>-sN".
// Supported values: 1 to kMaxScale.
constexpr std::uint32_t kMaxScale = 4;

std::uint32_t parse_scale(const char* text) {
    if (!text) return 1;
    if (!text[0] || text[1] || text[0] < '1' || text[0] > '0' + static_cast<char>(kMaxScale))
        throw std::runtime_error(std::string("Native Vulkan supports PS2_SCALE 1 to ") + std::to_string(kMaxScale) + ", got: " + text);
    return static_cast<std::uint32_t>(text[0] - '0');
}

std::filesystem::path shader_directory_for_scale(std::uint32_t scale) {
    const std::filesystem::path base(GT4_VULKAN_SHADER_DIR);
    if (scale == 1) return base;
    return base.parent_path() / (base.filename().string() + "-s" + std::to_string(scale));
}

// Presentation of a scaled image: linear filtering by default when scaled, nearest at scale 1.
// GT4_PRESENT_FILTER=nearest|linear overrides for both.
bool parse_linear_present(std::uint32_t scale) {
    const char* filter = std::getenv("GT4_PRESENT_FILTER");
    if (!filter) return scale > 1;
    if (std::strcmp(filter, "linear") == 0) return true;
    if (std::strcmp(filter, "nearest") == 0) return false;
    throw std::runtime_error(std::string("GT4_PRESENT_FILTER must be linear or nearest, got: ") + filter);
}

bool init(Mode requested) {
    if (requested == Mode::off) throw std::runtime_error("Native Vulkan pilot requires GPU rendering");
    g_scale = parse_scale(std::getenv("PS2_SCALE"));
    const std::uint32_t samples = g_scale * g_scale;
    if (std::getenv("PS2_GPU_PROFILE")) throw std::runtime_error("Native Vulkan GPU timestamps are not implemented");
    g_copy_words = 2048u * 2048u * samples;
    g_field_words = (1u << 19) * samples;
    g_vulkan.initialize(shader_directory_for_scale(g_scale));
    g_vulkan.set_linear_present(parse_linear_present(g_scale));
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
