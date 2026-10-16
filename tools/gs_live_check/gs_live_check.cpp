// SPDX-License-Identifier: MIT
// TASK-036: window and lifecycle check for a GS process, without the game.
//
//   gs_live_check <gs process exe> <file.gscap> <frames> <config.ini> [--kill-at N]
//
// Acts as the producer: creates a HIDDEN top-level window (never shown), a GS stream session, starts the GS process
// in a kill-on-close job with --config, waits for READY, posts CONTROL attach_window with the hidden window, then
// replays the first <frames> VSYNCs of a .gscap version 2 file. Like the game it pumps its window messages once per
// VSYNC and blocks in the frames-in-flight wait. Midway it posts CONTROL resize, then CONTROL snapshot and waits for
// SNAPSHOT_DONE. At the end it checks that the GS window became a child of the hidden window, posts CONTROL
// shutdown and requires exit code 0 within 3 s. With --kill-at N the GS process is terminated after VSYNC N and the
// check requires that the next wait reports the peer's death (PeerExited) instead of hanging.
//
// Exit code 0 when every check passed. Nothing is shown on screen.
#include "gs_stream_file.hpp"
#include "gs_stream_ipc.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gs_stream;

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

void pump() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

HWND create_hidden_parent() {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"GT4GsLiveCheckParent";
  RegisterClassW(&window_class);
  // WS_OVERLAPPEDWINDOW without WS_VISIBLE: the window exists but is never shown.
  HWND window = CreateWindowExW(0, window_class.lpszClassName, L"gs_live_check", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                0, 0, 976, 579, nullptr, nullptr, window_class.hInstance, nullptr);
  require(window != nullptr, "CreateWindowExW failed");
  return window;
}

struct ChildSearch {
  DWORD pid = 0;
  HWND found = nullptr;
};

BOOL CALLBACK find_child(HWND window, LPARAM parameter) {
  auto *search = reinterpret_cast<ChildSearch *>(parameter);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == search->pid) {
    search->found = window;
    return FALSE;
  }
  return TRUE;
}

std::string text_of(const char *field, std::size_t size) {
  std::size_t length = 0;
  while (length < size && field[length]) ++length;
  return std::string(field, length);
}

// Waits for an event record of `type`, printing ERROR records; false on timeout.
bool wait_for(ipc::Session &session, RecordType type, std::vector<std::uint8_t> &payload, std::uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    ipc::RecordView view;
    if (!session.wait_event(view, 50)) {
      pump();
      continue;
    }
    const RecordType got = static_cast<RecordType>(view.header->type);
    if (got == RecordType::error && view.header->payload_bytes >= sizeof(ErrorPayload)) {
      ErrorPayload error;
      std::memcpy(&error, view.payload, sizeof error);
      std::printf("[check] GS ERROR %u%s: %s\n", error.code, error.fatal ? " (fatal)" : "", text_of(error.text, sizeof error.text).c_str());
    }
    payload.assign(view.payload, view.payload + view.header->payload_bytes);
    session.events_in().release(view);
    if (got == type) return true;
  }
  return false;
}

int run(const std::wstring &gs_process, const std::string &capture, std::uint64_t frames, const std::wstring &config,
        std::uint64_t kill_at) {
  HWND parent = create_hidden_parent();
  pump();
  ipc::SessionOptions options;
  options.serial = "SCUS-97328";
  options.frames_in_flight = 2;
  auto session = ipc::Session::create_producer(options);
  ipc::ChildProcess child = ipc::ChildProcess::spawn(
      gs_process, L"--ipc " + ipc::widen(session->name()) + L" --parent " + std::to_wstring(ipc::current_process_id()) +
                      L" --config \"" + config + L"\"");
  session->set_peer(child.process());
  std::vector<std::uint8_t> payload;
  require(wait_for(*session, RecordType::ready, payload, 20000), "no READY within 20 s");
  ReadyPayload ready;
  std::memcpy(&ready, payload.data(), sizeof ready);
  std::printf("[check] READY: %s, %s\n", text_of(ready.renderer, 32).c_str(), text_of(ready.version, 64).c_str());

  const ControlPayload attach{static_cast<std::uint32_t>(ControlKind::attach_window), 0,
                              {reinterpret_cast<std::uint64_t>(parent), 960, 540}};
  session->post(RecordType::control, 0, &attach, sizeof attach);

  FileReader reader;
  std::string error;
  require(reader.open(capture, error), "cannot open the capture: " + error);
  RecordHeader header;
  std::vector<std::uint8_t> data;
  std::uint64_t vsyncs = 0;
  bool resized = false;
  const auto start = std::chrono::steady_clock::now();
  try {
    while (vsyncs < frames && reader.next(header, data)) {
      const auto type = static_cast<RecordType>(header.type);
      if (type == RecordType::control) continue;  // the capture's own shutdown, if any
      if (type == RecordType::vsync) {
        VsyncPayload vsync;
        std::memcpy(&vsync, data.data(), sizeof vsync);
        session->post_vsync(vsync);
        ++vsyncs;
        pump();  // like the game: window messages once per VSYNC
        if (!resized && vsyncs == frames / 2) {
          const ControlPayload resize{static_cast<std::uint32_t>(ControlKind::resize), 0, {1280, 720, 0}};
          session->post(RecordType::control, 0, &resize, sizeof resize);
          SetWindowPos(parent, nullptr, 0, 0, 1296, 759, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
          resized = true;
        }
        if (kill_at && vsyncs == kill_at) {
          std::printf("[check] terminating the GS process after VSYNC %llu\n", static_cast<unsigned long long>(vsyncs));
          TerminateProcess(child.process(), 77);
        }
        continue;
      }
      if (type == RecordType::read_fifo) {
        ReadFifoPayload read;
        std::memcpy(&read, data.data(), sizeof read);
        std::vector<std::uint8_t> reply(std::size_t(read.qwords) * 16);
        session->read_fifo(read.qwords, reply.data());
        continue;
      }
      session->post(type, header.flags, data.data(), data.size());
    }
  } catch (const ipc::PeerExited &exited) {
    if (!kill_at) throw;
    std::uint32_t code = 0;
    child.wait_exit(1000, &code);
    std::printf("[check] PeerExited after the kill: %s (exit code %u)\n", exited.what(), code);
    require(code == 77, "GS process exit code is the kill code");
    DestroyWindow(parent);
    std::puts("[check] kill check passed");
    return 0;
  }
  require(!kill_at, "the GS process was killed but no wait reported it");
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("[check] %llu VSYNCs in %.1f s (%.1f per second)\n", static_cast<unsigned long long>(vsyncs), seconds,
              vsyncs / seconds);

  // 640x480: a native-size snapshot at 3x (about 2400x1344 RGBA8) does not fit the 4 MiB response area.
  const ControlPayload snapshot{static_cast<std::uint32_t>(ControlKind::snapshot), 0, {1, 640, 480}};
  session->post(RecordType::control, 0, &snapshot, sizeof snapshot);
  require(wait_for(*session, RecordType::snapshot_done, payload, 10000), "no SNAPSHOT_DONE within 10 s");
  SnapshotDonePayload done;
  std::memcpy(&done, payload.data(), sizeof done);
  std::printf("[check] snapshot %llux%u %llu bytes\n", static_cast<unsigned long long>(done.width), done.height,
              static_cast<unsigned long long>(done.bytes));
  require(done.width > 0 && done.height > 0 && done.bytes == std::uint64_t(done.width) * done.height * 4, "snapshot size");

  ChildSearch search;
  search.pid = child.pid();
  EnumChildWindows(parent, find_child, reinterpret_cast<LPARAM>(&search));
  require(search.found != nullptr, "the GS window is a child of the parent window");
  RECT client{};
  GetClientRect(search.found, &client);
  std::printf("[check] GS child window 0x%p client %ldx%ld, style 0x%08lX\n", static_cast<void *>(search.found), client.right,
              client.bottom, static_cast<unsigned long>(GetWindowLongW(search.found, GWL_STYLE)));
  require(client.right == 1280 && client.bottom == 720, "the GS child window follows the resize");
  require((GetWindowLongW(search.found, GWL_STYLE) & WS_CHILD) != 0, "WS_CHILD style");

  const ControlPayload shutdown{static_cast<std::uint32_t>(ControlKind::shutdown), 0, {0, 0, 0}};
  session->post(RecordType::control, 0, &shutdown, sizeof shutdown);
  std::uint32_t code = 99;
  const auto shutdown_start = std::chrono::steady_clock::now();
  while (!child.wait_exit(10, &code)) {
    pump();
    require(std::chrono::steady_clock::now() - shutdown_start < std::chrono::seconds(3), "GS process exits within 3 s");
  }
  std::printf("[check] GS process exit code %u\n", code);
  require(code == 0, "GS process exit code 0 after shutdown");
  DestroyWindow(parent);
  std::puts("[check] all checks passed");
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 5 && !(argc == 7 && std::wstring(argv[5]) == L"--kill-at")) {
      std::fputs("usage: gs_live_check <gs process exe> <file.gscap> <frames> <config.ini> [--kill-at N]\n", stderr);
      return 2;
    }
    std::string capture;
    for (const wchar_t *c = argv[2]; *c; ++c) capture.push_back(static_cast<char>(*c));
    return run(argv[1], capture, std::wcstoull(argv[3], nullptr, 10), argv[4], argc == 7 ? std::wcstoull(argv[6], nullptr, 10) : 0);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[check] FAIL: %s\n", error.what());
    return 1;
  }
}
