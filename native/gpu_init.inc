bool init(Mode requested) {
    if (requested == Mode::off) throw std::runtime_error("Native Vulkan pilot requires GPU rendering");
    if (const char* scale = std::getenv("PS2_SCALE")) {
        if (std::strcmp(scale, "1") != 0) throw std::runtime_error("Native Vulkan pilot supports only scale=1");
    }
    if (std::getenv("PS2_GPU_PROFILE")) throw std::runtime_error("Native Vulkan GPU timestamps are not implemented");
    g_vulkan.initialize(GT4_VULKAN_SHADER_DIR);
    g_tiles_per_row = 128;
    g_tile_count.assign(static_cast<std::size_t>(g_tiles_per_row) * g_tiles_per_row, 0);
    g_tile_fill.assign(static_cast<std::size_t>(g_tiles_per_row) * g_tiles_per_row, 0);
    g_vram = g_vulkan.create_buffer(kMemorySize);
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
    std::fprintf(stderr, "[vulkan] GS mode=%s; CPU raster=%s\n",
                 requested == Mode::primary ? "primary" : "mirror",
                 requested == Mode::primary ? "disabled" : "enabled for comparison");
    return true;
}
