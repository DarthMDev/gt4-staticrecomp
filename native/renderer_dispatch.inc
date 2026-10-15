// TASK-027 M0b: the single renderer dispatch front end (spec chapter 12.2).
// Spliced at the head of the GS front end by scripts/prepare_external_vulkan.py (--renderer-hooks,
// and the legacy --parallel-hooks). GT4_RENDERER=donor|parallel|hw selects the path once per process.
// Every hook tests alt_active() first, so the donor path is unchanged unless the environment selects
// another renderer. Unknown values stop the process; nothing falls back silently.
// The hw value is accepted only in a port that carries the HWR front end (GT4_RENDERER_HW_SPLICED).

enum class Renderer { donor, parallel, hw };

#ifdef GT4_RENDERER_HW_SPLICED
// GT4_HW_* options: parsed (and validated) once, before the first frame, when GT4_RENDERER=hw.
gt4::hw::Options process_hw_options() {
    try {
        return gt4::hw::parse_options([](const char *name) -> const char * { return std::getenv(name); });
    } catch (const gt4::hw::OptionError &problem) {
        std::fprintf(stderr, "[hw] %s; refusing to start\n", problem.what());
        std::exit(EXIT_FAILURE);
    }
}
#endif

Renderer renderer_mode() {
    static const Renderer mode = [] {
        const char *value = std::getenv("GT4_RENDERER");
        if (!value || !*value || std::strcmp(value, "donor") == 0)
            return Renderer::donor;
        if (std::strcmp(value, "parallel") == 0)
            return Renderer::parallel;
        if (std::strcmp(value, "hw") == 0) {
#ifdef GT4_RENDERER_HW_SPLICED
            process_hw_options();
            return Renderer::hw;
#else
            std::fprintf(stderr, "[gs] GT4_RENDERER=hw: this port carries no HWR front end; refusing to start\n");
            std::exit(EXIT_FAILURE);
#endif
        }
        std::fprintf(stderr, "[gs] GT4_RENDERER=\"%s\" is not donor, parallel or hw; refusing to start\n", value);
        std::exit(EXIT_FAILURE);
    }();
    return mode;
}

bool parallel_active() { return renderer_mode() == Renderer::parallel; }
bool hw_active() { return renderer_mode() == Renderer::hw; }
// True for every non-donor renderer: the hooks in gs.cpp, gif.cpp and hw.cpp test this first.
bool alt_active() { return renderer_mode() != Renderer::donor; }
// Defined in renderer_routers.inc; declared here because the paraLLEl-GS front end calls it first.
void alt_mirror_privileged(std::uint32_t address, std::uint64_t value);
