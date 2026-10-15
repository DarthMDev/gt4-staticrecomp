// TASK-011 step 1: EE-side journal and worker-side end (see mtvu_journal.hpp).
#include "mtvu_journal.hpp"

#include <algorithm>
#include <stdexcept>

namespace gt4::mtvu {

WorkerEnd::WorkerEnd(SpscByteRing &input, SpscByteRing &output, Doorbell &bell)
    : input_(input), output_(output), bell_(bell) {}

void WorkerEnd::receive(RecordHeader &header, std::vector<std::uint8_t> &payload) {
  payload.clear();
  std::vector<std::uint8_t> chunk;
  for (;;) {
    RecordHeader part;
    for (;;) {
      const std::uint32_t seen = bell_.snapshot();
      if (input_.try_pop(part, chunk)) break;
      bell_.wait_changed(seen);
    }
    payload.insert(payload.end(), chunk.begin(), chunk.end());
    if ((part.flags & kFlagMore) == 0) {
      header = part;
      header.flags = 0;
      header.bytes = static_cast<std::uint32_t>(payload.size());
      return;
    }
  }
}

void WorkerEnd::emit_gif(std::uint8_t path, std::uint64_t job, const std::uint8_t *data, std::size_t bytes) {
  std::size_t offset = 0;
  do {
    const std::size_t chunk = std::min(bytes - offset, output_.max_payload());
    RecordHeader header;
    header.kind = static_cast<std::uint16_t>(RecordKind::Gif);
    header.path = path;
    header.job = job;
    header.bytes = static_cast<std::uint32_t>(chunk);
    header.flags = offset + chunk < bytes ? kFlagMore : 0;
    for (;;) {
      const std::uint32_t seen = bell_.snapshot();
      if (output_.try_push(header, data + offset)) break;
      bell_.wait_changed(seen);
    }
    offset += chunk;
  } while (offset < bytes);
}

void WorkerEnd::emit_job_done(std::uint64_t job) {
  RecordHeader header;
  header.kind = static_cast<std::uint16_t>(RecordKind::JobDone);
  header.job = job;
  for (;;) {
    const std::uint32_t seen = bell_.snapshot();
    if (output_.try_push(header, nullptr)) return;
    bell_.wait_changed(seen);
  }
}

EeJournal::EeJournal(SpscByteRing &input, SpscByteRing &output, Doorbell &bell, GsSink &sink)
    : input_(input), output_(output), bell_(bell), sink_(sink) {}

std::size_t EeJournal::outstanding_jobs() const {
  return static_cast<std::size_t>(std::count_if(journal_.begin(), journal_.end(), [](const JournalEntry &entry) {
    return entry.kind == EntryKind::Placeholder;
  }));
}

void EeJournal::post_job(RecordKind kind, std::uint64_t job, const std::uint8_t *payload, std::size_t bytes) {
  JournalEntry placeholder;
  placeholder.kind = EntryKind::Placeholder;
  placeholder.job = job;
  journal_.push_back(std::move(placeholder));

  RecordHeader header;
  header.kind = static_cast<std::uint16_t>(kind);
  header.job = job;
  send_chunks(input_, header, payload, bytes);
  ++jobs_posted_;
}

void EeJournal::append_ee_gif(std::uint8_t path, const std::uint8_t *data, std::size_t bytes) {
  JournalEntry entry;
  entry.kind = EntryKind::EeGif;
  entry.path = path;
  if (bytes != 0) entry.bytes.assign(data, data + bytes);
  journal_.push_back(std::move(entry));
}

void EeJournal::append_gif_reset() {
  JournalEntry entry;
  entry.kind = EntryKind::GifReset;
  journal_.push_back(std::move(entry));
}

void EeJournal::append_vertex_reset() {
  JournalEntry entry;
  entry.kind = EntryKind::VertexReset;
  journal_.push_back(std::move(entry));
}

void EeJournal::append_vblank_flush() {
  JournalEntry entry;
  entry.kind = EntryKind::VblankFlush;
  journal_.push_back(std::move(entry));
}

bool EeJournal::drain(bool blocking) {
  for (;;) {
    const std::uint32_t seen = bell_.snapshot();
    pump_output();
    if (execute_ready()) return true;
    if (!blocking) return false;
    bell_.wait_changed(seen);
  }
}

void EeJournal::send_stop() {
  RecordHeader header;
  header.kind = static_cast<std::uint16_t>(RecordKind::Stop);
  send_chunks(input_, header, nullptr, 0);
}

void EeJournal::send_chunks(SpscByteRing &ring, RecordHeader header, const std::uint8_t *payload, std::size_t bytes) {
  std::size_t offset = 0;
  do {
    const std::size_t chunk = std::min(bytes - offset, ring.max_payload());
    header.bytes = static_cast<std::uint32_t>(chunk);
    header.flags = offset + chunk < bytes ? kFlagMore : 0;
    for (;;) {
      const std::uint32_t seen = bell_.snapshot();
      if (ring.try_push(header, payload + offset)) break;
      // Deadlock rule: free the output ring while waiting for input space.
      pump_output();
      bell_.wait_changed(seen);
    }
    offset += chunk;
  } while (offset < bytes);
}

void EeJournal::pump_output() {
  RecordHeader header;
  std::vector<std::uint8_t> chunk;
  while (output_.try_pop(header, chunk)) {
    if (header.kind == static_cast<std::uint16_t>(RecordKind::Gif)) {
      partial_gif_.insert(partial_gif_.end(), chunk.begin(), chunk.end());
      partial_path_ = header.path;
      partial_job_ = header.job;
      if ((header.flags & kFlagMore) == 0) {
        CompletedEntry entry;
        entry.job_done = false;
        entry.path = partial_path_;
        entry.job = partial_job_;
        entry.bytes = std::move(partial_gif_);
        partial_gif_.clear();
        completed_.push_back(std::move(entry));
      }
    } else if (header.kind == static_cast<std::uint16_t>(RecordKind::JobDone)) {
      if (!partial_gif_.empty()) throw std::logic_error("mtvu JobDone inside an unfinished GIF record");
      CompletedEntry entry;
      entry.job_done = true;
      entry.job = header.job;
      completed_.push_back(std::move(entry));
    } else {
      throw std::logic_error("mtvu worker sent an unexpected output record");
    }
  }
}

bool EeJournal::execute_ready() {
  while (!journal_.empty()) {
    JournalEntry &entry = journal_.front();
    switch (entry.kind) {
      case EntryKind::EeGif:
        sink_.gif_transfer(entry.path, entry.bytes.data(), entry.bytes.size());
        ++outputs_executed_;
        break;
      case EntryKind::GifReset:
        sink_.gif_reset();
        break;
      case EntryKind::VertexReset:
        sink_.vertex_reset();
        break;
      case EntryKind::VblankFlush:
        sink_.vblank_flush();
        break;
      case EntryKind::Placeholder:
        if (!consume_job(entry.job)) return false;
        break;
    }
    journal_.pop_front();
  }
  return true;
}

bool EeJournal::consume_job(std::uint64_t job) {
  while (!completed_.empty()) {
    CompletedEntry &front = completed_.front();
    if (front.job != job) throw std::logic_error("mtvu output arrived out of job order");
    if (front.job_done) {
      completed_.pop_front();
      return true;
    }
    sink_.gif_transfer(front.path, front.bytes.data(), front.bytes.size());
    ++outputs_executed_;
    completed_.pop_front();
  }
  return false;
}

}  // namespace gt4::mtvu
