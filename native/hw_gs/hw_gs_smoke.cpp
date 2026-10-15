// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0069.
// Headless driver for the HWR backend at milestone M0 (chapters 12.2, 12.3, 12.6, 13.6).
//
//   hw_gs_smoke --print-options           renderer and GT4_HW_* effective values, then the JSON object
//   hw_gs_smoke [--frames N] [--window]   with GT4_RENDERER=hw: N guest frames of black output;
//                                         --window also attaches a hidden window (real swapchain)
//
// Exit codes: 0 success (donor selected: nothing to run), 1 backend failure, 2 usage or option error.

#include "hw_gs_dispatch.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

gt4::hw::EnvironmentLookup process_environment() {
  return [](const char *name) -> const char * { return std::getenv(name); };
}

int usage() {
  std::fprintf(stderr, "usage: hw_gs_smoke --print-options | [--frames N] [--window]\n");
  return 2;
}

// Hidden window for the swapchain: it is created but never shown.
HWND create_hidden_window() {
  WNDCLASSW cls{};
  cls.lpfnWndProc = DefWindowProcW;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"GT4HwrSmokeWindow";
  RegisterClassW(&cls);
  return CreateWindowExW(0, cls.lpszClassName, L"GT4 HWR smoke", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360,
                         nullptr, nullptr, cls.hInstance, nullptr);
}

void print_options(const gt4::hw::Options &options, gt4::hw::Renderer renderer) {
  std::printf("GT4_RENDERER=%s\n", gt4::hw::renderer_name(renderer));
  for (const auto &line : gt4::hw::describe_options(options)) std::printf("%s\n", line.c_str());
  std::printf("json %s\n", gt4::hw::options_json(options).c_str());
}

void print_device(const gt4::hw::DeviceReport &report) {
  std::printf("device=%s vendor=0x%04X amd=%d max_image_2d=%u max_compute_x=%u\n", report.device_name.c_str(),
              report.vendor_id, report.amd ? 1 : 0, report.max_image_dimension_2d,
              report.max_compute_workgroup_count_x);
  std::string found, missing;
  for (const auto &item : report.optional_found) found += " " + item;
  for (const auto &item : report.optional_missing) missing += " " + item;
  std::printf("optional_found:%s\noptional_missing:%s\n", found.c_str(), missing.c_str());
}

} // namespace

int main(int argc, char **argv) {
  bool print_only = false;
  bool use_window = false;
  std::uint64_t frames = 4200;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--print-options") {
      print_only = true;
    } else if (arg == "--window") {
      use_window = true;
    } else if (arg == "--frames" && i + 1 < argc) {
      frames = std::strtoull(argv[++i], nullptr, 10);
    } else {
      return usage();
    }
  }

  std::unique_ptr<gt4::hw::Dispatch> dispatch;
  try {
    dispatch = std::make_unique<gt4::hw::Dispatch>(process_environment());
  } catch (const gt4::hw::OptionError &problem) {
    std::fprintf(stderr, "hw_gs_smoke: %s\n", problem.what());
    return 2;
  }

  if (print_only) {
    print_options(dispatch->options(), dispatch->renderer());
    return 0;
  }
  // donor (unset) runs nothing here; parallel and hw go through start(), which refuses parallel.
  if (dispatch->renderer() == gt4::hw::Renderer::donor) {
    std::printf("GT4_RENDERER=donor: HWR not selected, nothing to run\n");
    return 0;
  }

  print_options(dispatch->options(), dispatch->renderer());
  std::string error;
  if (!dispatch->start(error)) {
    std::fprintf(stderr, "hw_gs_smoke: %s\n", error.c_str());
    return 1;
  }
  gt4::hw::Backend &backend = *dispatch->backend();
  print_device(backend.device_report());

  HWND window = nullptr;
  if (use_window) {
    window = create_hidden_window();
    if (!window || !backend.attach_window(window, error)) {
      std::fprintf(stderr, "hw_gs_smoke: window: %s\n", error.empty() ? "CreateWindow failed" : error.c_str());
      return 1;
    }
  }

  for (std::uint64_t frame = 0; frame < frames; ++frame) {
    backend.next_frame();
    backend.present(gt4::hw::PresentPhase::progressive);
  }
  backend.flush();
  backend.wait_idle();

  const gt4::hw::Stats stats = backend.stats();
  std::printf("frames=%llu presents=%llu black_presents=%llu gif_bytes=%llu swapchain_recreations=%llu faults=%llu\n",
              static_cast<unsigned long long>(stats.frames), static_cast<unsigned long long>(stats.presents),
              static_cast<unsigned long long>(stats.black_presents), static_cast<unsigned long long>(stats.gif_bytes),
              static_cast<unsigned long long>(stats.swapchain_recreations),
              static_cast<unsigned long long>(stats.faults));

  if (use_window) {
    backend.detach_window();
    DestroyWindow(window);
  }
  dispatch->stop();
  return 0;
}
