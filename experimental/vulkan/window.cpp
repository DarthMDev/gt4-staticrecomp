#include "window.hpp"
#include "gs/gs.hpp"
#include "native_vulkan.hpp"
#include "ps2/system.hpp"

#include <cstdio>
#include <stdexcept>

namespace {
HWND game_window = nullptr;
bool surface_attached = false;

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM first,
                                  LPARAM second) {
  if (message == WM_CLOSE) {
    DestroyWindow(window);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_SIZE && surface_attached) {
    ps2::gs::resize_window(LOWORD(second), HIWORD(second));
    return 0;
  }
  return DefWindowProcW(window, message, first, second);
}

void on_vblank() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      ps2::halt("window closed");
      return;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  // shortcut: keyboard only, add XInput after the rendering milestone.
  std::uint16_t buttons = 0;
  if (GetForegroundWindow() == game_window) {
    const struct {
      int key;
      std::uint16_t mask;
    } bindings[] = {{VK_RETURN, 1u << 3}, {VK_UP, 1u << 4},
                    {VK_RIGHT, 1u << 5},  {VK_DOWN, 1u << 6},
                    {VK_LEFT, 1u << 7},   {'X', 1u << 13},
                    {'Z', 1u << 14}};
    for (const auto &binding : bindings)
      if (GetAsyncKeyState(binding.key) & 0x8000)
        buttons |= binding.mask;
  }
  if (ps2::g_options.presses.empty())
    ps2::set_pad_buttons(buttons);
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
  game_window = CreateWindowExW(
      0, window_class.lpszClassName,
      L"GT4Recomp — Vulkan experimental — Enter: Start, Z: Cross, X: Circle",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 960, 720,
      nullptr, nullptr, window_class.hInstance, nullptr);
  if (!game_window)
    throw std::runtime_error("Cannot create native game window");
  return true;
}

int run_window(const std::function<int()> &emulate) {
  ps2::init_graphics();
  RECT bounds{};
  if (!GetClientRect(game_window, &bounds))
    throw std::runtime_error("Cannot inspect native window dimensions");
  if (!ps2::gs::attach_window(3, nullptr,
                              reinterpret_cast<std::uint64_t>(game_window),
                              nullptr, nullptr, bounds.right, bounds.bottom))
    throw std::runtime_error("Native Vulkan presentation refused window");
  surface_attached = true;
  std::fprintf(stderr,
               "[window] real guest GPU output through native Vulkan\n");
  // shortcut: one thread and FIFO presentation, qualify pipelining later.
  ps2::g_options.on_vblank = on_vblank;
  const int result = emulate();
  surface_attached = false;
  DestroyWindow(game_window);
  ps2::g_options.on_vblank = {};
  return result;
}
