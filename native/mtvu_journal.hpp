// TASK-011 step 1: ordered GS-operation journal for the MTVU worker (docs/MTVU-PLAN.md sections 5.1-5.3).
//
// The EE thread owns the journal. Every GS operation that must keep its program order is appended here:
// EE-side GIF transfers (PATH3 uploads, DMA ch2), privileged resets and VBlank flushes, and one Placeholder per
// job posted to the worker. The worker produces GIF bytes for PATH1/PATH2 in emission order and a JobDone after
// the last output of each job. Draining the journal on the EE thread executes entries strictly in order:
// an EE entry runs immediately, a Placeholder consumes the worker output of its job up to JobDone.
//
// Deadlock rule (section 5.2): whenever the EE waits for input-ring space, it first moves all worker output
// into an EE-side queue (pump_output), so a worker blocked on a full output ring always makes progress.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "mtvu_ring.hpp"

namespace gt4::mtvu {

// The GS front end. Only the EE thread calls these, and only in journal order.
class GsSink {
 public:
  virtual ~GsSink() = default;
  virtual void gif_transfer(std::uint8_t path, const std::uint8_t *data, std::size_t bytes) = 0;
  virtual void gif_reset() = 0;
  virtual void vertex_reset() = 0;
  virtual void vblank_flush() = 0;
};

// Worker side: receives whole logical input records and emits outputs. Single thread only.
class WorkerEnd {
 public:
  WorkerEnd(SpscByteRing &input, SpscByteRing &output, Doorbell &bell);

  // Blocks until one whole logical record arrived (chunks are reassembled).
  void receive(RecordHeader &header, std::vector<std::uint8_t> &payload);

  void emit_gif(std::uint8_t path, std::uint64_t job, const std::uint8_t *data, std::size_t bytes);
  void emit_job_done(std::uint64_t job);

 private:
  SpscByteRing &input_;
  SpscByteRing &output_;
  Doorbell &bell_;
};

// EE side. Not thread safe: one EE thread calls every method.
class EeJournal {
 public:
  EeJournal(SpscByteRing &input, SpscByteRing &output, Doorbell &bell, GsSink &sink);

  // Queues a placeholder for `job`, then sends the input record (blocking for input space).
  void post_job(RecordKind kind, std::uint64_t job, const std::uint8_t *payload, std::size_t bytes);

  void append_ee_gif(std::uint8_t path, const std::uint8_t *data, std::size_t bytes);
  void append_gif_reset();
  void append_vertex_reset();
  void append_vblank_flush();

  // Executes every journal entry whose outputs are complete. Non-blocking: returns at the first incomplete job.
  // Blocking: waits for the worker until the journal is empty. Returns true when the journal is empty.
  bool drain(bool blocking);

  // True when no job is outstanding and no operation is queued, so the EE may execute GS operations directly.
  bool can_execute_now() const { return journal_.empty() && completed_.empty(); }

  std::size_t queued_entries() const { return journal_.size(); }
  std::size_t outstanding_jobs() const;

  // Sends the Stop record (no placeholder). The caller drains first; the worker then exits.
  void send_stop();

  std::uint64_t jobs_posted() const { return jobs_posted_; }
  std::uint64_t outputs_executed() const { return outputs_executed_; }

 private:
  enum class EntryKind : std::uint8_t { EeGif, GifReset, VertexReset, VblankFlush, Placeholder };

  struct JournalEntry {
    EntryKind kind = EntryKind::EeGif;
    std::uint8_t path = 0;
    std::uint64_t job = 0;
    std::vector<std::uint8_t> bytes;
  };

  struct CompletedEntry {
    bool job_done = false;  // true: JobDone marker; false: GIF bytes
    std::uint8_t path = 0;
    std::uint64_t job = 0;
    std::vector<std::uint8_t> bytes;
  };

  void send_chunks(SpscByteRing &ring, RecordHeader header, const std::uint8_t *payload, std::size_t bytes);
  void pump_output();
  bool execute_ready();
  bool consume_job(std::uint64_t job);

  SpscByteRing &input_;
  SpscByteRing &output_;
  Doorbell &bell_;
  GsSink &sink_;
  std::deque<JournalEntry> journal_;
  std::deque<CompletedEntry> completed_;
  std::vector<std::uint8_t> partial_gif_;  // reassembly of a multi-chunk worker GIF record
  std::uint8_t partial_path_ = 0;
  std::uint64_t partial_job_ = 0;
  std::uint64_t jobs_posted_ = 0;
  std::uint64_t outputs_executed_ = 0;
};

}  // namespace gt4::mtvu
