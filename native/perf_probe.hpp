// Bounded host-side diagnostics for the owner-controlled performance
// investigation.
//
// The probe only observes host wall time and host counters. It never changes
// guest state, guest timing, scheduling, audio content or rendering. The
// current runtime executes the guest, the GS submissions and the presentation
// on a single thread, so plain counters are sufficient here; if a second
// thread is ever introduced, the accounting below must become atomic and the
// attribution rules must be revisited.
//
// Wall time is measured with QueryPerformanceCounter in microseconds and is
// never used to advance guest time.

#ifndef GT4_PERF_PROBE_HPP
#define GT4_PERF_PROBE_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif
// The performance probe needs a host clock and a diagnostic variable. The
// donor translation units that include this header are platform-neutral
// sources, so the Windows headers must not leak SDK macros into them:
// rpcndr.h defines the legacy macro `small`, which collides with a variable in
// ps2/vu_ops.hpp, so it is removed right after the include. NOMINMAX keeps the
// min/max macros out as well.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef small
#undef small
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gt4::perf {

// Host monotonic microseconds.
inline std::uint64_t now_us() {
  static const std::int64_t frequency = [] {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return value.QuadPart > 0 ? value.QuadPart : 1;
  }();
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  return static_cast<std::uint64_t>(counter.QuadPart) * 1000000ull /
         static_cast<std::uint64_t>(frequency);
}

// Reads a short diagnostic variable without the deprecated C runtime accessor.
inline std::string environment_value(const char *name) {
  char buffer[32]{};
  const DWORD length = GetEnvironmentVariableA(name, buffer, sizeof buffer);
  if (!length || length >= sizeof buffer)
    return {};
  return std::string(buffer, length);
}

// Wall-time stages of one guest VBlank cycle.
enum class Stage {
  host_vblank, // host callback: window pump, input poll, guest presentation
  gs_upload,   // host copies into the mapped Vulkan buffers
  gs_execute,  // GS compute submission and its completion wait
  present,     // acquire, blit submission and present
  audio_push,  // WinMM sample production from the guest SPU2
  // Subsystem attribution of the guest execution time. `ee_dispatch` is the
  // translated EE execution; `vu_interpret` and `vif_gif` run inside it, while
  // `iop_run` is a sibling of it.
  ee_dispatch,
  vu_interpret,
  vif_gif,
  iop_run,
  // Blocking waits observed on the emulation thread. Their time is already
  // included in the enclosing stage above; they are recorded separately to
  // attribute the wait instead of the work. They never nest inside each other.
  gs_submit_wait,
  acquire_wait,
  present_wait,
  audio_wait,
  count
};

const char *stage_name(Stage stage);

struct Totals {
  std::uint64_t calls = 0;
  std::uint64_t total_us = 0;
  std::uint64_t max_us = 0;
  void add(std::uint64_t elapsed_us) {
    ++calls;
    total_us += elapsed_us;
    if (elapsed_us > max_us)
      max_us = elapsed_us;
  }
  double milliseconds() const { return static_cast<double>(total_us) / 1000.0; }
};

// Guest-side counters sampled once per guest VBlank frame. The host supplies
// them so the probe stays independent of the runtime implementation. They are
// cumulative runtime counters; the probe stores their deltas.
struct GuestSample {
  std::uint64_t guest_cycles = 0;    // ps2::now()
  std::uint64_t guest_frames = 0;    // ps2::frames_run()
  std::uint64_t gs_primitives = 0;   // ps2::primitives_drawn()
  std::uint64_t vu1_interpreted = 0; // ps2::vu1_interpreted_calls()
};

class Probe {
public:
  static Probe &shared() {
    static Probe probe;
    return probe;
  }

  bool enabled() const { return enabled_; }

  // Explicitly disable the probe without touching the process environment,
  // used to compare instrumented and uninstrumented overhead.
  void set_enabled(bool enabled) { enabled_ = enabled; }

  // Seconds between automatic interval reports; tests use it to keep the
  // measured interval stable across the whole fixture run.
  void set_report_period_seconds(int seconds) {
    period_seconds_ = seconds < 1 ? 1 : seconds;
  }

  // Closes the current guest VBlank cycle and opens the next one.
  void frame_boundary(const GuestSample &guest);

  // Prints the accumulated interval and starts a new one.
  void report(const char *reason);

  // Discards the accumulated interval, keeping the frame clock running.
  void restart();

  // Records a blocking wait. Its duration is also added to the frame blocked
  // total, because it is time the emulation thread performs no work.
  void note_wait(Stage stage, std::uint64_t microseconds) {
    if (!enabled_ || stage == Stage::count)
      return;
    stages_[static_cast<std::size_t>(stage)].add(microseconds);
    blocked_us_ += microseconds;
  }

  void close_stage(Stage stage, std::uint64_t elapsed_us) {
    if (!enabled_ || stage == Stage::count)
      return;
    stages_[static_cast<std::size_t>(stage)].add(elapsed_us);
    // The host VBlank duration of the cycle that just ended is needed to
    // attribute the remaining time to guest execution.
    if (stage == Stage::host_vblank)
      last_vblank_us_ = elapsed_us;
  }

  // Observers used by the automated fixture to check the accounting.
  Totals stage(Stage stage) const {
    return stages_[static_cast<std::size_t>(stage)];
  }
  std::uint64_t interval_us() const { return interval_us_; }
  std::uint64_t guest_between_us() const { return guest_between_us_; }
  std::uint64_t blocked_us() const { return blocked_us_; }
  std::uint64_t guest_cycles() const { return guest_cycles_; }
  std::uint64_t guest_primitives() const { return guest_primitives_; }
  std::uint64_t guest_vu1_calls() const { return guest_vu1_calls_; }

  void add_dispatch(std::uint64_t workgroups) {
    if (!enabled_)
      return;
    ++dispatches_;
    workgroups_ += workgroups;
  }

  void add_presentation(std::uint64_t bytes) {
    if (!enabled_)
      return;
    ++presentations_;
    presentation_bytes_ += bytes;
  }

  void add_audio(std::uint64_t produced_frames, std::uint64_t completed_frames) {
    if (!enabled_)
      return;
    audio_in_frames_ += produced_frames;
    audio_completed_frames_ += completed_frames;
  }

  std::uint64_t frames() const { return frames_; }

  // Keeps at most kPeriodCapacity of the most recent frame periods.
  void record_period(std::uint64_t period_us) {
    if (periods_.size() == kPeriodCapacity)
      periods_.erase(periods_.begin());
    periods_.push_back(period_us);
  }

  // Formats one measurement interval. Exposed for tests, which pass a file
  // stream through the caller.
  void print_interval(std::FILE *stream, const char *reason);

private:
  Probe() {
    if (const std::string spec = environment_value("GT4_PERF"); !spec.empty())
      enabled_ = spec != "0";
    if (const std::string spec = environment_value("GT4_PERF_PERIOD");
        !spec.empty())
      period_seconds_ = std::atoi(spec.c_str());
    if (period_seconds_ < 1)
      period_seconds_ = 1;
    periods_.reserve(kPeriodCapacity);
  }

  static constexpr std::size_t kPeriodCapacity = 4096;

  std::array<Totals, static_cast<std::size_t>(Stage::count)> stages_{};
  bool enabled_ = true;
  bool first_frame_ = true;
  int period_seconds_ = 5;
  std::uint64_t last_frame_us_ = 0;
  std::uint64_t last_report_us_ = 0;
  std::uint64_t last_vblank_us_ = 0;
  std::uint64_t interval_us_ = 0;
  std::uint64_t guest_between_us_ = 0;
  std::uint64_t blocked_us_ = 0;
  std::uint64_t frames_ = 0;
  std::uint64_t dispatches_ = 0, workgroups_ = 0;
  std::uint64_t presentations_ = 0, presentation_bytes_ = 0;
  std::uint64_t audio_in_frames_ = 0, audio_completed_frames_ = 0;
  std::uint64_t guest_cycles_ = 0;
  std::uint64_t guest_primitives_ = 0, guest_vu1_calls_ = 0;
  std::uint64_t last_guest_cycles_ = 0, last_guest_primitives_ = 0,
                last_guest_vu1_calls_ = 0;
  // Bounded history of frame periods for percentiles; the oldest sample is
  // dropped once an interval exceeds the capacity.
  std::vector<std::uint64_t> periods_;
};

// RAII measurement of one wall-time stage.
class Scope {
public:
  explicit Scope(Stage stage)
      : stage_(stage), probe_(Probe::shared()), start_us_(now_us()) {}
  ~Scope() { probe_.close_stage(stage_, now_us() - start_us_); }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

private:
  Stage stage_;
  Probe &probe_;
  std::uint64_t start_us_;
};

inline const char *stage_name(Stage stage) {
  switch (stage) {
  case Stage::host_vblank:
    return "host_vblank";
  case Stage::gs_upload:
    return "gs_upload";
  case Stage::gs_execute:
    return "gs_execute";
  case Stage::present:
    return "present";
  case Stage::audio_push:
    return "audio_push";
  case Stage::ee_dispatch:
    return "ee_dispatch";
  case Stage::vu_interpret:
    return "vu_interpret";
  case Stage::vif_gif:
    return "vif_gif";
  case Stage::iop_run:
    return "iop_run";
  case Stage::gs_submit_wait:
    return "gs_submit_wait";
  case Stage::acquire_wait:
    return "acquire_wait";
  case Stage::present_wait:
    return "present_wait";
  case Stage::audio_wait:
    return "audio_wait";
  case Stage::count:
    break;
  }
  return "unknown";
}

inline void Probe::restart() {
  const auto now = now_us();
  frames_ = 0;
  interval_us_ = 0;
  guest_between_us_ = 0;
  blocked_us_ = 0;
  for (auto &totals : stages_)
    totals = Totals{};
  dispatches_ = workgroups_ = presentations_ = presentation_bytes_ = 0;
  audio_in_frames_ = audio_completed_frames_ = 0;
  guest_cycles_ = guest_primitives_ = guest_vu1_calls_ = 0;
  last_guest_cycles_ = last_guest_primitives_ = last_guest_vu1_calls_ = 0;
  periods_.clear();
  last_report_us_ = now;
  last_vblank_us_ = 0;
  first_frame_ = true;
}

inline void Probe::report(const char *reason) {
  print_interval(stderr, reason);
  restart();
}

inline void Probe::frame_boundary(const GuestSample &guest) {
  if (!enabled_)
    return;
  const auto now = now_us();
  if (first_frame_) {
    first_frame_ = false;
    last_frame_us_ = now;
    last_report_us_ = now;
    // Cumulative runtime counters: remember the baseline before storing deltas.
    last_guest_cycles_ = guest.guest_cycles;
    last_guest_primitives_ = guest.gs_primitives;
    last_guest_vu1_calls_ = guest.vu1_interpreted;
    return;
  }
  const std::uint64_t period_us = now - last_frame_us_;
  last_frame_us_ = now;
  ++frames_;
  interval_us_ += period_us;
  record_period(period_us);
  if (guest.guest_cycles >= last_guest_cycles_)
    guest_cycles_ += guest.guest_cycles - last_guest_cycles_;
  last_guest_cycles_ = guest.guest_cycles;
  if (guest.gs_primitives >= last_guest_primitives_)
    guest_primitives_ += guest.gs_primitives - last_guest_primitives_;
  last_guest_primitives_ = guest.gs_primitives;
  if (guest.vu1_interpreted >= last_guest_vu1_calls_)
    guest_vu1_calls_ += guest.vu1_interpreted - last_guest_vu1_calls_;
  last_guest_vu1_calls_ = guest.vu1_interpreted;
  // Everything outside the host VBlank callback is guest execution, GS
  // batching and whatever wait that work performs on this thread.
  guest_between_us_ += period_us > last_vblank_us_ ? period_us - last_vblank_us_
                                                   : 0;
  last_vblank_us_ = 0;
  if (now - last_report_us_ >=
      static_cast<std::uint64_t>(period_seconds_) * 1000000ull)
    report("periodic");
}

inline void Probe::print_interval(std::FILE *stream, const char *reason) {
  if (!frames_) {
    std::fprintf(stream, "[perf] %s: no complete guest frame yet\n", reason);
    return;
  }
  std::vector<std::uint64_t> sorted(periods_);
  std::sort(sorted.begin(), sorted.end());
  const auto percentile = [&sorted](double fraction) {
    const std::size_t index = static_cast<std::size_t>(
        fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index] / 1000.0;
  };
  const double interval_seconds = interval_us_ / 1000000.0;
  const double fps = frames_ / interval_seconds;
  const double guest_seconds = guest_cycles_ / 294912000.0;
  const double work_us =
      static_cast<double>(interval_us_) - static_cast<double>(blocked_us_);
  std::fprintf(stream,
               "[perf] %s: %llu guest frames in %.2fs wall = %.1f frames/s "
               "(%.1f%% of the 60 Hz guest VBlank rate)\n",
               reason, static_cast<unsigned long long>(frames_),
               interval_seconds, fps,
               100.0 * fps / 60.0);
  std::fprintf(stream,
               "[perf]   guest progress: %llu guest cycles = %.2fs at "
               "294.912 MHz = %.1f%% of hardware rate in wall time\n",
               static_cast<unsigned long long>(guest_cycles_), guest_seconds,
               100.0 * guest_seconds / interval_seconds);
  std::fprintf(stream,
               "[perf]   frame period ms: median %.2f p95 %.2f p99 %.2f max %.2f "
               "(%.1f ms budget at 60 Hz)\n",
               percentile(0.5), percentile(0.95), percentile(0.99),
               sorted.back() / 1000.0, 1000.0 / 60.0);
  std::fprintf(stream,
               "[perf]   per frame ms: guest_between %.2f host_vblank %.2f; "
               "thread work %.2f blocked %.2f (%.1f%% of wall)\n",
               guest_between_us_ / 1000.0 / static_cast<double>(frames_),
               stages_[static_cast<std::size_t>(Stage::host_vblank)].milliseconds() /
                   static_cast<double>(frames_),
               work_us / 1000.0 / static_cast<double>(frames_),
               blocked_us_ / 1000.0 / static_cast<double>(frames_),
               100.0 * static_cast<double>(blocked_us_) /
                   static_cast<double>(interval_us_));
  const auto per_frame = [this, frames = frames_](Stage stage) {
    return stages_[static_cast<std::size_t>(stage)].milliseconds() /
           static_cast<double>(frames);
  };
  std::fprintf(stream,
               "[perf]   waits ms/frame: gs_submit_wait %.2f acquire_wait %.2f "
               "present_wait %.2f audio_wait %.2f\n",
               per_frame(Stage::gs_submit_wait), per_frame(Stage::acquire_wait),
               per_frame(Stage::present_wait), per_frame(Stage::audio_wait));
  std::fprintf(stream,
               "[perf]   stages ms: gs_upload %.2f gs_execute %.2f present %.2f "
               "audio_push %.2f\n",
               per_frame(Stage::gs_upload), per_frame(Stage::gs_execute),
               per_frame(Stage::present), per_frame(Stage::audio_push));
  // Subsystem attribution of the guest execution outside the host callback.
  const double guest_ms =
      guest_between_us_ / 1000.0 / static_cast<double>(frames_);
  const double dispatch_ms = per_frame(Stage::ee_dispatch);
  const double iop_ms = per_frame(Stage::iop_run);
  const double other_ms =
      guest_ms - dispatch_ms - iop_ms > 0 ? guest_ms - dispatch_ms - iop_ms : 0;
  std::fprintf(stream,
               "[perf]   attribution ms: guest_between %.2f = ee_dispatch %.2f "
               "(%.0f%%) + iop_run %.2f + other %.2f\n",
               guest_ms, dispatch_ms, 100.0 * dispatch_ms / guest_ms, iop_ms,
               other_ms);
  std::fprintf(stream,
               "[perf]   nested inside ee_dispatch ms: vu_interpret %.2f "
               "vif_gif %.2f\n",
               per_frame(Stage::vu_interpret), per_frame(Stage::vif_gif));
  std::fprintf(stream,
               "[perf]   counts per frame: executes %.2f dispatches %.0f "
               "workgroups %.0f presents %.2f upload bytes %.0f\n",
               stages_[static_cast<std::size_t>(Stage::gs_execute)].calls /
                   static_cast<double>(frames_),
               static_cast<double>(dispatches_) / static_cast<double>(frames_),
               static_cast<double>(workgroups_) / static_cast<double>(frames_),
               static_cast<double>(presentations_) / static_cast<double>(frames_),
               static_cast<double>(presentation_bytes_) /
                   static_cast<double>(frames_));
  std::fprintf(stream,
               "[perf]   guest per frame: primitives %.0f vu1_interpreted %.0f "
               "audio frames %.0f (device completed %.0f)\n",
               static_cast<double>(guest_primitives_) /
                   static_cast<double>(frames_),
               static_cast<double>(guest_vu1_calls_) /
                   static_cast<double>(frames_),
               static_cast<double>(audio_in_frames_) /
                   static_cast<double>(frames_),
               static_cast<double>(audio_completed_frames_) /
                   static_cast<double>(frames_));
  std::fflush(stream);
}

} // namespace gt4::perf

#endif // GT4_PERF_PROBE_HPP
