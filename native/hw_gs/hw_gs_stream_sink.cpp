// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0070.
// Chapter 12.2/12.3 hookup on main's TASK-035 seam: GT4_RENDERER=hw feeds the GS stream hooks to the HWR backend
// in the game process (native/gs_stream/gs_stream_sink.hpp). Replaces the M0b gs.cpp splice of task/027-hw-renderer
// (native/hw_gs_frontend.inc and the paraLLEl-GS dispatch, dropped with paraLLEl-GS by decision 0070).
//
// The backend receives every GIF chunk, privileged write and VSYNC, draws through the GPU write-back renderer (M4/M5)
// and presents the composed display with the VBlank's field (M6: 13.3 field offset, 13.4 deinterlace). Local-to-host
// reads are answered from the shadow local memory after the open batch is flushed (10.2).

#include "gs_stream/gs_stream_sink.hpp"

#include "hw_gs_backend.hpp"
#include "hw_gs_options.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gt4::hw {
namespace {

Backend *g_backend = nullptr;
bool g_odd_field = false;
std::uint64_t g_vsyncs = 0;
std::uint64_t g_unmirrored_privileged = 0;

// M6 frame dumps (chapter 12.9 asks for them; the donor renderer cannot provide them in this mode).
// GT4_HW_DUMP_DIR=<folder> writes P6 PPM files, GT4_HW_DUMP_EVERY=<n> the interval in VBlanks (default
// 100). The image is the composed scanout of the GPU write-back path (Backend::scanout). The RAW VRAM
// buffer decode of chapter 12.9 stays with the CPU reference tool, which owns that oracle (IN-325).
std::string g_dump_dir;
std::uint32_t g_dump_every = 0;
std::uint64_t g_dumps = 0;
std::uint64_t g_dumps_without_image = 0;

void read_dump_options() {
  const char *dir = std::getenv("GT4_HW_DUMP_DIR");
  if (dir && *dir) {
    g_dump_dir = dir;
    g_dump_every = 100;
    if (const char *every = std::getenv("GT4_HW_DUMP_EVERY")) {
      const long value = std::strtol(every, nullptr, 10);
      g_dump_every = value > 0 ? static_cast<std::uint32_t>(value) : 100u;
    }
    std::fprintf(stderr, "[hw] frame dumps: %s every %u VBlanks (composed scanout image)\n", g_dump_dir.c_str(),
                 g_dump_every);
  }
}

// One P6 PPM of the composed scanout image. Returns false when there was no image this VBlank.
bool write_dump(Backend &hw, std::uint64_t index) {
  ScanoutRequest request;
  if (!hw.scanout(request) || request.width == 0 || request.height == 0) return false;
  char name[64];
  std::snprintf(name, sizeof(name), "frame_%05llu.ppm", static_cast<unsigned long long>(index));
  const std::string path = g_dump_dir + "\\" + name;
  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (!file) return false;
  std::fprintf(file, "P6\n%u %u\n255\n", request.width, request.height);
  for (std::uint32_t pixel : request.pixels) {
    const unsigned char rgb[3] = {static_cast<unsigned char>(pixel & 0xFFu),
                                  static_cast<unsigned char>((pixel >> 8) & 0xFFu),
                                  static_cast<unsigned char>((pixel >> 16) & 0xFFu)};
    std::fwrite(rgb, 1, 3, file);
  }
  std::fclose(file);
  return true;
}

void maybe_dump(Backend &hw) {
  if (g_dump_every == 0) return;
  if (g_vsyncs % g_dump_every != 0) return;
  if (write_dump(hw, g_vsyncs)) {
    ++g_dumps;
  } else {
    ++g_dumps_without_image;
  }
}

bool start_backend(std::string &error) {
  Options options;
  try {
    options = parse_options([](const char *name) -> const char * { return std::getenv(name); });
  } catch (const OptionError &problem) {
    error = problem.what();
    return false;
  }
  read_dump_options();
  // Created once and never destroyed: the Vulkan device must not be torn down during process exit.
  auto *created = new Backend;
  if (!created->init(options, error)) return false;
  g_backend = created;
  std::fprintf(stderr, "[hw] GT4_RENDERER=hw: HWR backend started in process, no GS process and no donor Vulkan renderer\n");
  for (const std::string &line : describe_options(options)) std::fprintf(stderr, "[hw] %s\n", line.c_str());
  return true;
}

Backend &backend() {
  if (!g_backend) {
    std::fprintf(stderr, "[hw] HWR backend used before start; stopping\n");
    std::exit(EXIT_FAILURE);
  }
  return *g_backend;
}

void gif(int path, const std::uint8_t *data, std::uint32_t qwc) {
  backend().gif_transfer(path, data, static_cast<std::size_t>(qwc) * 16);
}

void privileged(std::uint32_t address, std::uint64_t value) {
  if (!backend().write_privileged(address, value)) ++g_unmirrored_privileged;
}

void read_fifo(std::uint8_t *data, std::uint32_t qwords) {
  const std::size_t filled = backend().read_transfer_fifo(data, qwords);
  if (filled < qwords) std::memset(data + filled * 16, 0, (static_cast<std::size_t>(qwords) - filled) * 16);
}

// Once per guest VBlank: submit the recorded work, then present (window) or close the frame (headless).
void vsync(bool odd_field) {
  Backend &hw = backend();
  g_odd_field = odd_field;
  ++g_vsyncs;
  hw.set_display_field(odd_field);
  hw.flush();
  hw.next_frame();  // one guest frame per VBlank, windowed or headless (the session line's `frames`)
  if (hw.has_window()) hw.present(odd_field ? PresentPhase::field_odd : PresentPhase::field_even);
  maybe_dump(hw);
}

void gs_reset() {
  Backend &hw = backend();
  hw.reset_context();
  hw.reset_gif_paths();
  hw.clear_vram();
}

void gif_reset() { backend().reset_gif_paths(); }

bool attach_window(std::uint64_t window, std::uint32_t width, std::uint32_t height) {
  std::string error;
  if (!backend().attach_window(reinterpret_cast<void *>(window), error)) {
    std::fprintf(stderr, "[hw] window presentation refused: %s\n", error.c_str());
    return false;
  }
  backend().resize_window(width, height);
  std::fprintf(stderr, "[hw] window output through the HWR swapchain\n");
  return true;
}

void resize_window(std::uint32_t width, std::uint32_t height) { backend().resize_window(width, height); }

RenderCounters stats_counters() { return g_backend ? g_backend->render_counters() : RenderCounters{}; }

void finish(std::FILE *output) {
  if (!g_backend) return;
  g_backend->wait_idle();
  const Stats stats = g_backend->stats();
  std::fprintf(output,
               "[hw] session: vsyncs=%llu frames=%llu presents=%llu black_presents=%llu gif_bytes=%llu "
               "privileged=%llu unmirrored_privileged=%llu faults=%llu\n",
               static_cast<unsigned long long>(g_vsyncs), static_cast<unsigned long long>(stats.frames),
               static_cast<unsigned long long>(stats.presents), static_cast<unsigned long long>(stats.black_presents),
               static_cast<unsigned long long>(stats.gif_bytes),
               static_cast<unsigned long long>(stats.privileged_writes),
               static_cast<unsigned long long>(g_unmirrored_privileged), static_cast<unsigned long long>(stats.faults));
  std::fprintf(output,
               "[hw] m6: flushes=%llu held_presents=%llu dropped_frames=%llu auxiliary_frames=%llu translate_us=%.1f "
               "execute_us=%.1f compose_us=%.1f upload_us=%.1f present_us=%.1f\n",
               static_cast<unsigned long long>(stats.flushes),
               static_cast<unsigned long long>(stats.held_presents),
               static_cast<unsigned long long>(stats.dropped_frames),
               static_cast<unsigned long long>(stats.auxiliary_frames), stats.translate_us, stats.execute_us,
               stats.compose_us, stats.upload_us, stats.present_us);
  const RenderCounters counters = stats_counters();
  std::fprintf(output,
               "[hw] counters: batches=%llu primitives=%llu primitives_skipped_texture=%llu batches_empty=%llu "
               "textures=%llu texels=%llu gpu_draws=%llu writeback_pixels=%llu flat_deindexed=%llu sprites=%llu\n",
               static_cast<unsigned long long>(counters.batches),
               static_cast<unsigned long long>(counters.primitives),
               static_cast<unsigned long long>(counters.primitives_skipped_texture),
               static_cast<unsigned long long>(counters.batches_empty),
               static_cast<unsigned long long>(counters.textures),
               static_cast<unsigned long long>(counters.texels),
               static_cast<unsigned long long>(counters.gpu_draws),
               static_cast<unsigned long long>(counters.writeback_pixels),
               static_cast<unsigned long long>(counters.flat_deindexed),
               static_cast<unsigned long long>(counters.sprites));
  std::fprintf(output,
               "[hw] output: presentation_frames=%llu blank_frames=%llu deinterlaced_frames=%llu "
               "interlace_offset_frames=%llu merge_alpha_from_texel=%llu writeback_depth_words=%llu "
               "fifo_overruns=%llu\n",
               static_cast<unsigned long long>(counters.presentation_frames),
               static_cast<unsigned long long>(counters.blank_frames),
               static_cast<unsigned long long>(counters.deinterlaced_frames),
               static_cast<unsigned long long>(counters.interlace_offset_frames),
               static_cast<unsigned long long>(counters.merge_alpha_from_texel),
               static_cast<unsigned long long>(counters.writeback_depth_words),
               static_cast<unsigned long long>(counters.fifo_overruns));
  if (g_dump_every != 0) {
    std::fprintf(output, "[hw] dumps: written=%llu without_image=%llu every=%u dir=%s\n",
                 static_cast<unsigned long long>(g_dumps), static_cast<unsigned long long>(g_dumps_without_image),
                 g_dump_every, g_dump_dir.c_str());
  }
  std::fflush(output);
}

}  // namespace
}  // namespace gt4::hw

namespace gt4::gs_stream {

const InProcessSink &hw_in_process_sink() {
  static const InProcessSink sink = [] {
    InProcessSink s;
    s.start = &gt4::hw::start_backend;
    s.gif = &gt4::hw::gif;
    s.privileged = &gt4::hw::privileged;
    s.read_fifo = &gt4::hw::read_fifo;
    s.vsync = &gt4::hw::vsync;
    s.gs_reset = &gt4::hw::gs_reset;
    s.gif_reset = &gt4::hw::gif_reset;
    s.attach_window = &gt4::hw::attach_window;
    s.resize_window = &gt4::hw::resize_window;
    s.finish = &gt4::hw::finish;
    return s;
  }();
  return sink;
}

}  // namespace gt4::gs_stream
