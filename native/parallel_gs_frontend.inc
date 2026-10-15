// TASK-012 slice 163: paraLLEl-GS front end, selected by GT4_RENDERER=parallel.
// Spliced into the derived gs.cpp by scripts/prepare_external_vulkan.py --parallel-hooks.
// Every hook tests parallel_active() first, so the donor path is unchanged unless the
// environment selects paraLLEl-GS. Unknown GT4_RENDERER values stop the process.

enum class Renderer { donor, parallel };

Renderer renderer_mode() {
    static const Renderer mode = [] {
        const char *value = std::getenv("GT4_RENDERER");
        if (!value)
            return Renderer::donor;
        if (std::strcmp(value, "donor") == 0)
            return Renderer::donor;
        if (std::strcmp(value, "parallel") == 0)
            return Renderer::parallel;
        std::fprintf(stderr, "[gs] GT4_RENDERER=\"%s\" is not donor or parallel; refusing to start\n", value);
        std::exit(EXIT_FAILURE);
    }();
    return mode;
}

bool parallel_active() { return renderer_mode() == Renderer::parallel; }

// Game register writes and SetGsCrt decisions, one line each, when GT4_PGS_REG_LOG names a file.
std::FILE *parallel_register_log() {
    static std::FILE *file = [] {
        const char *path = std::getenv("GT4_PGS_REG_LOG");
        return path && *path ? std::fopen(path, "w") : nullptr;
    }();
    return file;
}

// GT4_PGS_SSAA=1|2|4|8|16 (default 1): paraLLEl-GS super-sampling. Experimental, owner-visible only in
// the window; frame dumps and LOCAL->HOST readbacks stay single-sample. Other values stop the process.
std::uint32_t parallel_super_sampling() {
    const char *value = std::getenv("GT4_PGS_SSAA");
    if (!value)
        return 1;
    for (const char *allowed : {"1", "2", "4", "8", "16"})
        if (std::strcmp(value, allowed) == 0)
            return static_cast<std::uint32_t>(std::atoi(value));
    std::fprintf(stderr, "[pgs] GT4_PGS_SSAA=\"%s\" is not 1, 2, 4, 8 or 16; refusing to start\n", value);
    std::exit(EXIT_FAILURE);
}

// GT4_PGS_DUMP_SOURCE=vram|scanout (default vram). vram decodes the display buffer from the copied
// VRAM with the donor decode (comparable with donor dumps); scanout dumps the paraLLEl-GS scanout
// image, the same image the window shows (before the 16:9 blit). Other values stop the process.
bool parallel_dump_scanout() {
    static const bool scanout = [] {
        const char *value = std::getenv("GT4_PGS_DUMP_SOURCE");
        if (!value || std::strcmp(value, "vram") == 0)
            return false;
        if (std::strcmp(value, "scanout") == 0)
            return true;
        std::fprintf(stderr, "[pgs] GT4_PGS_DUMP_SOURCE=\"%s\" is not vram or scanout; refusing to start\n", value);
        std::exit(EXIT_FAILURE);
    }();
    return scanout;
}

// Scanout phase for the current VBlank: the field the CRTC is reading, as the donor reports it in
// CSR.FIELD (g_odd_field toggles at each VBlank start). Slice 168 headless check: with this phase the
// bob-deinterlaced GT4 menu fields are stable from one VBlank to the next (PARALLEL-GS-PLAN.md 10).
std::uint32_t parallel_scanout_phase() { return g_odd_field ? 1u : 0u; }

struct ParallelFrontEnd final : public gt4::parallel_gs::SignalHandler {
    gt4::parallel_gs::Backend backend;
    bool game_wrote_smode2 = false;
    std::uint64_t vblanks = 0;

    // Inline SIGNAL/FINISH/LABEL packets use the donor's CSR and SIGLBLID handling.
    bool on_signal(std::uint64_t payload) override {
        ps2::gs::signal(0x60, payload);
        return false;
    }
    bool on_finish(std::uint64_t payload) override {
        ps2::gs::signal(0x61, payload);
        return false;
    }
    bool on_label(std::uint64_t payload) override {
        ps2::gs::signal(0x62, payload);
        return false;
    }
};

// Created on first use and intentionally never destroyed: the Vulkan device must not be torn
// down during static destruction at process exit.
ParallelFrontEnd &parallel() {
    static ParallelFrontEnd *front_end = [] {
        auto *created = new ParallelFrontEnd;
        std::string error;
        if (!created->backend.init(error)) {
            std::fprintf(stderr, "[pgs] paraLLEl-GS start failed: %s\n", error.c_str());
            std::exit(EXIT_FAILURE);
        }
        created->backend.set_signal_handler(created);
        const std::uint32_t super_sampling = parallel_super_sampling();
        if (super_sampling != 1 && !created->backend.set_super_sampling(super_sampling)) {
            std::fprintf(stderr, "[pgs] GT4_PGS_SSAA=%u refused by paraLLEl-GS\n", super_sampling);
            std::exit(EXIT_FAILURE);
        }
        std::fprintf(stderr, "[pgs] GT4_RENDERER=parallel: paraLLEl-GS backend started (SSAA %ux), donor Vulkan renderer not created\n",
                     super_sampling);
        return created;
    }();
    return *front_end;
}

void parallel_reset() {
    ParallelFrontEnd &front_end = parallel();
    front_end.backend.reset_context();
    front_end.backend.reset_gif_paths();
    front_end.backend.clear_vram();
}

void parallel_reset_gif() { parallel().backend.reset_gif_paths(); }

void parallel_gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
    parallel().backend.gif_transfer(static_cast<std::uint32_t>(path), data, static_cast<std::size_t>(qwc) * 16);
}

bool parallel_gif_busy(int path) { return parallel().backend.gif_busy(static_cast<std::uint32_t>(path)); }

void parallel_read_image(std::uint8_t *data, std::uint32_t qwords) { parallel().backend.read_transfer_fifo(data, qwords); }

void parallel_wait() { parallel().backend.wait_idle(); }

// With a window, present() advances the Granite frame context (WSI begin_frame) once per VBlank;
// headless runs advance it here.
void parallel_vblank() {
    ParallelFrontEnd &front_end = parallel();
    ++front_end.vblanks;
    front_end.backend.flush();
    if (!front_end.backend.has_window())
        front_end.backend.next_frame();
}

// Window output (slice 168, step 6): Granite WSI swapchain on the host window, FIFO unless
// GT4_PRESENT_MODE says otherwise (same names and parser as the donor presenter).
bool parallel_attach_window(std::uint64_t window, std::uint32_t width, std::uint32_t height) {
    gt4::parallel_gs::WindowTarget target;
    target.window = reinterpret_cast<void *>(window);
    target.width = width;
    target.height = height;
    switch (::gt4::pacing::parse_present_mode(std::getenv("GT4_PRESENT_MODE"))) {
    case ::gt4::pacing::PresentMode::mailbox: target.mode = gt4::parallel_gs::PresentMode::mailbox; break;
    case ::gt4::pacing::PresentMode::immediate: target.mode = gt4::parallel_gs::PresentMode::immediate; break;
    case ::gt4::pacing::PresentMode::fifo: target.mode = gt4::parallel_gs::PresentMode::fifo; break;
    }
    std::string error;
    if (!parallel().backend.attach_window(target, error)) {
        std::fprintf(stderr, "[pgs] window presentation refused: %s\n", error.c_str());
        return false;
    }
    std::fprintf(stderr, "[pgs] window output through paraLLEl-GS scanout (Granite WSI, %ux%u, present mode %s)\n", width,
                 height, ::gt4::pacing::present_mode_name(::gt4::pacing::parse_present_mode(std::getenv("GT4_PRESENT_MODE"))));
    return true;
}

void parallel_resize_window(std::uint32_t width, std::uint32_t height) { parallel().backend.resize_window(width, height); }

bool parallel_present() { return parallel().backend.present(parallel_scanout_phase()); }

// Headless dump of the scanout image (GT4_PGS_DUMP_SOURCE=scanout). Pixels are RGBA8 with the red
// channel in the low byte, as the donor decode returns them; alpha is forced opaque the same way.
bool parallel_scanout_frame(std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height) {
    gt4::parallel_gs::ScanoutRequest request;
    request.phase = parallel_scanout_phase();
    const gt4::parallel_gs::ScanoutImage image = parallel().backend.scanout(request);
    if (!image.valid)
        return false;
    width = image.width;
    height = image.height;
    rgba.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        std::uint32_t pixel;
        std::memcpy(&pixel, image.rgba.data() + i * 4, 4);
        rgba[i] = pixel | 0xFF000000u;
    }
    return true;
}

// Mirrors one privileged write by guest address. The donor keeps its own copy for CSR, IMR and
// the intc logic; paraLLEl-GS needs the display and video-mode registers.
void parallel_mirror_privileged(std::uint32_t address, std::uint64_t value) {
    ParallelFrontEnd &front_end = parallel();
    if (!front_end.backend.write_privileged(address, value)) {
        std::fprintf(stderr, "[pgs] privileged write 0x%08X is outside the GS page; not mirrored\n", address);
        return;
    }
    if (address & 0x1000u)
        return;
    const char *name = nullptr;
    switch (address & 0xFF0u) {
    case 0x000: name = "PMODE"; break;
    case 0x010: name = "SMODE1"; break;
    case 0x020: name = "SMODE2"; break;
    case 0x070: name = "DISPFB1"; break;
    case 0x080: name = "DISPLAY1"; break;
    case 0x090: name = "DISPFB2"; break;
    case 0x0A0: name = "DISPLAY2"; break;
    default: return;
    }
    if ((address & 0xFF0u) == 0x020u)
        front_end.game_wrote_smode2 = true;
    if (std::FILE *log = parallel_register_log())
        std::fprintf(log, "vblank %llu game %s 0x%08X = 0x%016llX\n", static_cast<unsigned long long>(front_end.vblanks),
                     name, address, static_cast<unsigned long long>(value));
}

// Copies the paraLLEl-GS VRAM into the donor memory image so the donor's display decode (the
// same read_pixel path as the donor renderer) produces comparable frames.
// Slice 167: read_pixel needs the swizzle tables built by init(). The donor builds them on its
// render_reset path, which parallel mode skips, and a headless run never calls init_graphics(), so
// without this call every 64x32 page decoded from its first word (flat blocks in the dumps).
void parallel_sync_memory() {
    init(); // swizzle tables for read_pixel; builds once, then returns
    gt4::parallel_gs::Backend &backend = parallel().backend;
    backend.flush();
    const void *vram = backend.map_vram(0, kMemorySize);
    if (!vram) {
        std::fprintf(stderr, "[pgs] VRAM readback unavailable; refusing to show a stale frame\n");
        std::exit(EXIT_FAILURE);
    }
    std::memcpy(g_memory, vram, kMemorySize);
}

// SetGsCrt (HLE) to SMODE1 and SMODE2, as the BIOS call programs both (slice 168: SMODE2 is written on
// every call, no longer only until the game writes it; GT4 rewrites SMODE2 itself every frame anyway).
// Unknown modes write nothing.
void note_crt_mode(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field) {
    if (!parallel_active())
        return;
    const ::gt4::gs::CrtRequest request{interlace, mode, field};
    const ::gt4::gs::CrtMapping mapping = ::gt4::gs::map_set_gs_crt(request);
    if (mapping.status != ::gt4::gs::CrtMappingStatus::ok) {
        std::fprintf(stderr, "[pgs] SetGsCrt interlace %u mode 0x%X field %u not mapped (%s); no scanout registers written\n",
                     interlace, mode, field, ::gt4::gs::describe(mapping.status));
        return;
    }
    ParallelFrontEnd &front_end = parallel();
    front_end.backend.write_privileged(0x12000010u, mapping.registers.smode1);
    front_end.backend.write_privileged(0x12000020u, mapping.registers.smode2);
    std::fprintf(stderr, "[pgs] SetGsCrt interlace %u mode 0x%X field %u -> SMODE1 0x%llX SMODE2 0x%llX\n", interlace, mode,
                 field, static_cast<unsigned long long>(mapping.registers.smode1),
                 static_cast<unsigned long long>(mapping.registers.smode2));
    if (std::FILE *log = parallel_register_log())
        std::fprintf(log, "vblank %llu SetGsCrt interlace %u mode 0x%X field %u -> SMODE1 0x%llX\n",
                     static_cast<unsigned long long>(front_end.vblanks), interlace, mode, field,
                     static_cast<unsigned long long>(mapping.registers.smode1));
}
