// Shared resources are bound once; per-slot uploads are bound by the adapter on each submission.
void rebuild_group() {
    g_vulkan.bind_buffers({g_vram, g_tables, nullptr, nullptr, g_clut, g_scratch, g_cache, g_decoded});
}

// Submits the pending batch without waiting for it. A non-zero size presents the
// RGBA result in g_scratch in the same submission. Returns true when an image was presented.
bool submit(std::uint32_t present_width = 0, std::uint32_t present_height = 0) {
    if (g_commands.empty()) return false;
    std::vector<gt4::vulkan::Dispatch> commands;
    commands.reserve(g_commands.size());
    for (const auto& command : g_commands) {
        commands.push_back({static_cast<std::uint32_t>(command.pipeline), command.x, command.y, command.param});
        ++g_dispatches[command.pipeline];
    }
    gt4::vulkan::Submission submission;
    submission.arena = reinterpret_cast<const std::uint8_t*>(g_arena.data());
    submission.arena_bytes = g_arena.size() * 4;
    submission.parameters = reinterpret_cast<const std::uint8_t*>(g_param_records.data());
    submission.parameter_bytes = g_param_records.size() * kParamStride;
    submission.commands = commands.data();
    submission.command_count = commands.size();
    if (present_width && present_height) {
        submission.present_source = g_scratch;
        submission.present_width = present_width;
        submission.present_height = present_height;
    }
    bool presented = false;
    // Pending VRAM writes now belong to this submission (page-tracked readbacks, slice 183).
    const std::uint64_t id = g_vulkan.submit(submission, &presented);
    g_vram_writes.submitted(id);
    ++g_submits;
    g_commands.clear();
    // Cache entries stamped with this id are protected until it completes.
    g_serial = g_vulkan.next_submission();
    g_param_records.clear();
    g_arena.clear();
    g_decoded_used = 0;
    g_clut_loads = 0;
    return presented;
}

void read_back(gt4::vulkan::Buffer* source, std::uint64_t bytes, void* output, std::uint64_t offset = 0) {
    g_vulkan.read(source, offset, output, bytes);
}

// Set GT4_GS_READBACK=full to restore the whole-queue wait for every readback (A/B control).
bool full_readback_wait() {
    static const bool full = [] {
        const char* mode = std::getenv("GT4_GS_READBACK");
        return mode && std::strcmp(mode, "full") == 0;
    }();
    return full;
}

