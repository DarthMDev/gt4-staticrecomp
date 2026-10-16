// SPDX-License-Identifier: MIT
// TASK-036/037/038: window, readback and lifecycle checks for a GS process, without the game.
//
//   gs_live_check <gs process exe> <file.gscap> <frames> <config.ini> [options]
//
// Acts as the producer: creates a HIDDEN top-level window (never shown), a GS stream session, starts the GS process
// in a kill-on-close job with --config, waits for READY, posts CONTROL attach_window with the hidden window, then
// replays the first <frames> VSYNCs of a .gscap version 2 file. Like the game it pumps its window messages once per
// VSYNC and blocks in the frames-in-flight wait. Midway it posts CONTROL resize, then CONTROL snapshot and waits for
// SNAPSHOT_DONE. At the end it checks that the GS window became a child of the hidden window, posts CONTROL
// shutdown and requires exit code 0 within 3 s. Nothing is shown on screen. Exit code 0 when every check passed.
//
// Options:
//   --kill-at N      terminate the GS process after VSYNC N; the next wait must report the peer's death (PeerExited).
//   --pace           release each VSYNC on a 60 Hz grid like the game's pacer (default: as fast as possible).
//   --readbacks      TASK-037: in every frame, halfway through its GIF records, inject a local-to-host transfer like
//                    the night race (BITBLTBUF/TRXPOS/TRXREG/TRXDIR=1 on PATH3, then READ_FIFO of 128 qwords = 2 KiB)
//                    and report the distribution of the producer's readback waits.
//   --readback-check MODE   TASK-037: after the replay, a 32x16 sprite of a known colour is drawn and 2 KiB of known
//                    bytes are uploaded (TRXDIR=0), then both are read back (TRXDIR=1). MODE accurate requires the exact
//                    bytes (what any correct GS returns, the built-in renderer included); MODE unsynchronized only
//                    reports what came back (PCSX2 answers from its local memory copy without waiting for the GS thread
//                    or the GPU, so drawn pixels may be stale).
//   --stall-check    TASK-038: the config sets [GT4GS] DebugStallWindowMs/DebugStallAfterMs, so the GS window thread
//                    stops pumping for a while. Meanwhile this producer keeps pumping its own window, resizes it and
//                    posts CONTROL resize every 30 VSYNCs, and a watcher thread sends WM_NULL to the parent window
//                    every 20 ms. Every answer, pump and SetWindowPos must take less than 250 ms.
//   --soak N         TASK-038: N resize cycles, one per VSYNC: CONTROL resize cycling 1920x1080, 960x540 and 0x0 (a
//                    minimized window), with SetWindowPos of the parent. No hang, no ERROR record.
//   --destroy-at N   TASK-038: destroy the parent window after VSYNC N (with --stall-check: while the GS window thread is
//                    stalled), measure how long DestroyWindow takes, then shut the GS process down. This is the game's
//                    close path (WM_CLOSE destroys the window before the GS process is shut down).
//   --aspect-check   TASK-038 / TASK-009: snapshots for window sizes 1280x720 and 960x720. The drawn rectangle must be
//                    the whole 1280x720 window and 960x540 (16:9) in the 960x720 window, the same rectangles as the
//                    built-in renderer's presentation (native_vulkan.cpp presentation_rect): no stretch, no crop.
#include "gs_stream_file.hpp"
#include "gs_stream_ipc.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gs_stream;
using Clock = std::chrono::steady_clock;

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

double ms_since(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

double g_max_pump_ms = 0;  // longest PeekMessage/Dispatch round of the producer's window thread

void pump() {
  const auto start = Clock::now();
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  g_max_pump_ms = std::max(g_max_pump_ms, ms_since(start));
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

std::uint64_t g_errors = 0;  // ERROR records seen

void print_error(const ipc::RecordView &view) {
  if (view.header->type != static_cast<std::uint16_t>(RecordType::error) || view.header->payload_bytes < sizeof(ErrorPayload))
    return;
  ErrorPayload error;
  std::memcpy(&error, view.payload, sizeof error);
  std::printf("[check] GS ERROR %u%s: %s\n", error.code, error.fatal ? " (fatal)" : "", text_of(error.text, sizeof error.text).c_str());
  ++g_errors;
}

// Waits for an event record of `type`, printing ERROR records; false on timeout.
bool wait_for(ipc::Session &session, RecordType type, std::vector<std::uint8_t> &payload, std::uint32_t timeout_ms) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline) {
    ipc::RecordView view;
    if (!session.wait_event(view, 50)) {
      pump();
      continue;
    }
    print_error(view);
    const RecordType got = static_cast<RecordType>(view.header->type);
    payload.assign(view.payload, view.payload + view.header->payload_bytes);
    session.events_in().release(view);
    if (got == type) return true;
  }
  return false;
}

// Releases pending event records (ERROR records are printed and counted).
void drain_events(ipc::Session &session) {
  ipc::RecordView view;
  while (session.events_in().peek(view)) {
    print_error(view);
    session.events_in().release(view);
  }
}

// --- GIF packets (PATH3, PACKED A+D and IMAGE) ---

struct GifBuilder {
  std::vector<std::uint8_t> bytes;

  void qword(std::uint64_t low, std::uint64_t high) {
    const std::size_t at = bytes.size();
    bytes.resize(at + 16);
    std::memcpy(bytes.data() + at, &low, 8);
    std::memcpy(bytes.data() + at + 8, &high, 8);
  }
  // PACKED tag: NLOOP = count, EOP, NREG = 1, REGS = A+D (0xE).
  void ad_tag(std::uint32_t count) { qword(std::uint64_t(count) | (1ull << 15) | (1ull << 60), 0xE); }
  void ad(std::uint32_t address, std::uint64_t value) { qword(value, address); }
  // IMAGE tag (FLG = 2) with NLOOP = qwords, EOP.
  void image_tag(std::uint32_t qwords) { qword(std::uint64_t(qwords) | (1ull << 15) | (2ull << 58), 0); }
};

// GS register addresses (A+D).
constexpr std::uint32_t kPrim = 0x00, kRgbaq = 0x01, kXyz2 = 0x05, kXyoffset1 = 0x18, kPrmodecont = 0x1A,
                        kScissor1 = 0x40, kAlpha1 = 0x42, kTest1 = 0x47, kFrame1 = 0x4C, kZbuf1 = 0x4E,
                        kBitbltbuf = 0x50, kTrxpos = 0x51, kTrxreg = 0x52, kTrxdir = 0x53;

std::uint64_t bitbltbuf_source(std::uint32_t sbp, std::uint32_t sbw, std::uint32_t spsm) {
  return std::uint64_t(sbp) | (std::uint64_t(sbw) << 16) | (std::uint64_t(spsm) << 24);
}
std::uint64_t bitbltbuf_destination(std::uint32_t dbp, std::uint32_t dbw, std::uint32_t dpsm) {
  return (std::uint64_t(dbp) << 32) | (std::uint64_t(dbw) << 48) | (std::uint64_t(dpsm) << 56);
}
std::uint64_t trxreg(std::uint32_t w, std::uint32_t h) { return std::uint64_t(w) | (std::uint64_t(h) << 32); }

void post_gif(ipc::Session &session, const std::vector<std::uint8_t> &bytes) {
  const GifPayload header{3, {}, 0, static_cast<std::uint32_t>(bytes.size()), 0};
  session.post(RecordType::gif, 0, &header, sizeof header, bytes.data(), bytes.size());
}

constexpr std::uint32_t kReadbackQwords = 32 * 16 * 4 / 16;  // 128 qwords = 2 KiB (the night race reads 2 KiB per frame)

// Local-to-host transfer setup of a 32x16 PSMCT32 rectangle at block `sbp` (buffer width 64 pixels).
std::vector<std::uint8_t> readback_setup(std::uint32_t sbp) {
  GifBuilder gif;
  gif.ad_tag(4);
  gif.ad(kBitbltbuf, bitbltbuf_source(sbp, 1, 0));
  gif.ad(kTrxpos, 0);
  gif.ad(kTrxreg, trxreg(32, 16));
  gif.ad(kTrxdir, 1);
  return gif.bytes;
}

// Follows GIF tags per path so that a synthetic packet is only injected between whole packets: the PCSX2 GS parses
// the GIF packets of all paths with one tag state, so a packet must not start inside another one.
struct GifPathTracker {
  std::uint64_t loops_left = 0, registers = 0, register_index = 0, format = 0, words_left = 0;

  void scan(const std::uint8_t *data, std::size_t bytes) {
    for (std::size_t at = 0; at + 16 <= bytes; at += 16) {
      std::uint64_t low;
      std::memcpy(&low, data + at, 8);
      if (loops_left == 0) {
        loops_left = low & 0x7FFF;
        format = (low >> 58) & 3;
        registers = (low >> 60) & 0xF;
        if (registers == 0) registers = 16;
        register_index = 0;
        words_left = loops_left * registers;
        continue;
      }
      if (format == 0) {
        if (++register_index == registers) {
          register_index = 0;
          --loops_left;
        }
      } else if (format == 1) {
        words_left = words_left > 2 ? words_left - 2 : 0;
        if (words_left == 0) loops_left = 0;
      } else {
        --loops_left;
      }
    }
  }
  bool idle() const { return loops_left == 0; }
};

struct WaitStats {
  std::vector<double> ms;
  void print(const char *label) const {
    if (ms.empty()) return;
    std::vector<double> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    double total = 0;
    std::size_t over1 = 0, over4 = 0, over8 = 0;
    for (double value : sorted) {
      total += value;
      over1 += value >= 1.0;
      over4 += value >= 4.0;
      over8 += value >= 8.0;
    }
    const auto at = [&](double q) { return sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(q * sorted.size()))]; };
    std::printf("[check] %s: %zu, mean %.3f ms, p50 %.3f, p90 %.3f, p99 %.3f, max %.3f ms; >=1 ms %zu, >=4 ms %zu, >=8 ms %zu\n",
                label, sorted.size(), total / sorted.size(), at(0.5), at(0.9), at(0.99), sorted.back(), over1, over4, over8);
  }
};

struct Options {
  std::uint64_t kill_at = 0;
  bool pace = false;
  bool readbacks = false;
  std::string readback_check;  // "", "accurate" or "unsynchronized"
  bool stall_check = false;
  std::uint64_t soak = 0;
  bool aspect_check = false;
  std::uint64_t destroy_at = 0;
};

// CONTROL snapshot for a window size; returns the RGBA8 pixels of the drawn rectangle.
std::vector<std::uint8_t> snapshot(ipc::Session &session, std::uint64_t id, std::uint32_t width, std::uint32_t height,
                                   std::uint32_t &out_width, std::uint32_t &out_height) {
  const ControlPayload request{static_cast<std::uint32_t>(ControlKind::snapshot), 0, {id, width, height}};
  session.post(RecordType::control, 0, &request, sizeof request);
  std::vector<std::uint8_t> payload;
  require(wait_for(session, RecordType::snapshot_done, payload, 10000), "no SNAPSHOT_DONE within 10 s");
  SnapshotDonePayload done;
  std::memcpy(&done, payload.data(), sizeof done);
  require(done.id == id, "snapshot id");
  require(done.width > 0 && done.height > 0 && done.bytes == std::uint64_t(done.width) * done.height * 4, "snapshot size");
  out_width = done.width;
  out_height = done.height;
  const std::uint8_t *pixels = session.response_area() + done.response_offset;
  return std::vector<std::uint8_t>(pixels, pixels + done.bytes);
}

// Mean R+G+B of one row or column of an RGBA8 image.
double row_energy(const std::vector<std::uint8_t> &rgba, std::uint32_t width, std::uint32_t row) {
  double sum = 0;
  for (std::uint32_t x = 0; x < width; ++x) {
    const std::uint8_t *p = &rgba[(std::size_t(row) * width + x) * 4];
    sum += p[0] + p[1] + p[2];
  }
  return sum / width;
}
double column_energy(const std::vector<std::uint8_t> &rgba, std::uint32_t width, std::uint32_t height, std::uint32_t column) {
  double sum = 0;
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t *p = &rgba[(std::size_t(y) * width + column) * 4];
    sum += p[0] + p[1] + p[2];
  }
  return sum / height;
}

void aspect_check(ipc::Session &session) {
  // presentation_rect(1280, 720, 16:9) = {0, 0, 1280, 720}; presentation_rect(960, 720, 16:9) = {0, 90, 960, 540}.
  std::uint32_t w = 0, h = 0;
  const auto wide = snapshot(session, 10, 1280, 720, w, h);
  std::printf("[check] aspect: drawn rectangle for a 1280x720 window %ux%u; edge energy top %.1f bottom %.1f left %.1f right %.1f\n",
              w, h, row_energy(wide, w, 0), row_energy(wide, w, h - 1), column_energy(wide, w, h, 0),
              column_energy(wide, w, h, w - 1));
  require(w == 1280 && h == 720, "16:9 window: the picture covers the whole window");
  snapshot(session, 11, 960, 720, w, h);
  std::printf("[check] aspect: drawn rectangle for a 960x720 window %ux%u (presentation_rect 960x540 at y 90)\n", w, h);
  require(w == 960 && h == 540, "4:3 window: a 16:9 rectangle of the full width, like presentation_rect");
  std::puts("[check] aspect check passed");
}

// Draws a 32x16 sprite of a known colour into frame page 0x60 (block 0xC00), uploads 2 KiB of known bytes to block
// 0xD00 and reads both back. True when both read back exactly.
bool readback_check(ipc::Session &session, const std::string &mode) {
  constexpr std::uint32_t kFramePage = 0x60, kSpriteBlock = kFramePage * 32, kUploadBlock = 0xD00;
  const std::uint8_t colour[4] = {0x11, 0x22, 0x33, 0x40};
  GifBuilder draw;
  draw.ad_tag(11);
  draw.ad(kFrame1, std::uint64_t(kFramePage) | (1ull << 16));  // FBP, FBW 1 (64 pixels), PSMCT32, no mask
  draw.ad(kScissor1, (std::uint64_t(63) << 16) | (std::uint64_t(63) << 48));
  draw.ad(kXyoffset1, 0);
  draw.ad(kZbuf1, 1ull << 32);  // ZMSK: no depth writes
  draw.ad(kTest1, (1ull << 16) | (1ull << 17));  // ZTE on, ZTST always
  draw.ad(kAlpha1, 0);
  draw.ad(kPrmodecont, 1);
  draw.ad(kPrim, 6);  // sprite, flat, no texture, no blending
  draw.ad(kRgbaq, std::uint64_t(colour[0]) | (std::uint64_t(colour[1]) << 8) | (std::uint64_t(colour[2]) << 16) |
                      (std::uint64_t(colour[3]) << 24) | (0x3F800000ull << 32));
  draw.ad(kXyz2, 0);
  draw.ad(kXyz2, std::uint64_t(32 << 4) | (std::uint64_t(16 << 4) << 16));
  post_gif(session, draw.bytes);

  std::vector<std::uint8_t> pattern(std::size_t(kReadbackQwords) * 16);
  for (std::size_t i = 0; i < pattern.size(); ++i) pattern[i] = static_cast<std::uint8_t>(i * 7 + 3);
  GifBuilder upload;
  upload.ad_tag(4);
  upload.ad(kBitbltbuf, bitbltbuf_destination(kUploadBlock, 1, 0));
  upload.ad(kTrxpos, 0);
  upload.ad(kTrxreg, trxreg(32, 16));
  upload.ad(kTrxdir, 0);
  upload.image_tag(kReadbackQwords);
  upload.bytes.insert(upload.bytes.end(), pattern.begin(), pattern.end());
  post_gif(session, upload.bytes);

  std::vector<std::uint8_t> sprite(pattern.size()), uploaded(pattern.size());
  post_gif(session, readback_setup(kSpriteBlock));
  session.read_fifo(kReadbackQwords, sprite.data());
  post_gif(session, readback_setup(kUploadBlock));
  session.read_fifo(kReadbackQwords, uploaded.data());

  std::size_t sprite_ok = 0, upload_ok = 0;
  for (std::size_t pixel = 0; pixel < sprite.size() / 4; ++pixel) sprite_ok += std::memcmp(&sprite[pixel * 4], colour, 4) == 0;
  for (std::size_t i = 0; i < uploaded.size(); ++i) upload_ok += uploaded[i] == pattern[i];
  std::printf("[check] readback-check (%s): sprite %zu/%zu pixels are 11223340 (first pixel %02X%02X%02X%02X); upload "
              "%zu/%zu bytes equal\n",
              mode.c_str(), sprite_ok, sprite.size() / 4, sprite[0], sprite[1], sprite[2], sprite[3], upload_ok, uploaded.size());
  return sprite_ok == sprite.size() / 4 && upload_ok == uploaded.size();
}

int run(const std::wstring &gs_process, const std::string &capture, std::uint64_t frames, const std::wstring &config,
        const Options &options) {
  HWND parent = create_hidden_parent();
  pump();
  ipc::SessionOptions session_options;
  session_options.serial = "SCUS-97328";
  session_options.frames_in_flight = 2;
  auto session = ipc::Session::create_producer(session_options);
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

  // Stall check: a watcher thread measures how long the parent window takes to answer a sent message.
  std::atomic<bool> watching{options.stall_check};
  std::atomic<std::uint64_t> answers{0}, timeouts{0}, max_answer_us{0};
  std::thread watcher;
  if (options.stall_check) {
    watcher = std::thread([&] {
      while (watching.load()) {
        const auto start = Clock::now();
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(parent, WM_NULL, 0, 0, SMTO_NORMAL, 2000, &result))
          ++answers;
        else
          ++timeouts;
        const auto us = static_cast<std::uint64_t>(ms_since(start) * 1000.0);
        std::uint64_t seen = max_answer_us.load();
        while (us > seen && !max_answer_us.compare_exchange_weak(seen, us)) {
        }
        Sleep(20);
      }
    });
  }

  FileReader reader;
  std::string error;
  require(reader.open(capture, error), "cannot open the capture: " + error);
  RecordHeader header;
  std::vector<std::uint8_t> data;
  std::uint64_t vsyncs = 0, soak_cycles = 0;
  bool resized = false;
  double max_window_op_ms = 0;
  const auto start = Clock::now();
  auto deadline = Clock::now();
  constexpr auto kPeriod = std::chrono::nanoseconds(1000000000 / 60);
  std::vector<std::pair<RecordHeader, std::vector<std::uint8_t>>> frame_records;
  WaitStats readback_waits;
  GifPathTracker paths[3];
  std::vector<std::uint8_t> reply(std::size_t(kReadbackQwords) * 16);
  const auto resize_parent = [&](std::uint32_t width, std::uint32_t height) {
    const auto op = Clock::now();
    SetWindowPos(parent, nullptr, 0, 0, static_cast<int>(width) + 16, static_cast<int>(height) + 39,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    max_window_op_ms = std::max(max_window_op_ms, ms_since(op));
  };
  const auto post_resize = [&](std::uint32_t width, std::uint32_t height) {
    const ControlPayload resize{static_cast<std::uint32_t>(ControlKind::resize), 0, {width, height, 0}};
    session->post(RecordType::control, 0, &resize, sizeof resize);
  };
  try {
    // A frame's records are collected up to its VSYNC, then posted (with the synthetic readback halfway through).
    while (vsyncs < frames && reader.next(header, data)) {
      const auto type = static_cast<RecordType>(header.type);
      if (type == RecordType::control) continue;  // the capture's own shutdown, if any
      if (type != RecordType::vsync) {
        frame_records.emplace_back(header, data);
        continue;
      }

      std::size_t gif_total = 0, gif_seen = 0;
      for (const auto &record : frame_records) gif_total += record.first.type == static_cast<std::uint16_t>(RecordType::gif);
      bool injected = false;
      for (const auto &[record_header, record_data] : frame_records) {
        const auto record_type = static_cast<RecordType>(record_header.type);
        if (record_type == RecordType::read_fifo) {
          ReadFifoPayload read;
          std::memcpy(&read, record_data.data(), sizeof read);
          std::vector<std::uint8_t> capture_reply(std::size_t(read.qwords) * 16);
          session->read_fifo(read.qwords, capture_reply.data());
          continue;
        }
        session->post(record_type, record_header.flags, record_data.data(), record_data.size());
        if (record_type == RecordType::gif) {
          GifPayload gif;
          std::memcpy(&gif, record_data.data(), sizeof gif);
          if (gif.path >= 1 && gif.path <= 3) paths[gif.path - 1].scan(record_data.data() + sizeof gif, gif.bytes);
          ++gif_seen;
        }
        if (options.readbacks && !injected && record_type == RecordType::gif && gif_seen * 2 >= gif_total &&
            paths[0].idle() && paths[1].idle() && paths[2].idle()) {
          post_gif(*session, readback_setup(0));
          const auto wait_start = Clock::now();
          session->read_fifo(kReadbackQwords, reply.data());
          readback_waits.ms.push_back(ms_since(wait_start));
          injected = true;
        }
      }
      frame_records.clear();
      if (options.pace) {
        deadline += kPeriod;
        std::this_thread::sleep_until(deadline);
        if (Clock::now() > deadline + kPeriod) deadline = Clock::now();
      }
      VsyncPayload vsync;
      std::memcpy(&vsync, data.data(), sizeof vsync);
      session->post_vsync(vsync);
      ++vsyncs;
      pump();  // like the game: window messages once per VSYNC
      drain_events(*session);
      if (!resized && vsyncs == frames / 2 && !options.soak && !options.stall_check) {
        post_resize(1280, 720);
        resize_parent(1280, 720);
        resized = true;
      }
      if (options.stall_check && vsyncs % 30 == 0) {
        const bool large = (vsyncs / 30) % 2 == 0;
        post_resize(large ? 1280 : 960, large ? 720 : 540);
        resize_parent(large ? 1280 : 960, large ? 720 : 540);
      }
      if (options.soak && soak_cycles < options.soak) {
        static const std::uint32_t sizes[3][2] = {{1920, 1080}, {960, 540}, {0, 0}};
        const auto &size = sizes[soak_cycles % 3];
        post_resize(size[0], size[1]);
        if (size[0]) resize_parent(size[0], size[1]);
        ++soak_cycles;
      }
      if (options.destroy_at && vsyncs == options.destroy_at) {
        watching.store(false);
        if (watcher.joinable()) watcher.join();
        const auto op = Clock::now();
        DestroyWindow(parent);
        const double destroy_ms = ms_since(op);
        std::printf("[check] DestroyWindow of the parent after VSYNC %llu took %.1f ms\n", static_cast<unsigned long long>(vsyncs),
                    destroy_ms);
        const ControlPayload shutdown{static_cast<std::uint32_t>(ControlKind::shutdown), 0, {0, 0, 0}};
        session->post(RecordType::control, 0, &shutdown, sizeof shutdown);
        std::uint32_t code = 99;
        require(child.wait_exit(5000, &code), "GS process exits within 5 s after the parent window was destroyed");
        std::printf("[check] GS process exit code %u after the parent window was destroyed\n", code);
        require(destroy_ms < 250, "DestroyWindow of the parent does not wait for the GS window thread");
        std::puts("[check] destroy check passed");
        return 0;
      }
      if (options.kill_at && vsyncs == options.kill_at) {
        std::printf("[check] terminating the GS process after VSYNC %llu\n", static_cast<unsigned long long>(vsyncs));
        TerminateProcess(child.process(), 77);
      }
    }
  } catch (const ipc::PeerExited &exited) {
    watching.store(false);
    if (watcher.joinable()) watcher.join();
    if (!options.kill_at) throw;
    std::uint32_t code = 0;
    child.wait_exit(1000, &code);
    std::printf("[check] PeerExited after the kill: %s (exit code %u)\n", exited.what(), code);
    require(code == 77, "GS process exit code is the kill code");
    DestroyWindow(parent);
    std::puts("[check] kill check passed");
    return 0;
  }
  require(!options.kill_at, "the GS process was killed but no wait reported it");
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  std::printf("[check] %llu VSYNCs in %.1f s (%.1f per second)%s\n", static_cast<unsigned long long>(vsyncs), seconds,
              vsyncs / seconds, options.pace ? ", paced at 60 Hz" : "");
  readback_waits.print("readback waits (producer side)");
  if (options.soak) std::printf("[check] soak: %llu resize cycles\n", static_cast<unsigned long long>(soak_cycles));

  if (options.stall_check) {
    watching.store(false);
    watcher.join();
    std::printf("[check] stall check: parent answered %llu WM_NULL (timeouts %llu), longest answer %.1f ms; longest pump %.1f "
                "ms; longest SetWindowPos %.1f ms\n",
                static_cast<unsigned long long>(answers.load()), static_cast<unsigned long long>(timeouts.load()),
                max_answer_us.load() / 1000.0, g_max_pump_ms, max_window_op_ms);
    require(timeouts.load() == 0 && max_answer_us.load() < 250000 && g_max_pump_ms < 250 && max_window_op_ms < 250,
            "the parent window stays responsive while the GS window thread is stalled");
  } else {
    std::printf("[check] longest pump %.1f ms; longest SetWindowPos %.1f ms\n", g_max_pump_ms, max_window_op_ms);
  }

  ChildSearch search;
  search.pid = child.pid();
  EnumChildWindows(parent, find_child, reinterpret_cast<LPARAM>(&search));
  require(search.found != nullptr, "the GS window is a child of the parent window");

  if (options.aspect_check) aspect_check(*session);

  if (!options.readback_check.empty()) {
    require(paths[0].idle() && paths[1].idle() && paths[2].idle(), "no GIF packet in progress before the readback check");
    const bool exact = readback_check(*session, options.readback_check);
    if (options.readback_check == "accurate")
      require(exact, "accurate readback returns the drawn and uploaded bytes");
    else
      std::printf("[check] unsynchronized readback %s the expected bytes\n", exact ? "returned" : "did not return");
  }

  // 640x480: a native-size snapshot at 3x (about 2400x1344 RGBA8) does not fit the 4 MiB response area.
  std::uint32_t snap_width = 0, snap_height = 0;
  snapshot(*session, 1, 640, 480, snap_width, snap_height);
  std::printf("[check] snapshot %ux%u\n", snap_width, snap_height);

  RECT client{};
  GetClientRect(search.found, &client);
  std::printf("[check] GS child window 0x%p client %ldx%ld, style 0x%08lX\n", static_cast<void *>(search.found), client.right,
              client.bottom, static_cast<unsigned long>(GetWindowLongW(search.found, GWL_STYLE)));
  if (!options.soak && !options.stall_check)
    require(client.right == 1280 && client.bottom == 720, "the GS child window follows the resize");
  require(client.right > 0 && client.bottom > 0, "the GS child window keeps a size (zero-size requests are ignored)");
  require((GetWindowLongW(search.found, GWL_STYLE) & WS_CHILD) != 0, "WS_CHILD style");
  drain_events(*session);
  require(g_errors == 0, "no ERROR records from the GS process");

  const ControlPayload shutdown{static_cast<std::uint32_t>(ControlKind::shutdown), 0, {0, 0, 0}};
  session->post(RecordType::control, 0, &shutdown, sizeof shutdown);
  std::uint32_t code = 99;
  const auto shutdown_start = Clock::now();
  while (!child.wait_exit(10, &code)) {
    pump();
    require(Clock::now() - shutdown_start < std::chrono::seconds(3), "GS process exits within 3 s");
  }
  std::printf("[check] GS process exit code %u\n", code);
  require(code == 0, "GS process exit code 0 after shutdown");
  DestroyWindow(parent);
  std::puts("[check] all checks passed");
  return 0;
}

std::string narrow(const wchar_t *text) {
  std::string out;
  for (const wchar_t *c = text; *c; ++c) out.push_back(static_cast<char>(*c));
  return out;
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc < 5) {
      std::fputs("usage: gs_live_check <gs process exe> <file.gscap> <frames> <config.ini> [--kill-at N] [--pace] "
                 "[--readbacks] [--readback-check accurate|unsynchronized] [--stall-check] [--soak N] [--destroy-at N] [--aspect-check]\n",
                 stderr);
      return 2;
    }
    Options options;
    for (int i = 5; i < argc; ++i) {
      const std::wstring arg = argv[i];
      if (arg == L"--kill-at" && i + 1 < argc)
        options.kill_at = std::wcstoull(argv[++i], nullptr, 10);
      else if (arg == L"--pace")
        options.pace = true;
      else if (arg == L"--readbacks")
        options.readbacks = true;
      else if (arg == L"--readback-check" && i + 1 < argc)
        options.readback_check = narrow(argv[++i]);
      else if (arg == L"--stall-check")
        options.stall_check = true;
      else if (arg == L"--soak" && i + 1 < argc)
        options.soak = std::wcstoull(argv[++i], nullptr, 10);
      else if (arg == L"--destroy-at" && i + 1 < argc)
        options.destroy_at = std::wcstoull(argv[++i], nullptr, 10);
      else if (arg == L"--aspect-check")
        options.aspect_check = true;
      else
        throw std::runtime_error("unknown option " + narrow(argv[i]));
    }
    if (!options.readback_check.empty() && options.readback_check != "accurate" && options.readback_check != "unsynchronized")
      throw std::runtime_error("--readback-check takes accurate or unsynchronized");
    return run(argv[1], narrow(argv[2]), std::wcstoull(argv[3], nullptr, 10), argv[4], options);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[check] FAIL: %s\n", error.what());
    return 1;
  }
}
