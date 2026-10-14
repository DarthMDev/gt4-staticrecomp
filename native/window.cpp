#include "window.hpp"
#include "gpu.hpp"
#include "gs/gs.hpp"
#include "host_io.hpp"
#include "native_vulkan.hpp"
#include "ps2/system.hpp"
#include "perf_probe.hpp"
#ifdef GT4_SHAPE_CAPTURE
#include "shape_capture_control.hpp"
#include "shape_observer_adapter.hpp"
#include <array>
#endif

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
HWND game_window = nullptr;
bool surface_attached = false;
bool closing = false;
ControllerInput *controller = nullptr;
bool widescreen = true;
#ifdef GT4_SHAPE_CAPTURE
std::unique_ptr<gt4::observation::CaptureControl> capture;
#endif

void update_title() {
  std::wstring title = widescreen
                                  ? L"GT4Recomp — 16:9 — F6: proporção — "
                                    L"Enter: Start, Z: acelerar, A: frear"
                                  : L"GT4Recomp — 4:3 — F6: proporção — Enter: "
                                    L"Start, Z: acelerar, A: frear";
#ifdef GT4_PERF_PROBE
  title += L" — F7: start test, F8: report, F9: diagnostics";
#endif
#ifdef GT4_SHAPE_CAPTURE
  if (capture) {
    using gt4::observation::CapturePhase;
    switch (capture->phase()) {
    case CapturePhase::idle: title += L" — F8: start capture"; break;
    case CapturePhase::capturing: title += L" — F8: stop capture"; break;
    case CapturePhase::ready: title += L" — F8: retry export"; break;
    case CapturePhase::exported: title += L" — capture saved"; break;
    }
  }
#endif
  SetWindowTextW(game_window, title.c_str());
}

void apply_pad(const PadState &pad) {
  ps2::set_pad_buttons(pad.buttons);
  for (int axis = 0; axis < 4; ++axis)
    ps2::set_pad_axis(axis, pad.axes[axis]);
  for (int button = 0; button < 16; ++button)
    ps2::set_pad_pressure(button, pad.pressures[button]);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM first,
                                  LPARAM second) {
#ifdef GT4_PERF_PROBE
  if (message == WM_KEYDOWN && (first == VK_F7 || first == VK_F8 || first == VK_F9)) {
    const auto request = first == VK_F7 ? gt4::perf::Request::start :
        first == VK_F8 ? gt4::perf::Request::report : gt4::perf::Request::toggle;
    gt4::perf::Probe::shared().request(request, (second & (1ll << 30)) != 0);
    return 0;
  }
#endif
#ifdef GT4_SHAPE_CAPTURE
  if (message == WM_KEYDOWN && first == VK_F8) {
    if (capture) capture->request_toggle((second & (1ll << 30)) != 0);
    return 0;
  }
#endif
  if (message == WM_CLOSE) {
    DestroyWindow(window);
    return 0;
  }
  if (message == WM_KEYDOWN && first == VK_F6 && !(second & (1ll << 30))) {
    widescreen = !widescreen;
    if (surface_attached)
      ps2::gs::gpu::set_widescreen(widescreen);
    update_title();
    return 0;
  }
  if (message == WM_DESTROY) {
    closing = true;
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_KILLFOCUS && controller) {
    controller->stop();
    if (ps2::g_options.presses.empty())
      apply_pad({});
  }
  if (message == WM_SIZE && surface_attached) {
    ps2::gs::resize_window(LOWORD(second), HIWORD(second));
    return 0;
  }
  return DefWindowProcW(window, message, first, second);
}

bool pump_messages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      ps2::halt("window closed");
      closing = true;
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return !closing;
}

void on_vblank() {
#ifdef GT4_PERF_PROBE
  gt4::perf::Probe::shared().frame_boundary(
      {ps2::now(), ps2::primitives_drawn(), ps2::vu1_interpreted_calls()});
#endif
  GT4_PERF_SCOPE(gt4::perf::Category::host_vblank);
  if (!pump_messages())
    return;
#ifdef GT4_SHAPE_CAPTURE
  if (capture) {
    const auto previous = capture->phase();
    capture->service();
    if (capture->phase() != previous) update_title();
  }
#endif
  std::uint16_t buttons = 0;
  const bool focused = GetForegroundWindow() == game_window;
  if (focused) {
    const struct {
      int key;
      std::uint16_t mask;
    } bindings[] = {
        {VK_BACK, 1u << 0},   {'C', 1u << 1},     {'V', 1u << 2},
        {VK_RETURN, 1u << 3}, {VK_UP, 1u << 4},   {VK_RIGHT, 1u << 5},
        {VK_DOWN, 1u << 6},   {VK_LEFT, 1u << 7}, {'X', 1u << 13},
        {'Z', 1u << 14},      {'A', 1u << 15},    {'S', 1u << 12},
        {'Q', 1u << 10},      {'W', 1u << 11},    {'1', 1u << 8},
        {'2', 1u << 9}};
    for (const auto &binding : bindings)
      if (GetAsyncKeyState(binding.key) & 0x8000)
        buttons |= binding.mask;
  }
  if (ps2::g_options.presses.empty()) {
    apply_pad(controller->poll(focused, buttons));
    if (focused)
      controller->rumble(ps2::pad_rumble());
  }
  ps2::gs::present();
}
} // namespace

bool open_window(bool fullscreen) {
  if (fullscreen)
    throw std::runtime_error(
        "Fullscreen is not qualified in this Vulkan pilot");
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = window_procedure;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"GT4NativeVulkanPilot";
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassW(&window_class))
    throw std::runtime_error("Cannot register native window class");
  widescreen = gt4::vulkan::parse_aspect(std::getenv("GT4_ASPECT")) ==
               gt4::vulkan::Aspect::widescreen;
  RECT window_bounds{0, 0, 960, widescreen ? 540 : 720};
  if (!AdjustWindowRectEx(&window_bounds, WS_OVERLAPPEDWINDOW, FALSE, 0))
    throw std::runtime_error("Cannot size native game window");
  game_window = CreateWindowExW(
      0, window_class.lpszClassName,
      L"GT4Recomp — Audio + XInput — Enter: Start, Z: acelerar, A: frear",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
      window_bounds.right - window_bounds.left,
      window_bounds.bottom - window_bounds.top, nullptr, nullptr,
      window_class.hInstance, nullptr);
  if (!game_window)
    throw std::runtime_error("Cannot create native game window");
  update_title();
  return true;
}

int run_window(const std::function<int()> &emulate) {
  ControllerInput input;
  controller = &input;
  const auto cleanup = [&]() {
#ifdef GT4_PERF_PROBE
    gt4::perf::Probe::shared().finish();
#endif
#ifdef GT4_SHAPE_CAPTURE
    if (capture) {
      capture->finish();
      capture.reset();
    }
#endif
    ps2::set_audio_sink({});
    ps2::g_options.on_vblank = {};
    input.stop();
    controller = nullptr;
    surface_attached = false;
    if (IsWindow(game_window))
      DestroyWindow(game_window);
    game_window = nullptr;
  };
  try {
#ifdef GT4_SHAPE_CAPTURE
    std::array<wchar_t, 32768> output{};
    const auto length = GetEnvironmentVariableW(L"GT4_OBSERVATION_OUTPUT", output.data(),
                                               static_cast<DWORD>(output.size()));
    if (length >= output.size()) throw std::runtime_error("Capture output path exceeds the host limit");
    if (length) {
      capture = std::make_unique<gt4::observation::CaptureControl>(
          gt4::observation::runtime_observer(), std::filesystem::path(output.data()));
      update_title();
    } else {
      std::fputs("[shape-capture] disabled: no explicit GT4_OBSERVATION_OUTPUT\n", stderr);
    }
#endif
    const auto lock_path = ps2::g_options.memcard_path + ".lock";
    HANDLE lock_handle =
        CreateFileA(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock_handle == INVALID_HANDLE_VALUE)
      throw std::runtime_error("Cannot exclusively lock game memory card: " +
                               std::to_string(GetLastError()));
    const std::unique_ptr<void, decltype(&CloseHandle)> card_lock(lock_handle,
                                                                  &CloseHandle);
    ps2::init_graphics();
    RECT bounds{};
    if (!GetClientRect(game_window, &bounds))
      throw std::runtime_error("Cannot inspect native window dimensions");
    if (!ps2::gs::attach_window(3, nullptr,
                                reinterpret_cast<std::uint64_t>(game_window),
                                nullptr, nullptr, bounds.right, bounds.bottom))
      throw std::runtime_error("Native Vulkan presentation refused window");
    surface_attached = true;
    ps2::gs::gpu::set_widescreen(widescreen);
    std::fprintf(stderr,
                 "[window] real guest GPU output through native Vulkan\n");
    // shortcut: one thread and FIFO presentation, qualify pipelining later.
    ps2::g_options.on_vblank = on_vblank;
    AudioOutput audio(pump_messages);
    ps2::set_audio_sink(
        [&audio](const std::int16_t *samples, std::size_t frames) {
          if (!audio.push(samples, frames))
            ps2::halt("window closed");
        });
    int result = 1;
    try {
      result = emulate();
    } catch (...) {
      ps2::set_audio_sink({});
      throw;
    }
    ps2::set_audio_sink({});
    audio.close(!closing && result == 0);
    cleanup();
    return result;
  } catch (const std::exception &error) {
    cleanup();
    std::fprintf(stderr, "[host] audio/input/window failure: %s\n",
                 error.what());
    return 1;
  }
}
