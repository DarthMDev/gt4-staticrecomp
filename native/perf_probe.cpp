#include "perf_probe.hpp"

#ifdef GT4_VU1_AOT
namespace ps2::vu {
__attribute__((weak)) void aot_report(std::FILE *) {}  // strong definition: VU1 AOT overlay
}
#endif
#ifdef GT4_VU0_AOT
namespace ps2::vu {
__attribute__((weak)) void vu0_aot_report(std::FILE *) {}  // strong definition: VU0 AOT overlay (slice 160)
}
#endif
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#define NOMINMAX
#include <windows.h>

namespace gt4::perf {
namespace {
std::uint64_t wall_us() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::uint64_t cpu_us() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
    return UINT64_MAX;
  const auto ticks = [](FILETIME value) {
    return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
  };
  // Divide before adding: FILETIME units are 100ns, not guest cycles.
  return ticks(kernel) / 10 + ticks(user) / 10;
}
constexpr const char *names[] = {
    "ee_dispatch", "vu_interpret", "iop_run", "vif_gif", "gs_host_copy",
    "gs_execute", "presentation", "audio_push", "host_vblank",
    "gs_fence_wait", "acquire_wait", "present_wait", "audio_wait", "unattributed",
    "vu0_micro_interpret", "vu1_interpret"
#ifdef GT4_VU1_AOT
    , "vu1_aot"
#endif
#ifdef GT4_VU0_AOT
    , "vu0_aot"
#endif
    };
static_assert(std::size(names) == static_cast<std::size_t>(Category::count));
}

Probe::Probe(Clocks clocks, bool enabled, std::FILE *output, bool detailed, bool split_vu)
    : clocks_(clocks), output_(output), detailed_(detailed), split_vu_(split_vu),
      owner_thread_(std::this_thread::get_id()),
      accounting_(enabled) {
  last_us_ = clocks_.wall_us();
  accounting_.restart(last_us_);
}

Probe &Probe::shared() {
#ifdef GT4_PERF_FRAME_ONLY
  constexpr bool detailed = false;
#else
  constexpr bool detailed = true;
#endif
#ifdef GT4_PERF_VU_SPLIT
  constexpr bool split_vu = true;
#else
  constexpr bool split_vu = false;
#endif
  static Probe probe({wall_us, cpu_us}, [] {
    char value[8]{};
    return GetEnvironmentVariableA("GT4_PERF", value, sizeof value) == 1 && value[0] == '1';
  }(), stderr, detailed, split_vu);
  return probe;
}

bool Probe::owner() noexcept {
  if (std::this_thread::get_id() == owner_thread_)
    return true;
  wrong_thread_.store(true);
  return false;
}

bool Probe::valid() const noexcept {
  return !wrong_thread_.load() && !counter_fault_ &&
         accounting_.fault() == AccountingFault::none;
}

std::uint64_t Probe::timestamp() noexcept {
  if (enabled())
    last_us_ = clocks_.wall_us();
  return last_us_;
}

Accounting::Token Probe::enter(Category category) noexcept {
  return owner() ? accounting_.enter(category, timestamp()) : 0;
}
void Probe::leave(Accounting::Token token) noexcept {
  if (owner()) accounting_.leave(token, timestamp());
}
void Probe::request(Request request, bool repeated) noexcept {
  if (!owner() || repeated) return;
  switch (request) {
  case Request::start: pending_start_ = true; break;
  case Request::report: pending_report_ = true; break;
  case Request::toggle: pending_toggle_ = !pending_toggle_; break;
  }
}

void Probe::add(std::uint64_t &value, std::uint64_t amount) noexcept {
  if (!owner() || !enabled()) return;
  if (amount > UINT64_MAX - value) counter_fault_ = true;
  else value += amount;
}
void Probe::bytes(bool read, std::uint64_t bytes) noexcept {
  add(read ? read_bytes_ : write_bytes_, bytes);
}
void Probe::readback(std::uint64_t bytes, bool forced, bool blocked) noexcept {
  add(readbacks_, 1);
  if (forced) add(readbacks_forced_, 1);
  if (blocked) add(readbacks_blocked_, 1);
  this->bytes(true, bytes);
}
void Probe::presentation(std::uint64_t rgba_bytes) noexcept {
  add(presentations_, 1); add(rgba_bytes_, rgba_bytes);
}
void Probe::dispatch(std::uint64_t workgroups) noexcept {
  add(dispatches_, 1); add(workgroups_, workgroups);
}
void Probe::audio(std::uint64_t produced, std::uint64_t completed,
                  std::uint64_t empty_refills) noexcept {
  add(audio_produced_, produced); add(audio_completed_, completed);
  add(empty_refills_, empty_refills);
}

void Probe::reset(std::uint64_t now, GuestSample guest) noexcept {
  accounting_.restart(now);
  last_us_ = last_frame_us_ = now;
  last_guest_ = guest;
  frames_ = guest_cycles_ = primitives_ = vu_calls_ = 0;
  read_bytes_ = write_bytes_ = rgba_bytes_ = 0;
  readbacks_ = readbacks_forced_ = readbacks_blocked_ = 0;
  presentations_ = dispatches_ = workgroups_ = 0;
  audio_produced_ = audio_completed_ = empty_refills_ = 0;
  period_count_ = next_period_ = 0;
  guest_reset_ = false;
  first_frame_ = false;
  cpu_start_ = cpu_at_boundary_;
  cpu_valid_ = cpu_start_ != UINT64_MAX;
}

void Probe::frame_boundary(GuestSample guest) noexcept {
  if (!owner()) return;
  if (!enabled() && !pending_toggle_ && !pending_start_ && !pending_report_) return;
  const auto now = clocks_.wall_us();
  cpu_at_boundary_ = clocks_.thread_cpu_us ? clocks_.thread_cpu_us() : UINT64_MAX;
  last_us_ = now;
  accounting_.checkpoint(now);
  if (enabled() && !first_frame_) {
    add(frames_, 1);
    periods_[next_period_] = now >= last_frame_us_ ? now - last_frame_us_ : 0;
    next_period_ = (next_period_ + 1) % capacity;
    period_count_ = std::min(period_count_ + 1, capacity);
    const auto delta = [this](auto current, auto previous, auto &total) {
      if (current < previous) guest_reset_ = true;
      else add(total, current - previous);
    };
    delta(guest.cycles, last_guest_.cycles, guest_cycles_);
    delta(guest.primitives, last_guest_.primitives, primitives_);
    delta(guest.vu1_interpreted, last_guest_.vu1_interpreted, vu_calls_);
    last_frame_us_ = now;
    last_guest_ = guest;
  } else if (enabled()) reset(now, guest);

  // Preserve a requested report before a same-boundary pause/restart.
  if (pending_report_) {
    print("manual (F8)");
    reset(now, guest);
    manual_ = false;
  }
  if (pending_toggle_) {
    if (enabled() && !pending_report_) print("diagnostics paused (F9)");
    accounting_.set_enabled(!enabled(), now);
    reset(now, guest);
    manual_ = false;
    std::fprintf(output_, "[perf] diagnostics %s at VBlank; new interval\n",
                 enabled() ? "enabled" : "disabled");
  }
  if (pending_start_) {
    if (!enabled()) accounting_.set_enabled(true, now);
    reset(now, guest);
    manual_ = true;
    std::fputs("[perf] manual interval started (F7); automatic resets suspended\n", output_);
  }
  const bool periodic = !manual_ && enabled() && accounting_.interval().elapsed_us >= 5000000;
  if (periodic) {
    print("periodic");
    reset(now, guest);
    manual_ = false;
  }
  pending_toggle_ = pending_start_ = pending_report_ = false;
}

void Probe::print(const char *reason) noexcept {
  if (!enabled()) { std::fprintf(output_, "[perf] %s: disabled\n", reason); return; }
  if (!valid()) {
    std::fprintf(output_, "[perf] INVALID: accounting_fault=%u wrong_thread=%d counter_fault=%d\n",
                 static_cast<unsigned>(accounting_.fault()), int(wrong_thread_.load()), int(counter_fault_));
    return;
  }
  const auto elapsed = accounting_.interval().elapsed_us;
  if (!frames_ || !elapsed || !period_count_) {
    std::fprintf(output_, "[perf] %s: no complete interval\n", reason); return;
  }
  std::array<std::uint64_t, capacity> sorted = periods_;
  std::sort(sorted.begin(), sorted.begin() + period_count_);
  const auto percentile = [&sorted, count = period_count_](std::size_t percent) {
    const auto rank = (percent * count + 99) / 100;
    return sorted[rank - 1] / 1000.0;
  };
  const double median = period_count_ % 2 ? sorted[period_count_ / 2] / 1000.0 :
      sorted[period_count_ / 2 - 1] / 2000.0 + sorted[period_count_ / 2] / 2000.0;
  const double frames = static_cast<double>(frames_);
  const double seconds = elapsed / 1000000.0;
  std::fprintf(output_, "[perf] %s: %llu VBlank samples in %.3fs = %.2f VBlanks/s; fresh_scene_frames=Unknown; measurement_mode=%s\n",
               reason, static_cast<unsigned long long>(frames_), seconds, frames / seconds,
               split_vu_ ? "vu-split" : detailed_ ? "detailed" : "frame-only");
  std::fprintf(output_, "[perf] frame ms: median %.3f p95 %.3f p99 %.3f max %.3f; samples=%zu/%llu (tail cap4096)\n",
               median, percentile(95), percentile(99), sorted[period_count_ - 1] / 1000.0,
               period_count_, static_cast<unsigned long long>(frames_));
  if (!guest_reset_)
    std::fprintf(output_, "[perf] guest_clock_model_speed=%.2f%% (294.912MHz); primitives/frame=%.1f vu1_interpreted/frame=%.1f\n",
        guest_cycles_ / 294912000.0 / seconds * 100.0, primitives_ / frames, vu_calls_ / frames);
  else std::fputs("[perf] guest counters reset: guest speed/count deltas Unknown\n", output_);
  const auto cpu_now = cpu_at_boundary_;
  if (cpu_valid_ && cpu_now != UINT64_MAX && cpu_now >= cpu_start_)
    std::fprintf(output_, "[perf] measured_thread_cpu_ms/frame=%.3f; occupancy=%.1f%%\n",
        (cpu_now - cpu_start_) / 1000.0 / frames, double(cpu_now - cpu_start_) / elapsed * 100);
  else std::fputs("[perf] measured_thread_cpu=Unknown\n", output_);
  if (report_section_)
    report_section_(output_);
  if (!detailed_) {
    std::fputs("[perf] subsystem timers/host traffic counters=Unavailable (compiled out); boundary clocks/guest counters remain\n", output_);
    std::fflush(output_);
    return;
  }
  std::fputs("[perf] exclusive wall ms/frame (additive, includes instrumentation/scheduler time):", output_);
  for (std::size_t index = 0; index < std::size(names); ++index)
    std::fprintf(output_, " %s=%.3f", names[index],
        accounting_.interval().categories[index].exclusive_us / 1000.0 / frames);
  std::fputc('\n', output_);
  std::fputs("[perf] inclusive wall ms/frame (overlapping, DO NOT SUM):", output_);
  for (std::size_t index = 0; index < std::size(names); ++index)
    std::fprintf(output_, " %s=%.3f", names[index],
        accounting_.interval().categories[index].inclusive_us / 1000.0 / frames);
  std::fprintf(output_, "\n[perf] mapped_write_bytes/frame=%.0f mapped_read_bytes/frame=%.0f presented_RGBA_bytes/frame=%.0f presents/frame=%.2f dispatches/frame=%.1f workgroups/frame=%.0f\n",
      write_bytes_ / frames, read_bytes_ / frames, rgba_bytes_ / frames,
      presentations_ / frames, dispatches_ / frames, workgroups_ / frames);
  std::fprintf(output_, "[perf] gs_readback/frame=%.2f bytes/frame=%.0f forced_flushes/frame=%.2f blocking_waits/frame=%.2f\n",
      readbacks_ / frames, read_bytes_ / frames, readbacks_forced_ / frames, readbacks_blocked_ / frames);
  std::fprintf(output_, "[perf] audio frames/frame: produced=%.1f completed=%.1f empty_refills=%.2f; gpu_timestamp_time=Unknown\n",
      audio_produced_ / frames, audio_completed_ / frames, empty_refills_ / frames);
  if (split_vu_) {
    const auto &categories = accounting_.interval().categories;
    const double total_us = static_cast<double>(categories[static_cast<std::size_t>(Category::vu0_micro_interpret)].exclusive_us) +
        categories[static_cast<std::size_t>(Category::vu1_interpret)].exclusive_us;
#ifdef GT4_VU1_AOT
    std::fprintf(output_, "[perf] vu_scope=unit-split run_fast only; vu_interpreter_total_exclusive_ms/frame=%.3f; VU0_macro_in_EE; generated_VU1_AOT_included (vu1_aot category)\n",
                 total_us / 1000.0 / frames);
#else
    std::fprintf(output_, "[perf] vu_scope=unit-split run_fast only; vu_interpreter_total_exclusive_ms/frame=%.3f; VU0_macro_in_EE; generated_VU1_AOT_excluded\n",
                 total_us / 1000.0 / frames);
#endif
  }
#ifdef GT4_VU1_AOT
  ps2::vu::aot_report(output_);
#endif
#ifdef GT4_VU0_AOT
  ps2::vu::vu0_aot_report(output_);
#endif
  std::fflush(output_);
}

void Probe::set_report_section(std::function<void(std::FILE *)> section) {
  if (owner())
    report_section_ = std::move(section);
}

void Probe::finish() noexcept {
  if (owner()) std::fputs("[perf] session end: partial interval omitted; use F8 before closing\n", output_);
}
} // namespace gt4::perf
