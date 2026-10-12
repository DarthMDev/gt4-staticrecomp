void rebuild_group() {
    g_vulkan.bind_buffers({g_vram, g_tables, g_params, g_data, g_clut, g_scratch, g_cache, g_decoded});
}

void ensure_capacity() {
    bool changed = false;
    const std::uint64_t data_bytes = std::max<std::uint64_t>(g_arena.size() * 4, 16);
    if (data_bytes > g_data_size) {
        g_vulkan.destroy_buffer(g_data);
        g_data_size = std::max<std::uint64_t>(g_data_size * 2, (data_bytes + (4u << 20) - 1) & ~((4ull << 20) - 1));
        g_data = g_vulkan.create_buffer(g_data_size);
        changed = true;
    }
    const std::uint64_t param_bytes = std::max<std::uint64_t>(g_param_records.size(), 1) * kParamStride;
    if (param_bytes > g_params_size) {
        g_vulkan.destroy_buffer(g_params);
        g_params_size = std::max<std::uint64_t>(g_params_size * 2, std::max<std::uint64_t>(param_bytes, 64 * 1024));
        g_params = g_vulkan.create_buffer(g_params_size);
        changed = true;
    }
    if (changed) rebuild_group();
}

void submit() {
    if (g_commands.empty()) return;
    ensure_capacity();
    g_vulkan.write(g_data, 0, g_arena.data(), g_arena.size() * 4);
    g_vulkan.write(g_params, 0, g_param_records.data(), g_param_records.size() * kParamStride);
    std::vector<gt4::vulkan::Dispatch> commands;
    commands.reserve(g_commands.size());
    for (const auto& command : g_commands) {
        commands.push_back({static_cast<std::uint32_t>(command.pipeline), command.x, command.y, command.param});
        ++g_dispatches[command.pipeline];
    }
    g_vulkan.execute(commands.data(), commands.size());
    ++g_submits;
    g_commands.clear();
    ++g_serial;
    g_param_records.clear();
    g_arena.clear();
    g_decoded_used = 0;
    g_clut_loads = 0;
}

void read_back(gt4::vulkan::Buffer* source, std::uint64_t bytes, void* output, std::uint64_t offset = 0) {
    g_vulkan.read(source, offset, output, bytes);
}
