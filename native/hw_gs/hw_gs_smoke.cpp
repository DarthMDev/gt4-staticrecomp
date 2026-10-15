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

#include <chrono>
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
  std::uint64_t resize_toggles = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--print-options") {
      print_only = true;
    } else if (arg == "--window") {
      use_window = true;
    } else if (arg == "--frames" && i + 1 < argc) {
      frames = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--resize-toggles" && i + 1 < argc) {
      resize_toggles = std::strtoull(argv[++i], nullptr, 10);
      use_window = true;
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

  // Fullscreen-toggle stall check (TASK-027 M0b): each toggle changes the window's client size and sets the
  // resize flag, exactly as Alt+Enter and F11 do; the swapchain is rebuilt before the next present. Every
  // toggle is timed from the flag to the end of that present. Limit: 100 ms (the owner's stall criterion).
  if (resize_toggles > 0) {
    double worst_ms = 0.0;
    for (std::uint64_t toggle = 0; toggle < resize_toggles; ++toggle) {
      const std::uint32_t client_width = toggle % 2 == 0 ? 800u : 640u;
      const std::uint32_t client_height = client_width * 9u / 16u;
      RECT outer{}, client{};
      GetWindowRect(window, &outer);
      GetClientRect(window, &client);
      const int frame_width = (outer.right - outer.left) - (client.right - client.left);
      const int frame_height = (outer.bottom - outer.top) - (client.bottom - client.top);
      SetWindowPos(window, nullptr, 0, 0, static_cast<int>(client_width) + frame_width,
                   static_cast<int>(client_height) + frame_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      const auto started = std::chrono::steady_clock::now();
      backend.resize_window(client_width, client_height);
      backend.next_frame();
      backend.present(gt4::hw::PresentPhase::progressive);
      backend.wait_idle();
      const double elapsed_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      if (elapsed_ms > worst_ms) worst_ms = elapsed_ms;
    }
    const gt4::hw::Stats toggled = backend.stats();
    std::printf("resize_toggles=%llu max_toggle_ms=%.2f swapchain_recreations=%llu\n",
                static_cast<unsigned long long>(resize_toggles), worst_ms,
                static_cast<unsigned long long>(toggled.swapchain_recreations));
    if (worst_ms > 100.0) {
      std::fprintf(stderr, "hw_gs_smoke: a fullscreen-style toggle stalled for %.2f ms (limit 100 ms)\n", worst_ms);
      if (use_window) {
        backend.detach_window();
        DestroyWindow(window);
      }
      dispatch->stop();
      return 1;
    }
  }

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
