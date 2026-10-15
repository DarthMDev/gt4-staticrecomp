// SPDX-License-Identifier: MIT
// Game-side GS stream backend; see gs_stream_client.hpp and docs/GS-STREAM-PROTOCOL.md section 7.
#include "gs_stream_client.hpp"

#include "gs_stream_file.hpp"
#include "gs_stream_ipc.hpp"
#include "perf_probe.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace gt4::gs_stream {
namespace {

namespace proto = ::gs_stream;
namespace ipc = ::gs_stream::ipc;
using proto::RecordType;

// The serial the producer announces in the control block and in stream files. The consumer uses it only to select
// per-game settings (a GS renderer's game database); it never changes the meaning of a record.
constexpr const char *kGameSerial = "SCUS-97328";
constexpr std::uint32_t kHandshakeTimeoutMs = 20000;
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

[[noreturn]] void refuse(const std::string &message) {
  std::fprintf(stderr, "[gs-ipc] %s; refusing to start\n", message.c_str());
  std::fflush(stderr);
  std::exit(EXIT_FAILURE);
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
          try {
            const std::uint32_t status = session_->read_fifo(part, out);
            if (status == proto::kReadbackZeroFilled) ++zero_filled_;
          } catch (const ipc::IpcError &error) {
            lost(error.what());
            std::memset(out, 0, std::size_t(part) * 16);
          }
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

  void request_resize(std::uint32_t width, std::uint32_t height) {
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
    if (has_stats_) print_consumer_stats(output, last_stats_);
    std::fflush(output);
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
    std::uint32_t frames_in_flight = 2;
    if (const char *value = std::getenv("GT4_GS_FRAMES_IN_FLIGHT")) {
      if (std::strcmp(value, "1") && std::strcmp(value, "2") && std::strcmp(value, "3"))
        refuse(std::string("GT4_GS_FRAMES_IN_FLIGHT=\"") + value + "\" is not 1, 2 or 3");
      frames_in_flight = static_cast<std::uint32_t>(value[0] - '0');
    }
    ipc::SessionOptions options;
    options.serial = kGameSerial;
    options.frames_in_flight = frames_in_flight;
    try {
      session_ = ipc::Session::create_producer(options);
      const std::wstring arguments = L"--ipc " + ipc::widen(session_->name()) + L" --parent " +
                                     std::to_wstring(ipc::current_process_id());
      child_ = ipc::ChildProcess::spawn(ipc::widen(process), arguments);
      session_->set_peer(child_.process());
    } catch (const ipc::IpcError &error) {
      refuse(std::string("cannot start the GS process ") + process + ": " + error.what());
    }
    ipc::RecordView event;
    bool ready = false;
    try {
      ready = session_->wait_event(event, kHandshakeTimeoutMs);
    } catch (const ipc::PeerExited &) {
      std::uint32_t code = 0;
      child_.wait_exit(1000, &code);
      drain_errors();
      refuse(std::string("the GS process ") + process + " exited during the handshake (code " + std::to_string(code) + ")");
    }
    if (!ready) refuse(std::string("the GS process ") + process + " did not answer within 20 s (no READY)");
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
    if (exited)
      std::fprintf(stderr, "[gs-ipc] GS renderer stopped (exit code %u): %s\n", code, what);
    else
      std::fprintf(stderr, "[gs-ipc] GS renderer stopped: %s\n", what);
    std::fflush(stderr);
    if (halt_) halt_("GS process exited");
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
    if (has_stats_) print_consumer_stats(file, last_stats_);
  }

  static void report_interval_hook(std::FILE *file, const gt4::perf::IntervalTime &interval, std::uint64_t frames);
  static void reset_interval_hook();
  static void wait_begin(ipc::Wake which);
  static void wait_end(ipc::Wake which);

 public:
  Counters counters_;
  Counters baseline_;
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
  if (Client *c = client()) c->baseline_ = c->counters_;
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
  c->privileged(0x12000020u, smode2);
}

bool attach_window(std::uint64_t window, std::uint32_t width, std::uint32_t height) {
  Client *c = client();
  if (!c || !c->stream()) return false;
  c->control(::gs_stream::ControlKind::attach_window, window, width, height);
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
