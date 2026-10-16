// TASK-011 step 2: VU1 worker thread (MTVU) runtime, used by the slice170 capsule (docs/MTVU-PLAN.md section 11).
//
// Threads: the EE thread (emulation, shadow VIF1, GS front end, journal) and one VU1 worker. The EE posts each
// VIF1 packet, VIF1 register write and VU1 reset as a job with a journal placeholder; the worker runs the donor
// VIF1 parser and the VU1 programs on its own state and emits PATH1/PATH2 GIF bytes in order; the EE drains the
// journal and executes every GS operation (rendering) in serial order.
//
// Only the EE thread calls the EE entry points; the worker calls worker_gif() and the Donor worker callbacks.
#pragma once

#include <cstdint>
#include <cstdio>

namespace gt4::mtvu {

struct Settings {
  bool enabled = true;  // GT4_MTVU: worker thread and shadow VIF1 (default on; 0 restores the single-thread path)
  bool verify = false;  // GT4_MTVU_VERIFY: per-frame GS stream hash, shadow vs worker VIF1 state at each VBlank
  bool strict = false;  // GT4_MTVU_STRICT: abort on a violated invariant (PATH1 signal, verify mismatch)
  // GT4_MTVU_REPORT_EVERY (slice171): print the F8-equivalent probe report every N VBlanks (headless; 0 = off).
  std::uint64_t report_every = 0;
  // GT4_MTVU_PRIV_ASYNC (TASK-041, slice198): privileged GS writes do not wait for the worker when an ordered
  // consumer is registered (the GS stream in pcsx2 mode); they are queued in the journal behind the outstanding
  // worker output instead. No effect in donor mode (no consumer registered).
  bool privileged_async = kPrivilegedAsyncDefault;

  static constexpr bool kPrivilegedAsyncDefault = true;
};

// Each switch accepts unset, "0" or "1"; GT4_MTVU_REPORT_EVERY accepts unset or a VBlank count. Any other value
// throws std::invalid_argument naming the variable.
Settings parse_settings(const char *mtvu, const char *verify, const char *strict, const char *report_every,
                        const char *privileged_async = nullptr);
Settings settings_from_environment();

// TASK-041: ordered privileged GS writes. The GS stream (GT4_RENDERER=pcsx2) needs privileged writes only in stream
// order with the GIF output; the EE keeps its own CSR/IMR/SIGLBLID copy at the program point. The consumer registers
// the function that posts a write; with GT4_MTVU_PRIV_ASYNC=1, sync("gs_privileged") then returns without waiting and
// the consumer calls queue_privileged for each write.
void set_ordered_privileged_consumer(void (*post)(std::uint32_t address, std::uint64_t value));
// True when GT4_MTVU_PRIV_ASYNC is on, the worker runs and a consumer is registered.
bool privileged_async();
// EE thread: true when the write was queued behind journal entries (the drain posts it in order through the
// consumer); false when the journal is empty or the mode is off, and the caller posts it now.
bool queue_privileged(std::uint32_t address, std::uint64_t value);
// EE wait of the last blocking gs_privileged sync in microseconds, then 0 (per-register accounting).
std::uint64_t take_privileged_wait_us();

// Donor operations. The worker callbacks run on the worker thread; render_gs runs on the EE thread.
struct Donor {
  void (*worker_vif_packet)(const std::uint8_t *data, std::uint32_t qwc);
  void (*worker_vif_write)(std::uint32_t reg, std::uint32_t value);
  void (*worker_vu_reset)();
  void (*render_gs)(int path, const std::uint8_t *data, std::uint32_t qwc);
  // Hash of the VIF1 state: the worker side (true) or the EE shadow (false). Called on the EE after a sync.
  std::uint64_t (*vif1_state_hash)(bool worker_side);
  // gs::signal (SIGNAL/FINISH/LABEL), applied on the EE when a PATH2/PATH3 packet is accepted (docs section 12).
  void (*apply_signal)(std::uint32_t reg, std::uint64_t value);
};

// Starts the worker when settings.enabled (EE thread, before emulation). Verify hashing works either way.
void start(const Settings &settings, const Donor &donor);

// Starts the worker with the donor callbacks of mtvu_bridge.cpp (slice170 capsule builds only).
void start_with_donor(const Settings &settings);

// True while the worker runs and GT4_MTVU is on: VIF1 is then shadowed on the EE thread.
bool enabled();
// True on the VU1 worker thread.
bool on_worker();
// TASK-062: cumulative busy time of the VU1 worker (VU1 programs plus its PATH1/PATH2 GIF output) in microseconds, as
// published after each job; 0 when the worker never ran or the performance accounting is off. Any thread.
std::uint64_t worker_busy_us();

// EE entry points (no-ops when not enabled, except gif_transfer which renders directly).
void post_vif_packet(const std::uint8_t *data, std::uint32_t qwc);
void post_vif_write(std::uint32_t reg, std::uint32_t value);
// EE GIF transfer (PATH3 from DMA, or any path on the EE). Scans signals; a signal forces a synchronous drain.
void gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc);
// Shadow VIF1 DIRECT/DIRECTHL data (the worker emits it): scans signals; a signal forces a synchronous drain.
void shadow_direct(int path, const std::uint8_t *data, std::uint32_t qwc);
// GIF reset: synchronous drain, then the EE scanners restart.
void gif_reset();
// True when a PATH2/PATH3 PACKED A+D write to `address` (0x60-0x62) was already applied on the EE by gif_transfer or
// shadow_direct, so the drained render must skip it. PATH1 is never pre-applied (its signals stay at render time).
bool signal_pre_applied(int path, std::uint32_t address);
// Every GS render (EE thread, journal or direct) passes here for the verify stream hash.
void note_render(int path, const std::uint8_t *data, std::uint32_t qwc);

// Blocking drain: the EE waits until the worker is idle and every journal entry is executed. No-op when disabled
// or on the worker. `reason` is counted for the summary.
void sync(const char *reason);
// Non-blocking drain: executes the journal entries that are complete. Cheap when nothing is queued.
void pump();
// Guest VBlank, before the host presents: synchronous drain, then the verify report.
void vblank_sync();

// Worker side, called by the donor GIF transfer when the worker produces GIF bytes (PATH1 XGKICK, PATH2 DIRECT).
void worker_gif(int path, const std::uint8_t *data, std::uint32_t qwc);

// Joins the worker after a final drain. Safe to call when not started.
void stop();
// Prints the counters (one "[mtvu]" line each) to `output`.
void report(std::FILE *output);

}  // namespace gt4::mtvu
