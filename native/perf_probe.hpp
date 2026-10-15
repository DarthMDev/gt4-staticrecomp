#pragma once

#include "perf_accounting.hpp"
#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>

namespace gt4::perf {

struct GuestSample {
  std::uint64_t cycles = 0;
  std::uint64_t primitives = 0;
  std::uint64_t vu1_interpreted = 0;
};

enum class Request { start, report, toggle };

// The optional CPU clock returns UINT64_MAX when unavailable.
struct Clocks {
  std::uint64_t (*wall_us)();
  std::uint64_t (*thread_cpu_us)();
};

class Probe {
public:
  Probe(Clocks clocks, bool enabled, std::FILE *output = stderr, bool detailed = true,
        bool split_vu = false);
  static Probe &shared();
  Accounting::Token enter(Category category) noexcept;
  void leave(Accounting::Token token) noexcept;
  void request(Request request, bool repeated = false) noexcept;
  void frame_boundary(GuestSample guest) noexcept;
  void bytes(bool read, std::uint64_t bytes) noexcept;
  // One GS-to-host readback of `bytes`. `forced` when pending work had to be submitted first;
  // `blocked` when the CPU waited for the GPU.
  void readback(std::uint64_t bytes, bool forced, bool blocked) noexcept;
  void presentation(std::uint64_t rgba_bytes) noexcept;
  void dispatch(std::uint64_t workgroups) noexcept;
  void audio(std::uint64_t produced, std::uint64_t completed,
             std::uint64_t empty_refills = 0) noexcept;
  void finish() noexcept;
  // Extra report lines (for example pacing diagnostics) printed after each
  // interval report. An empty function removes the section.
  void set_report_section(std::function<void(std::FILE *)> section);
  bool enabled() const noexcept { return accounting_.enabled(); }
  bool valid() const noexcept;
  std::uint64_t frames() const noexcept { return frames_; }
  const IntervalTime &interval() const noexcept { return accounting_.interval(); }

private:
  bool owner() noexcept;
  std::uint64_t timestamp() noexcept;
  void reset(std::uint64_t now, GuestSample guest) noexcept;
  void print(const char *reason) noexcept;
  void add(std::uint64_t &value, std::uint64_t amount) noexcept;
  Clocks clocks_;
  std::FILE *output_;
  const bool detailed_;
  const bool split_vu_;
  const std::thread::id owner_thread_;
  std::atomic<bool> wrong_thread_{false};
  Accounting accounting_;
  bool counter_fault_ = false, guest_reset_ = false, cpu_valid_ = false;
  bool first_frame_ = true, manual_ = false;
  bool pending_start_ = false, pending_report_ = false, pending_toggle_ = false;
  std::function<void(std::FILE *)> report_section_;
  std::uint64_t last_us_ = 0, last_frame_us_ = 0, cpu_start_ = 0;
  std::uint64_t cpu_at_boundary_ = UINT64_MAX;
  std::uint64_t frames_ = 0, guest_cycles_ = 0, primitives_ = 0, vu_calls_ = 0;
  GuestSample last_guest_{};
  std::uint64_t read_bytes_ = 0, write_bytes_ = 0, rgba_bytes_ = 0;
  std::uint64_t readbacks_ = 0, readbacks_forced_ = 0, readbacks_blocked_ = 0;
  std::uint64_t presentations_ = 0, dispatches_ = 0, workgroups_ = 0;
  std::uint64_t audio_produced_ = 0, audio_completed_ = 0, empty_refills_ = 0;
  static constexpr std::size_t capacity = 4096;
  std::array<std::uint64_t, capacity> periods_{};
  std::size_t period_count_ = 0, next_period_ = 0;
};

#ifdef GT4_PERF_FRAME_ONLY
class Scope {
public:
  explicit Scope(Category) noexcept {}
};
#else
class Scope {
public:
  explicit Scope(Category category) noexcept : probe_(Probe::shared()),
      token_(probe_.enter(category)) {}
  ~Scope() { if (token_) probe_.leave(token_); }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
private:
  Probe &probe_;
  Accounting::Token token_;
};
#endif
} // namespace gt4::perf

#if defined(GT4_PERF_PROBE) && !defined(GT4_PERF_FRAME_ONLY)
#define GT4_PERF_SCOPE(category) gt4::perf::Scope gt4_perf_scope(category)
#else
#define GT4_PERF_SCOPE(category) ((void)0)
#endif
