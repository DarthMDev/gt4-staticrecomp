// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0070.
// Chapter 12.2/12.3 hookup on main's TASK-035 seam: GT4_RENDERER=hw feeds the GS stream hooks to the HWR backend
// in the game process (native/gs_stream/gs_stream_sink.hpp). Replaces the M0b gs.cpp splice of task/027-hw-renderer
// (native/hw_gs_frontend.inc and the paraLLEl-GS dispatch, dropped with paraLLEl-GS by decision 0070).
//
// Scope today (M0 backend): the backend starts, receives every GIF chunk, privileged write and VSYNC, and presents
// black. Local-to-host reads return what read_transfer_fifo returns (zeros until the GPU write-back path lands).

#include "gs_stream/gs_stream_sink.hpp"

#include "hw_gs_backend.hpp"
#include "hw_gs_options.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace gt4::hw {
namespace {

Backend *g_backend = nullptr;
bool g_odd_field = false;
std::uint64_t g_vsyncs = 0;
std::uint64_t g_unmirrored_privileged = 0;

bool start_backend(std::string &error) {
  Options options;
  try {
    options = parse_options([](const char *name) -> const char * { return std::getenv(name); });
  } catch (const OptionError &problem) {
    error = problem.what();
    return false;
  }
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
  hw.flush();
  if (hw.has_window())
    hw.present(odd_field ? PresentPhase::field_odd : PresentPhase::field_even);
  else
    hw.next_frame();
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
