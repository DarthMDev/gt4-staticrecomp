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
};

// Each switch accepts unset, "0" or "1"; GT4_MTVU_REPORT_EVERY accepts unset or a VBlank count. Any other value
// throws std::invalid_argument naming the variable.
Settings parse_settings(const char *mtvu, const char *verify, const char *strict, const char *report_every);
Settings settings_from_environment();

// Donor operations. The worker callbacks run on the worker thread; render_gs runs on the EE thread.
struct Donor {
  void (*worker_vif_packet)(const std::uint8_t *data, std::uint32_t qwc);
  void (*worker_vif_write)(std::uint32_t reg, std::uint32_t value);
  void (*worker_vu_reset)();
  void (*render_gs)(int path, const std::uint8_t *data, std::uint32_t qwc);
  // Hash of the VIF1 state: the worker side (true) or the EE shadow (false). Called on the EE after a sync.
  std::uint64_t (*vif1_state_hash)(bool worker_side);
};

// Starts the worker when settings.enabled (EE thread, before emulation). Verify hashing works either way.
void start(const Settings &settings, const Donor &donor);

// Starts the worker with the donor callbacks of mtvu_bridge.cpp (slice170 capsule builds only).
void start_with_donor(const Settings &settings);

// True while the worker runs and GT4_MTVU is on: VIF1 is then shadowed on the EE thread.
bool enabled();
// True on the VU1 worker thread.
bool on_worker();

// EE entry points (no-ops when not enabled, except gif_transfer which renders directly).
void post_vif_packet(const std::uint8_t *data, std::uint32_t qwc);
void post_vif_write(std::uint32_t reg, std::uint32_t value);
// EE GIF transfer (PATH3 from DMA, or any path on the EE). Scans signals; a signal forces a synchronous drain.
void gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc);
// Shadow VIF1 DIRECT/DIRECTHL data (the worker emits it): scans signals; a signal forces a synchronous drain.
void shadow_direct(int path, const std::uint8_t *data, std::uint32_t qwc);
// GIF reset: synchronous drain, then the EE scanners restart.
void gif_reset();
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
