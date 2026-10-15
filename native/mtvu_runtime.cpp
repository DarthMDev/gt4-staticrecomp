// TASK-011 step 2: VU1 worker thread runtime (see mtvu_runtime.hpp and docs/MTVU-PLAN.md section 11).
#include "mtvu_runtime.hpp"

#include "mtvu_journal.hpp"
#include "mtvu_measure.hpp"
#include "mtvu_ring.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace gt4::mtvu {
namespace {

// Ring sizes from docs/MTVU-PLAN.md section 5.1.
constexpr std::size_t kInputBytes = 32u << 20;
constexpr std::size_t kOutputBytes = 8u << 20;
constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;

thread_local bool t_on_worker = false;

// Executes journal GS operations on the EE thread through the donor renderer.
class DonorSink final : public GsSink {
 public:
  explicit DonorSink(void (*render)(int, const std::uint8_t *, std::uint32_t)) : render_(render) {}
  void gif_transfer(std::uint8_t path, const std::uint8_t *data, std::size_t bytes) override {
    render_(path, data, static_cast<std::uint32_t>(bytes / 16));
  }
  // The runtime synchronises GIF, VBlank and privileged operations instead of journaling them (section 5.3).
  void gif_reset() override { throw std::logic_error("mtvu journal GIF reset is not used"); }
  void vertex_reset() override { throw std::logic_error("mtvu journal vertex reset is not used"); }
  void vblank_flush() override { throw std::logic_error("mtvu journal VBlank flush is not used"); }

 private:
  void (*render_)(int, const std::uint8_t *, std::uint32_t);
};

struct Runtime {
  Settings settings;
  Donor donor{};
  bool started = false;
  std::atomic<bool> running{false};  // worker alive and GT4_MTVU on: the EE shadows VIF1

  Doorbell bell;
  std::unique_ptr<SpscByteRing> input;
  std::unique_ptr<SpscByteRing> output;
  std::unique_ptr<DonorSink> sink;
  std::unique_ptr<EeJournal> journal;     // EE thread only
  std::unique_ptr<WorkerEnd> worker_end;  // worker thread only
  std::thread worker;

  std::atomic<bool> worker_failed{false};
  std::exception_ptr worker_error;  // written by the worker before worker_failed is released
  std::uint64_t worker_job = 0;     // worker thread only
  GifScanner worker_scanners[4];    // worker thread only: PATH1 invariant check
  GifScanner ee_scanners[4];        // EE thread only: signal detection before journaling

  std::uint64_t next_job = 1;  // EE thread only
  std::uint64_t vif_packets = 0, vif_writes = 0, ee_transfers = 0, direct_transfers = 0, gs_renders = 0;
  std::map<std::string, std::uint64_t> syncs;
  std::atomic<std::uint64_t> path1_signals{0};
  std::uint64_t verify_frames = 0, verify_mismatches = 0;
  std::uint64_t frame_hash = kFnvOffset;
  bool path1_reported = false;
};

Runtime &rt() {
  static Runtime runtime;
  return runtime;
}

bool parse_flag(const char *name, const char *value, bool fallback) {
  if (value == nullptr) return fallback;
  if (std::strcmp(value, "0") == 0) return false;
  if (std::strcmp(value, "1") == 0) return true;
  throw std::invalid_argument(std::string(name) + " must be unset, 0 or 1");
}

void fnv_bytes(std::uint64_t &hash, const void *data, std::size_t bytes) {
  const auto *at = static_cast<const std::uint8_t *>(data);
  for (std::size_t i = 0; i < bytes; ++i) {
    hash ^= at[i];
    hash *= kFnvPrime;
  }
}

void rethrow_worker_error() {
  Runtime &r = rt();
  if (r.worker_failed.load(std::memory_order_acquire)) std::rethrow_exception(r.worker_error);
}

void run_job(Runtime &r, RecordKind kind, const std::vector<std::uint8_t> &payload) {
  switch (kind) {
    case RecordKind::VifPacket:
      r.donor.worker_vif_packet(payload.data(), static_cast<std::uint32_t>(payload.size() / 16));
      break;
    case RecordKind::VifWrite: {
      if (payload.size() != 8) throw std::logic_error("mtvu VIF1 write record must carry 8 bytes");
      std::uint32_t reg = 0, value = 0;
      std::memcpy(&reg, payload.data(), 4);
      std::memcpy(&value, payload.data() + 4, 4);
      r.donor.worker_vif_write(reg, value);
      break;
    }
    case RecordKind::VuReset:
      r.donor.worker_vu_reset();
      break;
    default:
      throw std::logic_error("mtvu worker received an unexpected job kind");
  }
}

void worker_main() {
  t_on_worker = true;
  Runtime &r = rt();
  RecordHeader header;
  std::vector<std::uint8_t> payload;
  for (;;) {
    r.worker_end->receive(header, payload);
    const auto kind = static_cast<RecordKind>(header.kind);
    if (kind == RecordKind::Stop) return;
    r.worker_job = header.job;
    // After a failure the worker only acknowledges jobs, so the EE never waits forever; it rethrows at the next sync.
    if (!r.worker_failed.load(std::memory_order_acquire)) {
      try {
        run_job(r, kind, payload);
      } catch (...) {
        r.worker_error = std::current_exception();
        r.worker_failed.store(true, std::memory_order_release);
      }
    }
    r.worker_end->emit_job_done(header.job);
  }
}

}  // namespace

Settings parse_settings(const char *mtvu, const char *verify, const char *strict) {
  Settings settings;
  settings.enabled = parse_flag("GT4_MTVU", mtvu, true);
  settings.verify = parse_flag("GT4_MTVU_VERIFY", verify, false);
  settings.strict = parse_flag("GT4_MTVU_STRICT", strict, false);
  return settings;
}

Settings settings_from_environment() {
  return parse_settings(std::getenv("GT4_MTVU"), std::getenv("GT4_MTVU_VERIFY"), std::getenv("GT4_MTVU_STRICT"));
}

void start(const Settings &settings, const Donor &donor) {
  Runtime &r = rt();
  if (r.started) throw std::logic_error("mtvu started twice");
  r.started = true;
  r.settings = settings;
  r.donor = donor;
  if (!settings.enabled) return;
  if (!donor.worker_vif_packet || !donor.worker_vif_write || !donor.worker_vu_reset || !donor.render_gs ||
      !donor.vif1_state_hash) {
    throw std::logic_error("mtvu needs every donor callback");
  }
  r.input = std::make_unique<SpscByteRing>(kInputBytes, r.bell);
  r.output = std::make_unique<SpscByteRing>(kOutputBytes, r.bell);
  r.sink = std::make_unique<DonorSink>(donor.render_gs);
  r.journal = std::make_unique<EeJournal>(*r.input, *r.output, r.bell, *r.sink);
  r.worker_end = std::make_unique<WorkerEnd>(*r.input, *r.output, r.bell);
  r.running.store(true);
  r.worker = std::thread(worker_main);
}

bool enabled() { return rt().running.load(std::memory_order_acquire); }

bool on_worker() { return t_on_worker; }

void post_vif_packet(const std::uint8_t *data, std::uint32_t qwc) {
  Runtime &r = rt();
  if (!enabled()) return;
  const std::uint64_t job = r.next_job++;
  ++r.vif_packets;
  r.journal->post_job(RecordKind::VifPacket, job, data, static_cast<std::size_t>(qwc) * 16);
  r.journal->drain(false);
  rethrow_worker_error();
}

void post_vif_write(std::uint32_t reg, std::uint32_t value) {
  Runtime &r = rt();
  if (!enabled()) return;
  const std::uint64_t job = r.next_job++;
  ++r.vif_writes;
  std::uint8_t payload[8];
  std::memcpy(payload, &reg, 4);
  std::memcpy(payload + 4, &value, 4);
  r.journal->post_job(RecordKind::VifWrite, job, payload, sizeof payload);
  r.journal->drain(false);
  rethrow_worker_error();
}

void gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
  Runtime &r = rt();
  if (path < 0 || path > 3) throw std::logic_error("mtvu GIF path out of range");
  if (!enabled()) {
    r.donor.render_gs(path, data, qwc);
    return;
  }
  ++r.ee_transfers;
  PathCounts counts;
  r.ee_scanners[path].scan(data, qwc, counts);
  if (counts.signal + counts.finish + counts.label != 0) {
    // Signals raise interrupts on the EE: apply them at this point, after every earlier operation.
    sync("signal");
    ++r.gs_renders;
    r.donor.render_gs(path, data, qwc);
    return;
  }
  if (qwc == 0) return;
  if (r.journal->can_execute_now()) {
    ++r.gs_renders;
    r.donor.render_gs(path, data, qwc);
    return;
  }
  r.journal->append_ee_gif(static_cast<std::uint8_t>(path), data, static_cast<std::size_t>(qwc) * 16);
  r.journal->drain(false);
  rethrow_worker_error();
}

void shadow_direct(int path, const std::uint8_t *data, std::uint32_t qwc) {
  Runtime &r = rt();
  if (path < 0 || path > 3) throw std::logic_error("mtvu GIF path out of range");
  if (!enabled()) return;
  ++r.direct_transfers;
  PathCounts counts;
  r.ee_scanners[path].scan(data, qwc, counts);
  // The worker has this packet as a job already; the drain waits for its outputs, which the EE then renders.
  if (counts.signal + counts.finish + counts.label != 0) sync("direct_signal");
}

void gif_reset() {
  Runtime &r = rt();
  sync("gif_reset");
  for (GifScanner &scanner : r.ee_scanners) scanner = GifScanner{};
}

void note_render(int path, const std::uint8_t *data, std::uint32_t qwc) {
  Runtime &r = rt();
  if (!r.settings.verify) return;
  const auto path_byte = static_cast<std::uint8_t>(path);
  fnv_bytes(r.frame_hash, &path_byte, 1);
  fnv_bytes(r.frame_hash, data, static_cast<std::size_t>(qwc) * 16);
}

void sync(const char *reason) {
  Runtime &r = rt();
  if (!enabled() || t_on_worker) return;
  ++r.syncs[reason];
  r.journal->drain(true);
  rethrow_worker_error();
}

void pump() {
  Runtime &r = rt();
  if (!enabled()) return;
  r.journal->drain(false);
  rethrow_worker_error();
}

void vblank_sync() {
  Runtime &r = rt();
  if (!r.started) return;
  sync("vblank");
  if (!r.settings.verify) return;
  ++r.verify_frames;
  const std::uint64_t shadow = enabled() ? r.donor.vif1_state_hash(false) : 0;
  const std::uint64_t worker = enabled() ? r.donor.vif1_state_hash(true) : 0;
  const bool same = !enabled() || shadow == worker;
  if (!same) ++r.verify_mismatches;
  std::fprintf(stderr, "[mtvu-verify] frame %llu gs %016llx vif1 shadow %016llx worker %016llx %s\n",
               static_cast<unsigned long long>(r.verify_frames), static_cast<unsigned long long>(r.frame_hash),
               static_cast<unsigned long long>(shadow), static_cast<unsigned long long>(worker), same ? "same" : "MISMATCH");
  if (!same && r.settings.strict) throw std::runtime_error("mtvu strict: shadow and worker VIF1 state differ");
  r.frame_hash = kFnvOffset;
}

void worker_gif(int path, const std::uint8_t *data, std::uint32_t qwc) {
  Runtime &r = rt();
  if (!t_on_worker) throw std::logic_error("mtvu worker_gif called off the worker thread");
  if (qwc == 0) return;
  if (path == 1) {
    // Invariant (docs/MTVU-PLAN.md section 5.3): PATH1 never carries SIGNAL/FINISH/LABEL. Count and report.
    PathCounts counts;
    r.worker_scanners[1].scan(data, qwc, counts);
    if (counts.signal + counts.finish + counts.label != 0) {
      r.path1_signals.fetch_add(1, std::memory_order_relaxed);
      if (!r.path1_reported) {
        r.path1_reported = true;
        std::fprintf(stderr, "[mtvu] PATH1 SIGNAL/FINISH/LABEL in job %llu: IRQ applied late (known divergence)\n",
                     static_cast<unsigned long long>(r.worker_job));
      }
      if (r.settings.strict) throw std::runtime_error("mtvu strict: PATH1 SIGNAL/FINISH/LABEL");
    }
  }
  r.worker_end->emit_gif(static_cast<std::uint8_t>(path), r.worker_job, data, static_cast<std::size_t>(qwc) * 16);
}

void stop() {
  Runtime &r = rt();
  if (!r.running.load(std::memory_order_acquire)) return;
  try {
    sync("shutdown");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[mtvu] worker failed: %s\n", error.what());
  }
  r.running.store(false);
  r.journal->send_stop();
  r.worker.join();
}

void report(std::FILE *output) {
  Runtime &r = rt();
  if (!r.started) return;
  std::fprintf(output, "[mtvu] enabled %d verify %d strict %d worker_failed %d\n", r.settings.enabled ? 1 : 0,
               r.settings.verify ? 1 : 0, r.settings.strict ? 1 : 0, r.worker_failed.load() ? 1 : 0);
  if (r.journal) {
    std::fprintf(output, "[mtvu] jobs %llu vif_packets %llu vif_writes %llu ee_transfers %llu direct_transfers %llu renders %llu\n",
                 static_cast<unsigned long long>(r.journal->jobs_posted()), static_cast<unsigned long long>(r.vif_packets),
                 static_cast<unsigned long long>(r.vif_writes), static_cast<unsigned long long>(r.ee_transfers),
                 static_cast<unsigned long long>(r.direct_transfers), static_cast<unsigned long long>(r.gs_renders));
  }
  std::fprintf(output, "[mtvu] path1_signal %llu\n", static_cast<unsigned long long>(r.path1_signals.load()));
  for (const auto &[reason, count] : r.syncs)
    std::fprintf(output, "[mtvu] sync %s %llu\n", reason.c_str(), static_cast<unsigned long long>(count));
  if (r.settings.verify)
    std::fprintf(output, "[mtvu] verify frames %llu mismatches %llu\n", static_cast<unsigned long long>(r.verify_frames),
                 static_cast<unsigned long long>(r.verify_mismatches));
}

}  // namespace gt4::mtvu
