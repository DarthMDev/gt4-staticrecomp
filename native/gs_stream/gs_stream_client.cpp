// SPDX-License-Identifier: MIT
// Game-side GS stream backend; see gs_stream_client.hpp and docs/GS-STREAM-PROTOCOL.md section 7.
#include "gs_stream_client.hpp"

#include "gs_stream_file.hpp"
#include "gs_stream_ipc.hpp"
#include "perf_probe.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace gt4::gs_stream {
namespace {

namespace proto = ::gs_stream;
namespace ipc = ::gs_stream::ipc;
using proto::RecordType;

// The serial the producer announces in the control block and in stream files. The consumer uses it only to select
// per-game settings (a GS renderer's game database); it never changes the meaning of a record.
constexpr const char *kGameSerial = "SCUS-97328";
constexpr std::uint32_t kHandshakeTimeoutMs = 20000;
// GT4_GS_HANDSHAKE_TIMEOUT_MS (tests): 1000..60000, default kHandshakeTimeoutMs.
constexpr std::uint32_t kHandshakeTimeoutMinMs = 1000, kHandshakeTimeoutMaxMs = 60000;
// After the GS process died the guest keeps running this many VSYNCs (records are dropped, readbacks answered with
// zeros) before it halts: the runtime writes a changed memory card after 50 quiet VSYNCs, so a save in progress is
// finished and flushed first (TASK-038). GT4_GS_LOST_GRACE_FRAMES (tests): 0..600.
constexpr std::uint32_t kLostGraceFrames = 180, kLostGraceMaxFrames = 600;
// Printed when the player asked for a restart with the built-in renderer; the launcher looks for this line.
constexpr const char *kRestartMarker = "[gs-ipc] restart requested: renderer donor (CoreGS 1.1)";
constexpr std::uint32_t kShutdownTimeoutMs = 3000;
// GIF data per record and qwords per READ_FIFO: fixed by the default region sizes, so the in-process capture and a
// recorder process split the same chunks the same way (byte-identical files).
constexpr std::uint64_t kGifChunkBytes =
    (proto::kDefaultCommandCapacity / 2 - sizeof(proto::RecordHeader) - sizeof(proto::GifPayload)) & ~std::uint64_t(15);
constexpr std::uint32_t kReadFifoQwordsPerRequest = static_cast<std::uint32_t>(proto::kDefaultResponseCapacity / 16);

struct Counters {
  std::uint64_t records = 0, bytes = 0, gif_records = 0, gif_bytes = 0, priv = 0, vsyncs = 0, readbacks = 0,
                readback_bytes = 0, controls = 0, resets = 0;
};

// A visible top-level window of this process (the game window exists before the GS process starts), or null.
HWND visible_game_window() {
  struct Search {
    DWORD pid;
    HWND found;
  } search{GetCurrentProcessId(), nullptr};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto *s = reinterpret_cast<Search *>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == s->pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
          s->found = window;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.found;
}

std::wstring wide_text(const std::string &text) { return std::wstring(text.begin(), text.end()); }

// Asks the player whether to restart with the built-in renderer; prints the launcher marker on yes.
void offer_restart(HWND window, const std::wstring &what) {
  const std::wstring text = what + L"\n\nDetails are in the game log.\n\nRestart the game with the built-in renderer "
                                   L"(CoreGS 1.1)?";
  const int answer = MessageBoxW(window, text.c_str(), L"GT4Recomp: GS renderer", MB_YESNO | MB_ICONERROR | MB_SETFOREGROUND);
  if (answer == IDYES) {
    std::fprintf(stderr, "%s\n", kRestartMarker);
    std::fflush(stderr);
  }
}

[[noreturn]] void refuse(const std::string &message) {
  std::fprintf(stderr, "[gs-ipc] %s; refusing to start\n", message.c_str());
  std::fflush(stderr);
  // The game window is already open in a windowed run: say why it closes (headless runs have no window).
  if (HWND window = visible_game_window())
    offer_restart(window, L"The GS renderer (gt4-gs.exe) could not start:\n" + wide_text(message) + L".");
  std::exit(EXIT_FAILURE);
}

std::uint32_t environment_number(const char *name, std::uint32_t fallback, std::uint32_t low, std::uint32_t high) {
  const char *value = std::getenv(name);
  if (!value || !*value) return fallback;
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (*end || parsed < low || parsed > high)
    refuse(std::string(name) + "=\"" + value + "\" is not a number from " + std::to_string(low) + " to " + std::to_string(high));
  return static_cast<std::uint32_t>(parsed);
}

// Readback wait times of one F8 interval or of the session (EE wait for each READ_FIFO reply).
struct ReadbackWaits {
  static constexpr int kBuckets = 8;  // <0.25, <0.5, <1, <2, <4, <8, <16, >=16 ms
  std::uint64_t count = 0, total_us = 0, max_us = 0;
  std::uint64_t buckets[kBuckets] = {};

  void add(std::uint64_t us) {
    ++count;
    total_us += us;
    max_us = std::max(max_us, us);
    int bucket = 0;
    for (std::uint64_t limit = 250; bucket < kBuckets - 1 && us >= limit; limit *= 2) ++bucket;
    ++buckets[bucket];
  }

  void print(std::FILE *output, const char *label) const {
    std::fprintf(output,
                 "[gs-ipc] %s readback waits: %llu, mean %.3f ms, max %.3f ms, buckets(ms) <0.25=%llu 0.25-0.5=%llu "
                 "0.5-1=%llu 1-2=%llu 2-4=%llu 4-8=%llu 8-16=%llu >=16=%llu\n",
                 label, static_cast<unsigned long long>(count), count ? total_us / 1000.0 / count : 0.0, max_us / 1000.0,
                 static_cast<unsigned long long>(buckets[0]), static_cast<unsigned long long>(buckets[1]),
                 static_cast<unsigned long long>(buckets[2]), static_cast<unsigned long long>(buckets[3]),
                 static_cast<unsigned long long>(buckets[4]), static_cast<unsigned long long>(buckets[5]),
                 static_cast<unsigned long long>(buckets[6]), static_cast<unsigned long long>(buckets[7]));
  }
};

std::uint64_t now_us() {
  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  return static_cast<std::uint64_t>(counter.QuadPart / frequency.QuadPart * 1000000 +
                                    counter.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart);
}

std::string ascii(const char *text, std::size_t size) {
  std::size_t length = 0;
  while (length < size && text[length]) ++length;
  return std::string(text, length);
}

class Client {
 public:
  Client(Renderer renderer) : renderer_(renderer) {}

  void start() {
    open_capture();
    if (renderer_ == Renderer::pcsx2) start_process();
    gt4::perf::Probe::set_gs_ipc_hooks(&Client::report_interval_hook, &Client::reset_interval_hook);
  }

  bool stream() const { return renderer_ == Renderer::pcsx2; }

  void gif(int path, const std::uint8_t *data, std::uint32_t qwc) {
    const std::uint64_t total = static_cast<std::uint64_t>(qwc) * 16;
    std::uint64_t at = 0;
    do {
      const std::uint64_t bytes = std::min(kGifChunkBytes, total - at);
      const bool more = at + bytes < total;
      const proto::GifPayload header{static_cast<std::uint8_t>(path), {}, proto::kGifFlagSignalsApplied,
                                     static_cast<std::uint32_t>(bytes), 0};
      emit(RecordType::gif, more ? proto::kRecordFlagMore : 0, &header, sizeof header, data + at,
           static_cast<std::size_t>(bytes));
      ++counters_.gif_records;
      counters_.gif_bytes += bytes;
      at += bytes;
    } while (at < total);
  }

  void privileged(std::uint32_t address, std::uint64_t value) {
    if (address < proto::kPrivilegedBase || address >= proto::kPrivilegedBase + proto::kPrivilegedBytes) return;
    const proto::PrivPayload payload{address & ~7u, 0, value};
    emit(RecordType::priv, 0, &payload, sizeof payload);
    ++counters_.priv;
  }

  bool read_fifo(std::uint8_t *data, std::uint32_t qwords) {
    for (std::uint32_t done = 0; done < qwords;) {
      const std::uint32_t part = std::min(qwords - done, kReadFifoQwordsPerRequest);
      const proto::ReadFifoPayload request{part, 0, ++readback_id_};
      ++counters_.readbacks;
      counters_.readback_bytes += std::uint64_t(part) * 16;
      write_capture(RecordType::read_fifo, 0, &request, sizeof request);
      ++sequence_;
      count(sizeof request);
      if (stream()) {
        std::uint8_t *out = data + std::size_t(done) * 16;
        if (dead_) {
          std::memset(out, 0, std::size_t(part) * 16);
        } else {
          gt4::perf::Scope wait(gt4::perf::Category::gs_ipc_readback_wait);
          const std::uint64_t start = now_us();
          try {
            const std::uint32_t status = session_->read_fifo(part, out);
            if (status == proto::kReadbackZeroFilled) ++zero_filled_;
          } catch (const ipc::IpcError &error) {
            lost(error.what());
            std::memset(out, 0, std::size_t(part) * 16);
          }
          const std::uint64_t waited = now_us() - start;
          readback_interval_.add(waited);
          readback_session_.add(waited);
        }
      }
      done += part;
    }
    return stream();
  }

  void vsync(bool odd_field) {
    const proto::VsyncPayload payload{frame_, static_cast<std::uint8_t>(odd_field ? 1 : 0), {},
                                      proto::kVsyncFlagFieldValid};
    write_capture(RecordType::vsync, 0, &payload, sizeof payload);
    ++sequence_;
    count(sizeof payload);
    ++counters_.vsyncs;
    if (session_ && !dead_) {
      gt4::perf::Scope post(gt4::perf::Category::gs_ipc_vsync_post);
      try {
        session_->post_vsync(payload);
      } catch (const ipc::IpcError &error) {
        lost(error.what());
      }
    }
    ++frame_;
    if (dead_ && !halted_ && frames_since_lost_++ >= lost_grace_frames_) {
      halted_ = true;
      if (halt_) halt_("GS process exited");
    }
    if (capture_.is_open() && frame_ > capture_last_) {
      capture_.close();
      std::fprintf(stderr, "[gs-ipc] capture closed after frame %llu\n", static_cast<unsigned long long>(capture_last_));
    }
    poll_events();
    const std::uint64_t resize = pending_resize_.exchange(0);
    if ((resize >> 63) && stream()) control(proto::ControlKind::resize, (resize >> 32) & 0x7FFFFFFFu, resize & 0xFFFFFFFFu, 0);
  }

  void reset(proto::ResetKind kind, std::uint32_t mask) {
    const proto::ResetPayload payload{static_cast<std::uint32_t>(kind), mask, 0};
    emit(RecordType::reset, 0, &payload, sizeof payload);
    ++counters_.resets;
  }

  void control(proto::ControlKind kind, std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    const proto::ControlPayload payload{static_cast<std::uint32_t>(kind), 0, {a, b, c}};
    emit(RecordType::control, 0, &payload, sizeof payload);
    ++counters_.controls;
  }

  void attach(std::uint64_t window, std::uint32_t width, std::uint32_t height) {
    // The GS process draws into a child of our window: the parent must not paint over it.
    const HWND parent = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window));
    SetWindowLongPtrW(parent, GWL_STYLE, GetWindowLongPtrW(parent, GWL_STYLE) | WS_CLIPCHILDREN);
    window_ = window;
    control(proto::ControlKind::attach_window, window, width, height);
  }

  void request_resize(std::uint32_t width, std::uint32_t height) {
    // A minimized window reports a 0x0 client area: keep the GS swap chain as it is until the window is restored.
    if (width == 0 || height == 0) return;
    // Bit 63 marks a pending request; the window thread may call this between VSYNCs.
    pending_resize_.store((std::uint64_t(1) << 63) | (std::uint64_t(width & 0x7FFFFFFFu) << 32) | height);
  }

  void set_halt(void (*halt)(const char *)) { halt_ = halt; }

  void finish(std::FILE *output) {
    if (finished_) return;
    finished_ = true;
    capture_.close();  // the capture holds the game's stream; the transport's shutdown request is not part of it
    if (session_ && !dead_) {
      control(proto::ControlKind::shutdown, 0, 0, 0);
      std::uint32_t code = 0;
      if (child_.wait_exit(kShutdownTimeoutMs, &code)) {
        poll_events();
        std::fprintf(output, "[gs-ipc] GS process exited with code %u after shutdown\n", code);
      } else {
        std::fprintf(output, "[gs-ipc] GS process did not exit within %u ms of shutdown; ending it\n", kShutdownTimeoutMs);
        child_.kill();
      }
    }
    const double frames = counters_.vsyncs ? static_cast<double>(counters_.vsyncs) : 1.0;
    std::fprintf(output,
                 "[gs-ipc] session: renderer %s vsyncs %llu records %llu bytes %llu gif %llu (%llu bytes) priv %llu "
                 "readbacks %llu (%llu bytes, %llu zero-filled) resets %llu controls %llu\n",
                 stream() ? "pcsx2" : "donor", static_cast<unsigned long long>(counters_.vsyncs),
                 static_cast<unsigned long long>(counters_.records), static_cast<unsigned long long>(counters_.bytes),
                 static_cast<unsigned long long>(counters_.gif_records), static_cast<unsigned long long>(counters_.gif_bytes),
                 static_cast<unsigned long long>(counters_.priv), static_cast<unsigned long long>(counters_.readbacks),
                 static_cast<unsigned long long>(counters_.readback_bytes), static_cast<unsigned long long>(zero_filled_),
                 static_cast<unsigned long long>(counters_.resets), static_cast<unsigned long long>(counters_.controls));
    std::fprintf(output, "[gs-ipc] session per frame: bytes=%.0f records=%.1f readbacks=%.3f\n", counters_.bytes / frames,
                 counters_.records / frames, counters_.readbacks / frames);
    if (session_) {
      const ipc::WaitTotals &w = session_->wait_totals();
      std::fprintf(output,
                   "[gs-ipc] session waits: space %llu (%.3f ms total), frames in flight %llu (%.3f ms total), "
                   "readback %.3f ms total\n",
                   static_cast<unsigned long long>(w.space_waits), w.space_wait_us / 1000.0,
                   static_cast<unsigned long long>(w.frames_waits), w.frames_wait_us / 1000.0, w.response_wait_us / 1000.0);
    }
    if (readback_session_.count) readback_session_.print(output, "session");
    if (has_stats_) print_consumer_stats(output, last_stats_);
    std::fflush(output);
    if (dead_ && window_) {
      // The player sees a window that stopped updating: say why. The guest ran on for the grace VSYNCs after the GS
      // process died, so a changed memory card was written by the runtime (50 quiet VSYNCs) before the halt.
      offer_restart(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_)),
                    L"The GS renderer process (gt4-gs.exe) stopped" +
                        (exit_known_ ? L" with exit code " + std::to_wstring(exit_code_) : std::wstring()) +
                        L".\nThe game will now close. Your memory card was saved.");
    }
  }

 private:
  void open_capture() {
    const char *path = std::getenv("GT4_GS_CAPTURE");
    if (!path || !*path) return;
    if (const char *range = std::getenv("GT4_GS_CAPTURE_FRAMES")) {
      unsigned long long first = 0, last = 0;
      char extra = 0;
      if (std::sscanf(range, "%llu:%llu%c", &first, &last, &extra) != 2 || last < first)
        refuse(std::string("GT4_GS_CAPTURE_FRAMES=\"") + range + "\" is not first:last");
      capture_first_ = first;
      capture_last_ = last;
    }
    std::string error;
    if (!capture_.open(path, kGameSerial, capture_first_, error)) refuse("GT4_GS_CAPTURE: " + error);
    std::fprintf(stderr, "[gs-ipc] capturing the GS stream to %s (frames %llu to %llu, .gscap version 2)\n", path,
                 static_cast<unsigned long long>(capture_first_), static_cast<unsigned long long>(capture_last_));
  }

  void start_process() {
    const char *process = std::getenv("GT4_GS_PROCESS");
    if (!process || !*process) refuse("GT4_RENDERER=pcsx2 needs GT4_GS_PROCESS=<path of the GS process>");
    handshake_timeout_ms_ = environment_number("GT4_GS_HANDSHAKE_TIMEOUT_MS", kHandshakeTimeoutMs, kHandshakeTimeoutMinMs,
                                               kHandshakeTimeoutMaxMs);
    lost_grace_frames_ = environment_number("GT4_GS_LOST_GRACE_FRAMES", kLostGraceFrames, 0, kLostGraceMaxFrames);
    std::uint32_t frames_in_flight = 2;
    if (const char *value = std::getenv("GT4_GS_FRAMES_IN_FLIGHT")) {
      if (std::strcmp(value, "1") && std::strcmp(value, "2") && std::strcmp(value, "3"))
        refuse(std::string("GT4_GS_FRAMES_IN_FLIGHT=\"") + value + "\" is not 1, 2 or 3");
      frames_in_flight = static_cast<std::uint32_t>(value[0] - '0');
    }
    // TASK-036: the GS process must match its pin (GT4_GS_PROCESS_SHA256, set by the launcher from the manifest).
    if (const char *pin = std::getenv("GT4_GS_PROCESS_SHA256"); pin && *pin) {
      std::string actual, error;
      if (!file_sha256(ipc::widen(process), actual, error)) refuse(std::string("cannot hash the GS process ") + process + ": " + error);
      std::string expected(pin);
      std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (actual != expected)
        refuse(std::string("the GS process ") + process + " has SHA-256 " + actual + ", the pin is " + expected);
      std::fprintf(stderr, "[gs-ipc] GS process SHA-256 matches its pin (%s)\n", actual.c_str());
    }
    std::wstring config_argument;
    if (const char *config = std::getenv("GT4_GS_CONFIG"); config && *config) {
      if (GetFileAttributesW(ipc::widen(config).c_str()) == INVALID_FILE_ATTRIBUTES)
        refuse(std::string("GT4_GS_CONFIG=") + config + " does not exist");
      config_argument = L" --config \"" + ipc::widen(config) + L"\"";
    }
    ipc::SessionOptions options;
    options.serial = kGameSerial;
    options.frames_in_flight = frames_in_flight;
    try {
      session_ = ipc::Session::create_producer(options);
      const std::wstring arguments = L"--ipc " + ipc::widen(session_->name()) + L" --parent " +
                                     std::to_wstring(ipc::current_process_id()) + config_argument;
      child_ = ipc::ChildProcess::spawn(ipc::widen(process), arguments);
      session_->set_peer(child_.process());
    } catch (const ipc::IpcError &error) {
      refuse(std::string("cannot start the GS process ") + process + ": " + error.what());
    }
    ipc::RecordView event;
    bool ready = false;
    try {
      ready = session_->wait_event(event, handshake_timeout_ms_);
    } catch (const ipc::PeerExited &) {
      std::uint32_t code = 0;
      child_.wait_exit(1000, &code);
      drain_errors();
      refuse(std::string("the GS process ") + process + " exited during the handshake (code " + std::to_string(code) + ")");
    }
    if (!ready)
      refuse(std::string("the GS process ") + process + " did not answer within " +
             std::to_string(handshake_timeout_ms_ / 1000.0).substr(0, 4) + " s (no READY record; the GS device or its "
             "shader cache may be stuck, or the process is not a GS stream consumer)");
    if (event.header->type == static_cast<std::uint16_t>(RecordType::error)) {
      proto::ErrorPayload payload{};
      std::memcpy(&payload, event.payload, std::min<std::size_t>(sizeof payload, event.header->payload_bytes));
      refuse(std::string("the GS process refused the session: ") + ascii(payload.text, sizeof payload.text));
    }
    if (event.header->type != static_cast<std::uint16_t>(RecordType::ready) ||
        event.header->payload_bytes < sizeof(proto::ReadyPayload))
      refuse("the GS process sent no READY record first");
    std::memcpy(&ready_, event.payload, sizeof ready_);
    session_->events_in().release(event);
    if (ready_.version_major != proto::kVersionMajor) refuse("the GS process implements another protocol major version");
    session_->set_wait_hooks({&Client::wait_begin, &Client::wait_end});
    if (const char *mode = std::getenv("GT4_GS_READBACK"); mode && *mode)
      std::fprintf(stderr, "[gs-ipc] readback mode %s (GT4_GS_READBACK, applied by the GS process configuration)\n", mode);
    std::fprintf(stderr,
                 "[gs-ipc] GT4_RENDERER=pcsx2: donor Vulkan renderer not created; GS process %s pid %u ready "
                 "(consumer %s, renderer %s, version %s, protocol %u.%u, flags 0x%x, frames in flight %u)\n",
                 process, ready_.consumer_pid, ascii(ready_.consumer, 32).c_str(), ascii(ready_.renderer, 32).c_str(),
                 ascii(ready_.version, 64).c_str(), ready_.version_major, ready_.version_minor, ready_.flags,
                 frames_in_flight);
  }

  void write_capture(RecordType type, std::uint16_t flags, const void *first, std::size_t first_bytes,
                     const void *second = nullptr, std::size_t second_bytes = 0) {
    if (capture_.is_open() && frame_ >= capture_first_)
      capture_.write(type, flags, sequence_ + 1, first, first_bytes, second, second_bytes);
  }

  void count(std::size_t payload_bytes) {
    ++counters_.records;
    counters_.bytes += proto::record_size(payload_bytes);
  }

  // One record to every active output: the capture file and the GS process ring.
  void emit(RecordType type, std::uint16_t flags, const void *first, std::size_t first_bytes,
            const void *second = nullptr, std::size_t second_bytes = 0) {
    write_capture(type, flags, first, first_bytes, second, second_bytes);
    ++sequence_;
    count(first_bytes + second_bytes);
    if (!session_ || dead_) return;
    gt4::perf::Scope write(gt4::perf::Category::gs_ipc_write);
    try {
      session_->post(type, flags, first, first_bytes, second, second_bytes);
    } catch (const ipc::IpcError &error) {
      lost(error.what());
    }
  }

  void lost(const char *what) {
    if (dead_) return;
    dead_ = true;
    std::uint32_t code = 0;
    const bool exited = child_.wait_exit(1000, &code);
    drain_errors();
    exit_known_ = exited;
    exit_code_ = code;
    if (exited)
      std::fprintf(stderr, "[gs-ipc] GS renderer stopped (exit code %u): %s\n", code, what);
    else
      std::fprintf(stderr, "[gs-ipc] GS renderer stopped: %s\n", what);
    std::fprintf(stderr, "[gs-ipc] the game halts after %u more VSYNCs (memory card flush)\n", lost_grace_frames_);
    std::fflush(stderr);
    frames_since_lost_ = 0;
    if (lost_grace_frames_ == 0 && !halted_) {
      halted_ = true;
      if (halt_) halt_("GS process exited");
    }
  }

  void drain_errors() {
    if (!session_) return;
    poll_events();
  }

  void poll_events() {
    if (!session_) return;
    ipc::RingReader &events = session_->events_in();
    ipc::RecordView event;
    while (events.peek(event)) {
      const auto type = static_cast<RecordType>(event.header->type);
      if (type == RecordType::stats && event.header->payload_bytes >= sizeof(proto::StatsPayload)) {
        std::memcpy(&last_stats_, event.payload, sizeof last_stats_);
        has_stats_ = true;
      } else if (type == RecordType::error && event.header->payload_bytes >= sizeof(proto::ErrorPayload)) {
        proto::ErrorPayload payload;
        std::memcpy(&payload, event.payload, sizeof payload);
        std::fprintf(stderr, "[gs-ipc] GS process error %u%s: %s\n", payload.code, payload.fatal ? " (fatal)" : "",
                     ascii(payload.text, sizeof payload.text).c_str());
      } else if (type == RecordType::exiting) {
        std::fprintf(stderr, "[gs-ipc] GS process is exiting\n");
      } else if (type == RecordType::window) {
        std::fprintf(stderr, "[gs-ipc] GS window event\n");
      }
      events.release(event);
    }
  }

  // The consumer's cumulative counters since READY (its latest STATS record); times are per consumer VSYNC.
  static void print_consumer_stats(std::FILE *output, const proto::StatsPayload &stats) {
    const double frames = stats.vsyncs ? static_cast<double>(stats.vsyncs) : 1.0;
    std::fprintf(output,
                 "[gs-ipc] consumer STATS (cumulative): vsyncs %llu records %llu bytes %llu readbacks %llu zero-filled %llu; "
                 "busy ms/frame=%.3f wait ms/frame=%.3f\n",
                 static_cast<unsigned long long>(stats.vsyncs), static_cast<unsigned long long>(stats.records),
                 static_cast<unsigned long long>(stats.bytes), static_cast<unsigned long long>(stats.readbacks),
                 static_cast<unsigned long long>(stats.readbacks_zero_filled), stats.busy_us / 1000.0 / frames,
                 stats.wait_us / 1000.0 / frames);
  }

  // F8 [gs-ipc] section (EE thread, at each probe report).
  void report_interval(std::FILE *file, const gt4::perf::IntervalTime &interval, std::uint64_t frames) {
    if (!frames) return;
    const double scale = static_cast<double>(frames) * 1000.0;
    const auto ms = [&](gt4::perf::Category category) {
      return interval.categories[static_cast<std::size_t>(category)].exclusive_us / scale;
    };
    const double f = static_cast<double>(frames);
    const Counters d{counters_.records - baseline_.records, counters_.bytes - baseline_.bytes,
                     counters_.gif_records - baseline_.gif_records, counters_.gif_bytes - baseline_.gif_bytes,
                     counters_.priv - baseline_.priv, counters_.vsyncs - baseline_.vsyncs,
                     counters_.readbacks - baseline_.readbacks, counters_.readback_bytes - baseline_.readback_bytes,
                     counters_.controls - baseline_.controls, counters_.resets - baseline_.resets};
    std::fprintf(file, "[gs-ipc] renderer %s%s; EE ms/frame: gs_ipc_write=%.3f gs_ipc_backpressure=%.3f "
                 "gs_ipc_vsync_post=%.3f gs_ipc_readback_wait=%.3f\n",
                 stream() ? "pcsx2" : "donor", capture_.is_open() ? " (+capture)" : "", ms(gt4::perf::Category::gs_ipc_write),
                 ms(gt4::perf::Category::gs_ipc_backpressure), ms(gt4::perf::Category::gs_ipc_vsync_post),
                 ms(gt4::perf::Category::gs_ipc_readback_wait));
    std::fprintf(file, "[gs-ipc] stream per frame: bytes=%.0f records=%.1f gif_records=%.1f gif_bytes=%.0f priv=%.2f "
                 "readbacks=%.3f readback_bytes=%.0f (vsyncs %llu)\n",
                 d.bytes / f, d.records / f, d.gif_records / f, d.gif_bytes / f, d.priv / f, d.readbacks / f,
                 d.readback_bytes / f, static_cast<unsigned long long>(d.vsyncs));
    if (readback_interval_.count) readback_interval_.print(file, "interval");
    if (has_stats_) print_consumer_stats(file, last_stats_);
  }

  static void report_interval_hook(std::FILE *file, const gt4::perf::IntervalTime &interval, std::uint64_t frames);
  static void reset_interval_hook();
  static void wait_begin(ipc::Wake which);
  static void wait_end(ipc::Wake which);

 public:
  Counters counters_;
  Counters baseline_;
  ReadbackWaits readback_interval_;  // since the last F7/F8 (or periodic) report
  gt4::perf::Accounting::Token wait_token_ = 0;

 private:
  Renderer renderer_;
  std::unique_ptr<ipc::Session> session_;
  ipc::ChildProcess child_;
  proto::ReadyPayload ready_{};
  proto::FileWriter capture_;
  std::uint64_t capture_first_ = 0, capture_last_ = ~std::uint64_t(0);
  std::uint64_t sequence_ = 0;
  std::uint64_t frame_ = 0;
  std::uint64_t readback_id_ = 0;
  std::uint64_t zero_filled_ = 0;
  proto::StatsPayload last_stats_{};
  bool has_stats_ = false;
  bool dead_ = false;
  bool finished_ = false;
  std::atomic<std::uint64_t> pending_resize_{0};
  void (*halt_)(const char *) = nullptr;
  std::uint64_t window_ = 0;  // game window handle after attach_window (0: headless)
  bool exit_known_ = false;
  std::uint32_t exit_code_ = 0;
  ReadbackWaits readback_session_;
  std::uint32_t handshake_timeout_ms_ = kHandshakeTimeoutMs;
  std::uint32_t lost_grace_frames_ = kLostGraceFrames;
  std::uint32_t frames_since_lost_ = 0;
  bool halted_ = false;
};

Renderer parse_renderer() {
  const char *value = std::getenv("GT4_RENDERER");
  if (!value || std::strcmp(value, "donor") == 0) return Renderer::donor;
  if (std::strcmp(value, "pcsx2") == 0) return Renderer::pcsx2;
  refuse(std::string("GT4_RENDERER=\"") + value + "\" is not donor or pcsx2");
}

// Created on first use; null when the donor renderer runs without a capture (every hook then returns at once).
// Intentionally never destroyed: the GS process ends through finish() or the job object at process exit.
Client *client() {
  static Client *const instance = []() -> Client * {
    const Renderer mode = parse_renderer();
    const char *capture = std::getenv("GT4_GS_CAPTURE");
    if (mode == Renderer::donor && !(capture && *capture)) return nullptr;
    auto *created = new Client(mode);
    created->start();
    return created;
  }();
  return instance;
}

void Client::report_interval_hook(std::FILE *file, const gt4::perf::IntervalTime &interval, std::uint64_t frames) {
  if (Client *c = client()) c->report_interval(file, interval, frames);
}

void Client::reset_interval_hook() {
  if (Client *c = client()) {
    c->baseline_ = c->counters_;
    c->readback_interval_ = {};
  }
}

void Client::wait_begin(ipc::Wake which) {
  Client *c = client();
  if (c && which == ipc::Wake::space && !c->wait_token_)
    c->wait_token_ = gt4::perf::Probe::shared().enter(gt4::perf::Category::gs_ipc_backpressure);
}

void Client::wait_end(ipc::Wake which) {
  Client *c = client();
  if (c && which == ipc::Wake::space && c->wait_token_) {
    gt4::perf::Probe::shared().leave(c->wait_token_);
    c->wait_token_ = 0;
  }
}

// SMODE1 fields (GS privileged register 0x12000010): LC bits 3-9, CMOD bits 13-14 (0 progressive, 2 NTSC, 3 PAL).
// SMODE2 (0x12000020): INT bit 0, FFMD bit 1. Values written for each SetGsCrt request, as in the TASK-012 seam.
constexpr std::uint64_t kCmodProgressive = 0, kCmodNtsc = 2, kCmodPal = 3;
constexpr std::uint64_t kLcAnalog = 32, kLcHdtv = 22, kLcVga = 15;

}  // namespace

bool file_sha256(const std::wstring &path, std::string &hex, std::string &error) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = "CreateFileW failed (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  std::vector<unsigned char> buffer(1 << 20);
  while (ok) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
      ok = false;
      break;
    }
    if (read == 0) break;
    ok = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
  }
  unsigned char digest[32] = {};
  if (ok) ok = BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (!ok) {
    error = "SHA-256 computation failed";
    return false;
  }
  static const char digits[] = "0123456789abcdef";
  hex.clear();
  for (unsigned char byte : digest) {
    hex.push_back(digits[byte >> 4]);
    hex.push_back(digits[byte & 15]);
  }
  return true;
}

bool map_set_gs_crt(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field, std::uint64_t &smode1,
                    std::uint64_t &smode2) {
  if (field > 1 || interlace > 1) return false;
  std::uint64_t cmod = 0, lc = 0;
  if (mode >= 0x1A && mode <= 0x3B) {  // VESA modes: progressive only
    if (interlace) return false;
    cmod = kCmodProgressive;
    lc = kLcVga;
  } else if (mode == 0x02 || mode == 0x03) {  // NTSC, PAL
    cmod = mode == 0x02 ? kCmodNtsc : kCmodPal;
    lc = kLcAnalog;
  } else if (mode == 0x50) {  // DTV 480p
    if (interlace) return false;
    cmod = kCmodProgressive;
    lc = kLcAnalog;
  } else if (mode == 0x51) {  // DTV 1080i
    if (!interlace) return false;
    cmod = kCmodProgressive;
    lc = kLcHdtv;
  } else if (mode == 0x52) {  // DTV 720p
    if (interlace) return false;
    cmod = kCmodProgressive;
    lc = kLcHdtv;
  } else {
    return false;
  }
  smode1 = (cmod << 13) | (lc << 3);
  smode2 = interlace ? (1u | (field ? 2u : 0u)) : 0u;
  return true;
}

bool map_set_gs_crt_syncv(std::uint32_t mode, std::uint64_t &syncv) {
  // SYNCV fields: VFP bits 0-9, VFPE 10-19, VBP 20-31, VBPE 32-41, VDP 42-52, VS 53-63.
  const auto pack = [](std::uint64_t vs, std::uint64_t vdp, std::uint64_t vbpe, std::uint64_t vbp, std::uint64_t vfpe,
                       std::uint64_t vfp) {
    return (vs << 53) | (vdp << 42) | (vbpe << 32) | (vbp << 20) | (vfpe << 10) | vfp;
  };
  if (mode == 0x02) {
    syncv = pack(6, 480, 6, 26, 6, 1);  // 0x00C7800601A01801
    return true;
  }
  if (mode == 0x03) {
    syncv = pack(5, 576, 5, 33, 5, 1);  // 0x00A9000502101401
    return true;
  }
  return false;
}

Renderer renderer() {
  static const Renderer mode = parse_renderer();
  return mode;
}

bool stream_renderer() { return renderer() == Renderer::pcsx2; }

bool on_gif_render(int path, const std::uint8_t *data, std::uint32_t qwc) {
  Client *c = client();
  if (!c) return false;
  if (qwc) c->gif(path, data, qwc);
  return c->stream();
}

void on_privileged_write(std::uint32_t address, std::uint64_t value) {
  if (Client *c = client()) c->privileged(address, value);
}

bool read_fifo(std::uint8_t *data, std::uint32_t qwords) {
  Client *c = client();
  return c ? c->read_fifo(data, qwords) : false;
}

void on_vsync(bool odd_field) {
  if (Client *c = client()) c->vsync(odd_field);
}

void on_gs_reset() {
  if (Client *c = client()) c->reset(::gs_stream::ResetKind::gs_hard, 0);
}

void on_gif_reset() {
  if (Client *c = client()) c->reset(::gs_stream::ResetKind::gif_paths, 0xEu);
}

void on_set_gs_crt(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field) {
  Client *c = client();
  if (!c) return;
  std::uint64_t smode1 = 0, smode2 = 0;
  if (!map_set_gs_crt(interlace, mode, field, smode1, smode2)) {
    std::fprintf(stderr, "[gs-ipc] SetGsCrt interlace %u mode 0x%X field %u not mapped; no SMODE writes posted\n",
                 interlace, mode, field);
    return;
  }
  c->privileged(0x12000010u, smode1);
  std::uint64_t syncv = 0;
  if (map_set_gs_crt_syncv(mode, syncv)) c->privileged(0x12000060u, syncv);
  c->privileged(0x12000020u, smode2);
}

bool attach_window(std::uint64_t window, std::uint32_t width, std::uint32_t height) {
  Client *c = client();
  if (!c || !c->stream()) return false;
  c->attach(window, width, height);
  return true;
}

void resize_window(std::uint32_t width, std::uint32_t height) {
  Client *c = client();
  if (c && c->stream()) c->request_resize(width, height);
}

void set_halt_handler(void (*halt)(const char *reason)) {
  if (Client *c = client()) c->set_halt(halt);
}

void finish(std::FILE *output) {
  if (Client *c = client()) c->finish(output);
}

}  // namespace gt4::gs_stream
